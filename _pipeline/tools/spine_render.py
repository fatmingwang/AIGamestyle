"""Draw a Spine 3.8 skeleton in its setup pose, as the game would show it standing still (Doc/Reskin_Game_Art.md).

    python spine_render.py <skeleton.json> <page dir> <out.png> [--atlas X.atlas] [--scale 0.5] [--only-bones-under all]
                          [--hide "^(85_|frame|FX)"]

A check for reskins: a Spine character is many parts on one atlas page, so a page image says little about how the
character looks put together. This puts it together: each bone's world transform from its setup values, region
attachments placed on their bones, meshes deformed by their bone weights, each triangle textured from its part's
picture, in the slots' draw order (additive slots added). The page dir holds the atlas page(s) -- give the reskinned
folder to see the reskin with the original's .atlas and .json (they are never changed).

--anim / --t: a frame of an animation (bone rotate / translate / scale, attachment switches and mesh deforms, linear
between keys -- Spine's curves are not followed). Not drawn: clipping, paths, IK / transform constraints, draw-order
changes, slot dark colours. Good enough to judge a part's look, not a pixel-exact Spine render.
"""
import argparse
import json
import math
import re
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from spine_page_restyle import footprint, parse_atlas


def bone_world(bones):
    """name -> (a, b, c, d, wx, wy) for the setup pose (transform mode 'normal' only)."""
    out = {}
    for b in bones:
        rot = b.get("rotation", 0.0)
        sx, sy = b.get("scaleX", 1.0), b.get("scaleY", 1.0)
        shx, shy = b.get("shearX", 0.0), b.get("shearY", 0.0)
        la = math.cos(math.radians(rot + shx)) * sx
        lb = math.cos(math.radians(rot + 90 + shy)) * sy
        lc = math.sin(math.radians(rot + shx)) * sx
        ld = math.sin(math.radians(rot + 90 + shy)) * sy
        x, y = b.get("x", 0.0), b.get("y", 0.0)
        p = out.get(b.get("parent"))
        if p is None:
            out[b["name"]] = (la, lb, lc, ld, x, y)
            continue
        pa, pb, pc, pd, px, py = p
        out[b["name"]] = (pa * la + pb * lc, pa * lb + pb * ld, pc * la + pd * lc, pc * lb + pd * ld,
                          pa * x + pb * y + px, pc * x + pd * y + py)
    return out


def part_canvases(atlas_path, page_dir, pma=None):
    """region name -> its untrimmed picture (orig size), upright, from the page in page_dir."""
    out = {}
    for page in parse_atlas(atlas_path):
        img = Image.open(Path(page_dir) / page["file"]).convert("RGBA")
        page_pma = _is_pma(img) if pma is None else pma
        for r in page["regions"]:
            part = img.crop(footprint(r))
            if r["rotated"]:
                part = part.transpose(Image.ROTATE_270)
            nums = lambda k, d: tuple(int(float(t)) for t in r[k].split(",")) if r.get(k) else d
            ow, oh = nums("orig", part.size)
            ox, oy = nums("offset", (0, 0))
            canvas = Image.new("RGBA", (ow, oh), (0, 0, 0, 0))
            canvas.paste(part, (ox, oh - oy - part.size[1]))   # atlas offsets count y from the bottom
            canvas.info["pma"] = page_pma   # a premultiplied page: drawn as the game blends it
            out[r["name"]] = canvas
    return out


def _is_pma(img):
    from restyle_pilot import is_premultiplied
    return is_premultiplied(img)


def draw_triangle(dst, tex, src_tri, dst_tri, tint, additive):
    """Texture one triangle: an affine map from dst back to src, inside the triangle's mask."""
    xs, ys = [p[0] for p in dst_tri], [p[1] for p in dst_tri]
    x0, y0 = int(math.floor(min(xs))), int(math.floor(min(ys)))
    x1, y1 = int(math.ceil(max(xs))) + 1, int(math.ceil(max(ys))) + 1
    if x1 <= 0 or y1 <= 0 or x0 >= dst.size[0] or y0 >= dst.size[1] or x1 - x0 < 1 or y1 - y0 < 1:
        return
    (ax, ay), (bx, by), (cx, cy) = [(p[0] - x0, p[1] - y0) for p in dst_tri]
    det = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
    if abs(det) < 1e-9:
        return
    (ua, va), (ub, vb), (uc, vc) = src_tri
    # dst (x, y) -> src (u, v): solve the affine through the three corners
    m = np.linalg.solve(np.array([[ax, ay, 1], [bx, by, 1], [cx, cy, 1]], dtype=float),
                        np.array([[ua, va], [ub, vb], [uc, vc]], dtype=float))
    coeffs = (m[0, 0], m[1, 0], m[2, 0], m[0, 1], m[1, 1], m[2, 1])
    w, h = x1 - x0, y1 - y0
    piece = tex.transform((w, h), Image.AFFINE, coeffs, resample=Image.BILINEAR)
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).polygon([(ax, ay), (bx, by), (cx, cy)], fill=255)
    a = np.asarray(piece, dtype=np.float32)
    a[..., :3] *= np.array(tint[:3], dtype=np.float32)
    a[..., 3] *= tint[3] * (np.asarray(mask, dtype=np.float32) / 255.0)
    if tex.info.get("pma") and not additive:   # premultiplied: colour + background * (1 - alpha), stray colour shows
        m = (np.asarray(mask, dtype=np.float32) / 255.0)[..., None]
        sa = a[..., 3:4] / 255.0
        src = np.asarray(piece, dtype=np.float32)
        src[..., :3] *= np.array(tint[:3], dtype=np.float32) * tint[3]
        bx0, by0 = max(x0, 0), max(y0, 0)
        bx1, by1 = min(x1, dst.size[0]), min(y1, dst.size[1])
        sl = (slice(by0 - y0, by1 - y0), slice(bx0 - x0, bx1 - x0))
        base = np.asarray(dst.crop((bx0, by0, bx1, by1)), dtype=np.float32)
        brgb = base[..., :3] * (base[..., 3:4] / 255.0)          # the background, premultiplied
        rgb = src[sl][..., :3] * m[sl] + brgb * (1 - sa[sl] * m[sl])
        al = (sa[sl] * m[sl] + base[..., 3:4] / 255.0 * (1 - sa[sl] * m[sl]))
        outp = np.concatenate([np.where(al > 0, rgb / np.maximum(al, 1e-6), 0), al * 255], -1)
        dst.paste(Image.fromarray(np.clip(outp, 0, 255).astype(np.uint8), "RGBA"), (bx0, by0))
        return
    piece = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA")
    if additive:
        base = np.asarray(dst.crop((x0, y0, x1, y1)), dtype=np.float32)
        add = np.asarray(piece, dtype=np.float32)
        base[..., :3] = np.minimum(255, base[..., :3] + add[..., :3] * (add[..., 3:4] / 255.0))
        dst.paste(Image.fromarray(base.astype(np.uint8), "RGBA"), (x0, y0))
    else:
        dst.alpha_composite(piece, (max(x0, 0), max(y0, 0)), (max(-x0, 0), max(-y0, 0)))


def hex_color(s):
    s = (s or "ffffffff").ljust(8, "f")
    return [int(s[i:i + 2], 16) / 255.0 for i in range(0, 8, 2)]


def _sample(keys, t, fields, default):
    """A timeline's value at time t: linear between keys (Spine's curves are not followed), held after the last."""
    if not keys:
        return default
    prev = None
    for k in keys:
        kt = k.get("time", 0.0)
        if kt > t:
            if prev is None:
                return [k.get(f, d) for f, d in zip(fields, default)]
            pt = prev.get("time", 0.0)
            u = 0.0 if prev.get("curve") == "stepped" or kt == pt else (t - pt) / (kt - pt)
            return [prev.get(f, d) + (k.get(f, d) - prev.get(f, d)) * u for f, d in zip(fields, default)]
        prev = k
    return [prev.get(f, d) for f, d in zip(fields, default)]


def posed_bones(bones, anim, t):
    """The bones with an animation's rotate / translate / scale timelines applied at time t (setup + timeline)."""
    out = []
    tl = (anim or {}).get("bones", {})
    for b in bones:
        b = dict(b)
        k = tl.get(b["name"], {})
        if "rotate" in k:
            b["rotation"] = b.get("rotation", 0.0) + _sample(k["rotate"], t, ["angle"], [0.0])[0]
        if "translate" in k:
            dx, dy = _sample(k["translate"], t, ["x", "y"], [0.0, 0.0])
            b["x"], b["y"] = b.get("x", 0.0) + dx, b.get("y", 0.0) + dy
        if "scale" in k:
            sx, sy = _sample(k["scale"], t, ["x", "y"], [1.0, 1.0])
            b["scaleX"], b["scaleY"] = b.get("scaleX", 1.0) * sx, b.get("scaleY", 1.0) * sy
        out.append(b)
    return out


def _weight_counts(v, n):
    """The positions of the bone counts in a weighted mesh's vertices array."""
    out, k = [], 0
    for _ in range(n):
        out.append(k)
        k += 1 + 4 * int(v[k])
    return out


def _deform(anim, slot, att, t, n):
    """The deform timeline's vertex offsets for one attachment at time t (n numbers), or None."""
    for sk in (anim or {}).get("deform", {}).values():
        keys = sk.get(slot, {}).get(att)
        if not keys:
            continue
        def full(k):
            v = np.zeros(n)
            vs = k.get("vertices", [])
            o = k.get("offset", 0)
            v[o:o + len(vs)] = vs
            return v
        prev = None
        for k in keys:
            kt = k.get("time", 0.0)
            if kt > t:
                if prev is None:
                    return full(k)
                pt = prev.get("time", 0.0)
                u = 0.0 if kt == pt else (t - pt) / (kt - pt)
                return full(prev) + (full(k) - full(prev)) * u
            prev = k
        return full(prev)
    return None


def render(skel_path, page_dir, atlas_path=None, scale=0.5, hide=None, under=None, pad=40, bounds=None, show=None,
           with_bounds=False, geometry=False, anim=None, t=0.0, pma=None):
    """The setup pose as an RGBA image. bounds=(minx, maxx, miny, maxy) in skeleton units fixes the frame (so renders
    of different slot sets line up); show: a regex, only slots it matches are drawn. with_bounds: also return them."""
    j = json.loads(Path(skel_path).read_text(encoding="utf-8"))
    atlas_path = atlas_path or Path(skel_path).with_suffix(".atlas")
    canv = part_canvases(Path(atlas_path), page_dir, pma)
    animation = j.get("animations", {}).get(anim) if anim else None
    world = bone_world(posed_bones(j["bones"], animation, t) if animation else j["bones"])
    slot_att = {}   # attachment timelines: the attachment shown at time t
    for sname, tl in (animation or {}).get("slots", {}).items():
        keys = [k for k in tl.get("attachment", []) if k.get("time", 0.0) <= t]
        if keys:
            slot_att[sname] = keys[-1].get("name")
    parents = {b["name"]: b.get("parent") for b in j["bones"]}

    def is_under(bone):
        while bone:
            if bone == under:
                return True
            bone = parents.get(bone)
        return False

    skins = j["skins"] if isinstance(j["skins"], list) else [{"name": k, "attachments": v} for k, v in j["skins"].items()]
    atts = next((s["attachments"] for s in skins if s.get("name") == "default"), skins[0]["attachments"])
    bone_list = [b["name"] for b in j["bones"]]
    hide_rx = re.compile(hide) if hide else None
    draws = []   # (triangles [(src_tri, dst_tri)], canvas, tint, additive), in draw order, in skeleton units
    for slot in j["slots"]:
        name = slot_att.get(slot["name"], slot.get("attachment"))
        if not name or (hide_rx and hide_rx.search(slot["name"])) or (under and not is_under(slot["bone"])) or                 (show and not re.search(show, slot["name"])):
            continue
        a = atts.get(slot["name"], {}).get(name)
        if a is None:
            continue
        kind = a.get("type", "region")
        tex = canv.get(a.get("path", name))
        if tex is None:
            continue
        tint = hex_color(slot.get("color"))
        tint = [t * c for t, c in zip(tint, hex_color(a.get("color")))]
        bw = world[slot["bone"]]
        tw, th = tex.size
        tris = []
        if kind == "region":
            w, h = a.get("width", tw), a.get("height", th)
            sx, sy, rot = a.get("scaleX", 1.0), a.get("scaleY", 1.0), math.radians(a.get("rotation", 0.0))
            cos, sin = math.cos(rot), math.sin(rot)
            corners = []
            for lx, ly in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
                lx, ly = lx * sx, ly * sy
                x = a.get("x", 0.0) + lx * cos - ly * sin
                y = a.get("y", 0.0) + lx * sin + ly * cos
                corners.append((bw[0] * x + bw[1] * y + bw[4], bw[2] * x + bw[3] * y + bw[5]))
            uv = [(0, th), (tw, th), (tw, 0), (0, 0)]   # bottom-left, bottom-right, top-right, top-left (y up)
            tris = [((uv[0], uv[1], uv[2]), (corners[0], corners[1], corners[2])),
                    ((uv[0], uv[2], uv[3]), (corners[0], corners[2], corners[3]))]
        elif kind == "mesh":
            uvs = a["uvs"]
            n = len(uvs) // 2
            v = a["vertices"]
            nw = len(v) if len(v) == len(uvs) else sum(int(v[k]) for k in _weight_counts(v, len(uvs) // 2))
            off = _deform(animation, slot["name"], name, t, len(v) if len(v) == len(uvs) else 2 * nw) if animation else None
            pts = []
            if len(v) == len(uvs):   # not weighted: in the slot bone's space
                for i in range(n):
                    x, y = v[2 * i], v[2 * i + 1]
                    if off is not None:
                        x, y = x + off[2 * i], y + off[2 * i + 1]
                    pts.append((bw[0] * x + bw[1] * y + bw[4], bw[2] * x + bw[3] * y + bw[5]))
            else:
                k = 0
                wi = 0
                for _ in range(n):
                    cnt = int(v[k]); k += 1
                    wx = wy = 0.0
                    for _ in range(cnt):
                        bi, x, y, wt = int(v[k]), v[k + 1], v[k + 2], v[k + 3]
                        if off is not None:
                            x, y = x + off[2 * wi], y + off[2 * wi + 1]
                        wi += 1
                        k += 4
                        b = world[bone_list[bi]]
                        wx += (b[0] * x + b[1] * y + b[4]) * wt
                        wy += (b[2] * x + b[3] * y + b[5]) * wt
                    pts.append((wx, wy))
            src = [(uvs[2 * i] * tw, uvs[2 * i + 1] * th) for i in range(n)]
            ti = a["triangles"]
            tris = [((src[ti[i]], src[ti[i + 1]], src[ti[i + 2]]), (pts[ti[i]], pts[ti[i + 1]], pts[ti[i + 2]])) for i in range(0, len(ti), 3)]
        else:
            continue
        draws.append((tris, tex, tint, slot.get("blend") == "additive", slot["name"], a.get("path", name)))

    # the bounds of everything drawn, then draw at `scale` with y down
    allp = [p for d0 in draws for _, d in d0[0] for p in d]
    if bounds:
        minx, maxx, miny, maxy = bounds
    elif not allp:
        return (Image.new("RGBA", (16, 16)), None) if with_bounds else Image.new("RGBA", (16, 16))
    else:
        minx, maxx = min(p[0] for p in allp), max(p[0] for p in allp)
        miny, maxy = min(p[1] for p in allp), max(p[1] for p in allp)
    W, H = int((maxx - minx) * scale) + 2 * pad, int((maxy - miny) * scale) + 2 * pad
    to = lambda p: ((p[0] - minx) * scale + pad, (maxy - p[1]) * scale + pad)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if geometry:   # the parts in the frame's pixels, for projecting a picture back onto them (spine_project.py)
        return [{"slot": sl, "region": rg, "tex": tex, "tint": tint, "additive": add,
                 "tris": [(s, [to(p) for p in d]) for s, d in tris]} for tris, tex, tint, add, sl, rg in draws], (W, H), (minx, maxx, miny, maxy)
    for tris, tex, tint, additive, *_ in draws:
        for s, d in tris:
            draw_triangle(out, tex, s, [to(p) for p in d], tint, additive)
    return (out, (minx, maxx, miny, maxy)) if with_bounds else out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("skeleton")
    ap.add_argument("page_dir")
    ap.add_argument("out")
    ap.add_argument("--atlas")
    ap.add_argument("--scale", type=float, default=0.5)
    ap.add_argument("--hide", help="regex of slot names not to draw (backgrounds, effects)")
    ap.add_argument("--under", help="draw only slots on bones under this bone")
    ap.add_argument("--bg", default="404040")
    ap.add_argument("--anim", help="an animation's pose instead of the setup pose")
    ap.add_argument("--t", type=float, default=0.0, help="the time in that animation (seconds)")
    ap.add_argument("--pma", choices=["auto", "on", "off"], default="auto",
                    help="premultiplied pages: auto = judged from the pixels; on / off = as the game is set (a broken page "
                         "cannot be judged from its own pixels: give the original's setting)")
    a = ap.parse_args()
    img = render(a.skeleton, a.page_dir, a.atlas, a.scale, a.hide, a.under, anim=a.anim, t=a.t,
                 pma=None if a.pma == "auto" else a.pma == "on")
    box = img.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()   # only what is drawn
    if box:
        img = img.crop((max(0, box[0] - 20), max(0, box[1] - 20), min(img.size[0], box[2] + 20), min(img.size[1], box[3] + 20)))
    bg = Image.new("RGBA", img.size, tuple(int(a.bg[i:i + 2], 16) for i in (0, 2, 4)) + (255,))
    bg.alpha_composite(img)
    bg.convert("RGB").save(a.out)
    print(f"{a.out}: {img.size}")


if __name__ == "__main__":
    main()
