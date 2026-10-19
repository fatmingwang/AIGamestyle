"""Paint a redrawn picture of a Spine character back onto its parts (Doc/Reskin_Game_Art.md, "Changing a character").

    project(skeleton, page_dir, out_dir, concept, scale, bounds, masks=None, hidden=())

The way to change a character's body or clothes (a woman into a man) without the Spine editor: draw the whole
character once, in its setup pose (spine_render.py gives the pose; ComfyUI redraws it), then copy that drawing back
onto every part through the part's own mesh / region:

  * a part's pixels take the drawing's colour where that part is the one seen in the setup pose; pixels hidden under
    another part (a torso under an arm) take their nearest seen pixel's colour, so they look right when the arm moves;
  * a part's outline may only SHRINK: its new alpha is its old alpha, times `masks[slot]` (a picture in the frame's
    pixels: where the part may stay -- a skirt cut to trousers). A slot in `hidden` becomes fully transparent (a
    braid). Nothing grows past its old outline, so the meshes, bones and animations stay valid;
  * a part not seen in the setup pose at all (closed eyelids, other mouth shapes) is left as it was.

The .atlas and the skeleton .json are never changed; only the page pictures are, at the same size. A premultiplied
page (Spine's "premultiply alpha") is read and written premultiplied, with no colour in fully transparent pixels --
otherwise the game adds the stray colour (a ghost of the cut-off skirt, glowing part edges).
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

import spine_render as sr
from restyle_pilot import is_premultiplied, premultiply, unpremultiply
from spine_page_restyle import footprint, parse_atlas


def _coverage(tris, tex, size):
    """Where a part is drawn (its own alpha, through its triangles), as a bool array in frame pixels."""
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    for s, d in tris:
        sr.draw_triangle(img, tex, s, d, (1, 1, 1, 1), False)
    return np.asarray(img)[..., 3] > 100


def _back(tris, screen_img, canvas_size):
    """screen_img (frame pixels) carried back onto the part's canvas through its triangles."""
    out = Image.new("RGBA", canvas_size, (0, 0, 0, 0))
    for s, d in tris:
        sr.draw_triangle(out, screen_img, d, s, (1, 1, 1, 1), False)
    return out


def _grow(rgb, have, alpha, rounds=60):
    """Fill pixels inside the alpha that have no colour from their coloured neighbours (8 directions per round)."""
    want = (alpha > 0) & ~have
    h, w = have.shape
    for _ in range(rounds):
        if not want.any():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros((h, w), np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy == 0 and dx == 0:
                    continue
                sh = np.roll(np.roll(have, dy, 0), dx, 1)
                sc = np.roll(np.roll(rgb, dy, 0), dx, 1)
                take = sh & want
                acc[take] += sc[take]
                cnt[take] += 1
        new = cnt > 0
        rgb[new] = acc[new] / cnt[new][:, None]
        have = have | new
        want = want & ~new
    return rgb, have


def project(skeleton, page_dir, out_dir, concept, scale, bounds, masks=None, hidden=(), atlas=None, keep=(), trust=None):
    masks = masks or {}
    skeleton = Path(skeleton)
    atlas = Path(atlas) if atlas else skeleton.with_suffix(".atlas")
    # a premultiplied page (the source's own setting) is worked on in plain colours and written premultiplied again
    pma = {page["file"]: is_premultiplied(Image.open(Path(page_dir) / page["file"])) for page in parse_atlas(atlas)}
    geo, size, _ = sr.render(skeleton, page_dir, atlas, scale=scale, bounds=bounds, geometry=True)
    concept = concept.convert("RGBA").resize(size) if concept.size != size else concept.convert("RGBA")
    # who is seen where: the last part drawn over a pixel, with masks applied and hidden parts left out
    owner = np.full((size[1], size[0]), -1, np.int32)
    allowed = {}
    for i, g in enumerate(geo):
        if g["slot"] in hidden or g["additive"]:
            continue
        cov = _coverage(g["tris"], g["tex"], size)
        if g["slot"] in keep:   # a kept part (an arm) still covers what lies under it: nobody takes colour there
            owner[cov] = -2
            continue
        if g["slot"] in masks:
            allowed[g["slot"]] = np.asarray(masks[g["slot"]].convert("L").resize(size)) > 127
            cov &= allowed[g["slot"]]
        owner[cov] = i
    crgb = np.asarray(concept)[..., :3]
    # `trust` (frame pixels): where the concept's colour may be taken; elsewhere (its soft edge, which holds
    # background colour) the part's pixels are filled from their neighbours inside, so no light halo
    trusted = np.asarray(trust.convert("L").resize(size)) > 200 if trust is not None else True

    new_canvas, gone = {}, set()
    for i, g in enumerate(geo):
        rg = g["region"]
        if g["slot"] in hidden:
            gone.add(rg)
            continue
        if g["additive"] or g["slot"] in keep or rg in new_canvas:
            continue
        seen = owner == i
        if not seen.any():
            continue   # not seen in the setup pose: left as it was
        tex = unpremultiply(g["tex"]) if g["tex"].info.get("pma") else g["tex"]
        scr = np.zeros((size[1], size[0], 4), np.uint8)
        scr[..., :3] = crgb
        scr[..., 3] = np.where(seen & trusted, 255, 0)
        painted = np.asarray(_back(g["tris"], Image.fromarray(scr, "RGBA"), tex.size), dtype=np.float32)
        old = np.asarray(tex, dtype=np.float32)
        alpha = old[..., 3].copy()
        if g["slot"] in allowed:   # the outline may only shrink
            ok = np.asarray(_back(g["tris"], Image.fromarray(np.dstack([np.zeros((size[1], size[0], 3), np.uint8),
                                                                        (allowed[g["slot"]] * 255).astype(np.uint8)]), "RGBA"),
                                  tex.size))[..., 3] > 127
            alpha = np.where(ok, alpha, 0)
        have = painted[..., 3] > 127
        rgb = np.where(have[..., None], painted[..., :3], 0).astype(np.float32)
        rgb, have = _grow(rgb, have, alpha)
        rgb = np.where(have[..., None], rgb, old[..., :3])
        rgb = np.where(alpha[..., None] > 0, rgb, 0)   # nothing in fully transparent pixels
        new_canvas[rg] = Image.fromarray(np.dstack([np.clip(rgb, 0, 255), alpha]).astype(np.uint8), "RGBA")

    # back into the pages: each region's trimmed rectangle of its canvas, turned back if it is stored rotated
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    changed = []
    for page in parse_atlas(atlas):
        src_page = Path(page_dir) / page["file"]
        img = Image.open(src_page).convert("RGBA")
        for r in page["regions"]:
            if r["name"] not in new_canvas and r["name"] not in gone:
                continue
            box = footprint(r)
            w, h = (box[2] - box[0], box[3] - box[1])
            uw, uh = (h, w) if r["rotated"] else (w, h)
            nums = lambda k, d: tuple(int(float(t)) for t in r[k].split(",")) if r.get(k) else d
            ow, oh = nums("orig", (uw, uh))
            ox, oy = nums("offset", (0, 0))
            if r["name"] in gone:
                part = Image.new("RGBA", (uw, uh), (0, 0, 0, 0))
            else:
                part = new_canvas[r["name"]].crop((ox, oh - oy - uh, ox + uw, oh - oy))
            if r["rotated"]:
                part = part.transpose(Image.ROTATE_90)
            if pma[page["file"]]:
                part = premultiply(part)
            clear = Image.new("RGBA", part.size, (0, 0, 0, 0))
            img.paste(clear, box[:2])
            img.paste(part, box[:2])
            changed.append(r["name"])
        img.save(out_dir / page["file"])
    return changed
