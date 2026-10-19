"""Change a Spine character (a woman into a man, another body or outfit) without the Spine editor
(Doc/Reskin_Game_Art.md, "Changing a character").

    python character_swap.py ..\\style_profiles\\man_10085.json [--seed 22] [--concept-only] [--frames]

A part's picture may lose pixels but must not grow past its old outline (its mesh would cut it off), so the new
character is the old one's outline, cut down where the new body is smaller (a skirt into trousers, no bust, no braid),
and repainted inside. Four steps:

  1. the guide: the setup pose drawn by spine_render.py, with the plan's "hide" parts left out (long hair, a ribbon)
     and the torso cut to trousers above the real legs ("trousers": the waist is the torso's narrowest row, each leg
     tapers from the hip to its calf just below the hem); the legs keep only what shows below the hem;
  2. the concept: ComfyUI redraws the guide as the new character (img2img + ControlNet canny holding the new outline);
  3. the projection (spine_project.py): every part not in "keep" takes the concept's colours where it is the part
     seen, its hidden pixels from its nearest seen ones; "hide" parts become transparent; the cuts shrink the torso
     and legs. "keep" parts (arms, hands, sleeves, props: the same for the new character) keep their own pixels;
  4. the output: the whole game copied to the plan's output with the changed atlas page(s) -- the .atlas and the
     skeleton .json files are never changed, so every skeleton on that page (and every animation) shows the new
     character. --frames renders setup + animation frames for a look (spine_render.py --anim).

Then the plan's "restyle_parts" are restyled alone inside their outlines (sleeves mostly hidden behind the body,
closed eyelids never seen standing still), and "trousers"."cover_shoulders" (a colour) paints the torso's skin below
the neck as the shirt in the guide, so the new character's shirt closes over bare shoulders.

The plan (style_profiles/<name>.json): source, output, skeleton (relative to the game), hide, keep, trousers
{torso, legs, seat, margin, cover_shoulders}, restyle_parts {region: subject}, prompt, negative, seed, denoise,
control, part_denoise, part_control, scale. A premultiplied page stays premultiplied.
"""
import argparse
import json
import re
import shutil
from pathlib import Path

import numpy as np
from PIL import Image

import restyle_pilot as rp
import spine_project as sp
import spine_render as sr


def load_plan(path):
    plan = json.loads(Path(path).read_text(encoding="utf-8"))
    base = Path(path).resolve().parent.parent.parent   # style_profiles -> _pipeline -> AIGamestyle
    plan["source"] = (base / plan["source"]).resolve()
    plan["output"] = (base / plan["output"]).resolve()
    return plan


def ranges(row, min_w=15):
    xs = np.nonzero(row)[0]
    if xs.size == 0:
        return []
    cuts = np.nonzero(np.diff(xs) > 3)[0]
    starts, ends = np.r_[xs[0], xs[cuts + 1]], np.r_[xs[cuts], xs[-1]]
    return [(int(a), int(b)) for a, b in zip(starts, ends) if b - a > min_w]


def build_guide(plan, skel, page_dir):
    """The guide image (frame pixels), the frame, and the torso / legs masks (frame pixels)."""
    scale = plan.get("scale", 0.66)
    j = json.loads(skel.read_text(encoding="utf-8"))
    slots = [s["name"] for s in j["slots"] if s.get("attachment")]
    hide = set(plan.get("hide", []))
    tr = plan.get("trousers")
    rx = lambda names: "^(" + "|".join(re.escape(n) for n in names) + ")$"
    _, B = sr.render(skel, page_dir, scale=scale, with_bounds=True)
    masks = {}
    if not tr:
        guide = sr.render(skel, page_dir, scale=scale, bounds=B, show=rx([n for n in slots if n not in hide]))
        return guide, B, scale, masks
    torso_slot, leg_slots = tr["torso"], tr["legs"]
    ti = slots.index(torso_slot)
    below = sr.render(skel, page_dir, scale=scale, bounds=B, show=rx([n for n in slots[:ti] if n not in hide and n not in leg_slots]))
    torso = sr.render(skel, page_dir, scale=scale, bounds=B, show=rx([torso_slot]))
    above = sr.render(skel, page_dir, scale=scale, bounds=B, show=rx([n for n in slots[ti + 1:] if n not in hide]))
    legs = sr.render(skel, page_dir, scale=scale, bounds=B, show=rx(leg_slots))
    ta, la = np.asarray(torso)[..., 3] > 20, np.asarray(legs)[..., 3] > 20
    rows = np.nonzero(ta.any(1))[0]
    top, bot = rows.min(), rows.max()
    widths = ta.sum(1)
    lo, hi = top + int((bot - top) * 0.25), top + int((bot - top) * 0.6)
    waist = lo + int(np.argmin(widths[lo:hi]))
    knee = tr.get("hem") == "knee"
    calves, start = [], bot + 2
    if knee:
        # knee breeches: the leg pictures begin at the knee (under a skirt), so the breeches end there, overlapping
        # the legs' tops by "overlap" px; everything below is the legs' own (socks, shoes) and moves with the leg bones
        start = int(np.nonzero(la.any(1))[0].min()) + tr.get("overlap", 30)
    for y in range(start, min(start + 60, la.shape[0])):
        r = ranges(la[y])
        if len(r) == 2:
            calves, start = r, y
            break
    if len(calves) != 2:
        raise SystemExit("trousers: could not find two legs " + ("at the knees" if knee else "just below the torso's hem"))
    end = start if knee else bot
    hips = ranges(ta[waist])
    hipL, hipR = hips[0][0], hips[-1][1]
    mid = (hipL + hipR) // 2
    crotch = waist + int((end - waist) * tr.get("seat", 0.30))
    m = tr.get("margin", 8)
    mask = np.zeros_like(ta)
    mask[:waist] = True
    for y in range(waist, end + 1):
        u = (y - waist) / max(1, end - waist)
        for a0, a1, b0, b1 in ((hipL, mid, calves[0][0] - m, calves[0][1] + m), (mid, hipR, calves[1][0] - m, calves[1][1] + m)):
            mask[y, max(0, int(a0 + (b0 - a0) * u)):int(a1 + (b1 - a1) * u)] = True
        if y < crotch:
            mask[y, hipL:hipR] = True
    t = np.asarray(torso).copy()
    t[..., 3] = np.where(mask, t[..., 3], 0)
    if tr.get("cover_shoulders"):   # skin below the neck (bare shoulders, a low neckline) shown as the shirt
        rgb = t[..., :3].astype(np.float32) / 255.0
        r_, g_, b_ = rgb[..., 0], rgb[..., 1], rgb[..., 2]
        mx_, mn_ = rgb.max(-1), rgb.min(-1)
        d_ = mx_ - mn_ + 1e-6
        hue = np.where(mx_ == r_, ((g_ - b_) / d_) % 6, np.where(mx_ == g_, (b_ - r_) / d_ + 2, (r_ - g_) / d_ + 4)) * 60
        skin = (hue >= 8) & (hue <= 40) & (d_ / (mx_ + 1e-6) >= 0.18) & (d_ / (mx_ + 1e-6) <= 0.82) & (mx_ >= 0.35) & (t[..., 3] > 20)
        srows = np.nonzero(skin.any(1))[0]
        if srows.size:
            neck_w = np.median([skin[y].sum() for y in srows[:max(1, len(srows) // 6)]])
            shoulders = next((y for y in srows if skin[y].sum() > neck_w * 1.6), None)
            if shoulders is not None:
                cover = skin.copy()
                cover[:shoulders] = False
                t[cover, :3] = tr["cover_shoulders"]
    lg = np.asarray(legs).copy()
    if not knee:
        lg[:bot - 2, :, 3] = 0
    guide = below.copy()
    guide.alpha_composite(Image.fromarray(lg, "RGBA"))
    guide.alpha_composite(Image.fromarray(t, "RGBA"))
    guide.alpha_composite(above)
    masks[torso_slot] = Image.fromarray(mask.astype(np.uint8) * 255)
    # the waist and the hem in skeleton units (y up), for rebind_breeches; the frame is render()'s (pad 40)
    tr["_waist_y"], tr["_hem_y"] = (B[3] - (y - 40) / scale for y in (waist, end))
    if not knee:   # long trousers: the legs keep only what showed below the hem (with knee breeches they are kept)
        lm = np.zeros_like(ta)
        lm[bot - 2:] = True
        for s in leg_slots:
            masks[s] = Image.fromarray(lm.astype(np.uint8) * 255)
    return guide, B, scale, masks


def make_concept(plan, guide, seed):
    box = guide.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    crop = (max(0, box[0] - 30), max(0, box[1] - 30), min(guide.size[0], box[2] + 30), min(guide.size[1], box[3] + 30))
    g = guide.crop(crop)
    prepared = rp.prepare(g)
    name = rp.upload(prepared, "character_swap_guide.png")
    gen = rp.run(rp.workflow(name, plan["prompt"], plan["negative"], plan.get("denoise", 0.8), seed, plan.get("control", 0.55)))
    concept = gen.resize(prepared.size).resize(g.size, Image.LANCZOS)
    frame = Image.new("RGBA", guide.size, (0, 0, 0, 0))
    frame.paste(concept.convert("RGBA"), crop[:2])
    # the concept's own outline, by background removal: where it shows background, no projected part may stay
    cut = rp.run({"1": {"class_type": "LoadImage", "inputs": {"image": rp.upload(concept.convert("RGB"), "character_swap_concept.png")}},
                  "2": {"class_type": "InspyrenetRembg", "inputs": {"image": ["1", 0], "torchscript_jit": "default"}},
                  "3": {"class_type": "SaveImage", "inputs": {"images": ["2", 0], "filename_prefix": "character_swap_cut"}}}, rgba=True)
    outline = Image.new("L", guide.size, 0)
    outline.paste(cut.getchannel("A").resize(g.size), crop[:2])
    return frame, g, concept, outline


def restyle_parts(plan, skel, page_out, seed):
    """The plan's "restyle_parts" {region: subject}: each part restyled alone, inside its outline (the usual restyle:
    img2img + ControlNet canny, the original alpha kept) -- for parts mostly hidden in the setup pose, which a
    projection cannot paint (sleeves behind the body, closed eyelids never shown standing still)."""
    from spine_page_restyle import footprint, parse_atlas
    parts = plan.get("restyle_parts", {})
    if not parts:
        return
    for page in parse_atlas(skel.with_suffix(".atlas")):
        path = page_out / page["file"]
        stored = Image.open(path).convert("RGBA")
        pma = rp.is_premultiplied(Image.open(skel.parent / page["file"]))
        plain = rp.unpremultiply(stored) if pma else stored
        for r in page["regions"]:
            if r["name"] not in parts:
                continue
            box = footprint(r)
            part = plain.crop(box)
            up = part.transpose(Image.ROTATE_270) if r["rotated"] else part
            name = rp.upload(rp.prepare(up), f"swap_{r['name']}.png".replace(" ", "_"))
            prompt = f"{parts[r['name']]}, {plan.get('part_style', 'glossy casual slot game character art, clean edges')}"
            gen = rp.run(rp.workflow(name, prompt, plan["negative"], plan.get("part_denoise", 0.6), seed, plan.get("part_control", 0.6)))
            new = rp.finish(gen, up)
            new = new.transpose(Image.ROTATE_90) if r["rotated"] else new
            stored.paste(rp.premultiply(new) if pma else new, box[:2])
            print("restyled", r["name"])
        stored.save(path)


def cloth_parts(plan, skel, page_out):
    """The plan's "cloth_parts" {"parts": [...], "colour": [r, g, b], "blur": px}: those parts repainted as plain
    cloth of that colour, keeping only their soft shading (the luminance, blurred by `blur` px so lace, trims and the
    line between skin and cloth go). For parts that are wholly to become cloth -- a puffed sleeve with a bare shoulder
    above it becomes a shirt sleeve. The outline is kept exactly."""
    from PIL import ImageFilter
    from spine_page_restyle import footprint, parse_atlas
    cp = plan.get("cloth_parts")
    if not cp:
        return
    colour = np.array(cp["colour"], dtype=np.float32)
    for page in parse_atlas(skel.with_suffix(".atlas")):
        path = page_out / page["file"]
        stored = Image.open(path).convert("RGBA")
        pma = rp.is_premultiplied(Image.open(skel.parent / page["file"]))
        plain = rp.unpremultiply(stored) if pma else stored
        for r in page["regions"]:
            if r["name"] not in cp["parts"]:
                continue
            box = footprint(r)
            part = plain.crop(box)
            a = np.asarray(part, dtype=np.float32)
            inside = a[..., 3] > 127
            lum = a[..., :3] @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
            # blur only inside the outline (the outside would darken the edge): blur(lum*in) / blur(in)
            k = ImageFilter.GaussianBlur(cp.get("blur", 4))
            num = np.asarray(Image.fromarray(np.where(inside, lum, 0).astype(np.uint8)).filter(k), dtype=np.float32)
            den = np.asarray(Image.fromarray((inside * 255).astype(np.uint8)).filter(k), dtype=np.float32) / 255
            shade = num / np.maximum(den, 1e-3)
            ref = np.percentile(shade[inside], 85) if inside.any() else 200.0
            a[..., :3] = colour * np.clip(shade / max(ref, 1), 0.5, 1.05)[..., None]
            a[..., :3] = np.where(a[..., 3:] > 0, a[..., :3], 0)
            new = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA")
            stored.paste(rp.premultiply(new) if pma else new, box[:2])
            print("cloth:", r["name"])
        stored.save(path)


def still_animations(plan, skel, page_out):
    """The plan's "still" {"bones": regex, "deform": [slots]}: those bones' timelines and those slots' mesh deforms
    taken out of every animation, in the output's copies of the skeletons beside `skel` (they share its page). For
    motion the new character must not have: a skirt's swing and flare, which would swing trousers painted into the
    skirt part, and a leg kicked out from under the skirt, which would leave the breeches behind. Read from the
    source each time, so a rerun gives the same files."""
    st = plan.get("still")
    if not st:
        return
    bone_rx = re.compile(st.get("bones", "^$"))
    deform = set(st.get("deform", []))
    for src in sorted(skel.parent.glob("*.json")):
        j = json.loads(src.read_text(encoding="utf-8"))
        if "animations" not in j or "bones" not in j:
            continue
        gone = 0
        for A in j["animations"].values():
            for b in [b for b in A.get("bones", {}) if bone_rx.search(b)]:
                del A["bones"][b]
                gone += 1
            for sk in A.get("deform", {}).values():
                for sl in [sl for sl in sk if sl in deform]:
                    del sk[sl]
                    gone += 1
        (page_out / src.name).write_text(json.dumps(j, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
        print(f"still: {src.name}, {gone} timelines taken out")


def _affine_inv(m, p):
    a, b, c, d, x, y = m
    det = a * d - b * c
    px, py = p[0] - x, p[1] - y
    return ((d * px - b * py) / det, (-c * px + a * py) / det)


def _mesh(att, bw, bones):
    """A weighted mesh's vertices: [(world x, world y, {bone index: weight})] in the setup pose."""
    v, out, k = att["vertices"], [], 0
    for _ in range(len(att["uvs"]) // 2):
        n, wx, wy, ws = int(v[k]), 0.0, 0.0, {}
        k += 1
        for _ in range(n):
            bi, x, y, w = int(v[k]), v[k + 1], v[k + 2], v[k + 3]
            a, b, c, d, tx, ty = bw[bones[bi]]
            wx += w * (a * x + b * y + tx)
            wy += w * (c * x + d * y + ty)
            ws[bi] = ws.get(bi, 0.0) + w
            k += 4
        out.append((wx, wy, ws))
    return out


def rebind_breeches(plan, page_out):
    """Knee breeches painted into a skirt part move with the skirt's bones while the legs below move with the leg
    bones, so in a sway or a kick the hem slides off the knees. In the output's skeletons, each vertex of the torso
    mesh below the waist takes, more and more toward the hem, the bone weights of the nearest leg vertex (fully at
    the hem): the breeches' hem then follows the knees in every pose. Positions in the setup pose stay exactly the
    same (each bone's local coordinates recomputed). The torso's deform timelines are dropped: their numbers belong
    to the old weights."""
    tr = plan.get("trousers") or {}
    if tr.get("hem") != "knee" or "_waist_y" not in tr:
        return
    torso_slot, leg_slots = tr["torso"], tr["legs"]
    y0, y1 = tr["_waist_y"], tr["_hem_y"]
    for path in sorted(page_out.glob("*.json")):
        j = json.loads(path.read_text(encoding="utf-8"))
        skins = j.get("skins")
        if not skins or "bones" not in j:
            continue
        atts = (skins[0] if isinstance(skins, list) else {"attachments": skins.get("default", {})})["attachments"]
        if torso_slot not in atts or not all(l in atts for l in leg_slots):
            continue
        bones = [b["name"] for b in j["bones"]]
        bw = sr.bone_world(j["bones"])
        ta = next(iter(atts[torso_slot].values()))
        legv = [p for l in leg_slots for a in atts[l].values() if a.get("type") == "mesh" for p in _mesh(a, bw, bones)]
        lx = np.array([p[0] for p in legv])
        ly = np.array([p[1] for p in legv])
        out, moved = [], 0
        for wx, wy, ws in _mesh(ta, bw, bones):
            u = min(1.0, max(0.0, (y0 - wy) / max(1e-6, y0 - y1)))
            u = u * u * (3 - 2 * u)
            if u > 0:
                near = legv[int(np.argmin((lx - wx) ** 2 + (ly - wy) ** 2))][2]
                new = {b: w * (1 - u) for b, w in ws.items()}
                for b, w in near.items():
                    new[b] = new.get(b, 0.0) + w * u
                new = {b: w for b, w in new.items() if w >= 0.005}
                tot = sum(new.values())
                ws = {b: w / tot for b, w in new.items()}
                moved += 1
            out.append(len(ws))
            for b, w in ws.items():
                x, y = _affine_inv(bw[bones[b]], (wx, wy))
                out += [b, round(x, 2), round(y, 2), round(w, 4)]
        ta["vertices"] = out
        for A in j.get("animations", {}).values():
            for sk in A.get("deform", {}).values():
                sk.pop(torso_slot, None)
        path.write_text(json.dumps(j, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
        print(f"breeches: {path.name}, {moved} torso vertices bound toward the legs")


def frames_sheet(plan, skel, page_out, out_png):
    j = json.loads(skel.read_text(encoding="utf-8"))
    shots = [(None, 0.0)] + [(a, t) for a, t in plan.get("frames", [])]
    ims = []
    for an, t in shots:
        o, B = sr.render(skel, skel.parent, scale=0.5, anim=an, t=t, hide=plan.get("frames_hide"), with_bounds=True)
        for sk, page in ((skel, skel.parent), (page_out / skel.name, page_out)):   # after: the output's skeleton
            im = sr.render(sk, page, skel.with_suffix(".atlas"), scale=0.5, anim=an, t=t, hide=plan.get("frames_hide"), bounds=B)
            ims.append(im.crop(o.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()))
    h = 640
    ims = [i.resize((max(1, int(i.size[0] * h / i.size[1])), h)) for i in ims]
    sheet = Image.new("RGBA", (sum(i.size[0] for i in ims) + 10 * len(ims), h), (64, 64, 64, 255))
    x = 0
    for i in ims:
        sheet.alpha_composite(i, (x, 0))
        x += i.size[0] + 10
    sheet.convert("RGB").save(out_png)
    del j


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plan")
    ap.add_argument("--seed", type=int)
    ap.add_argument("--concept-only", action="store_true", help="stop after the concept (to try seeds)")
    ap.add_argument("--frames", action="store_true", help="also render setup + animation frames, before | after")
    a = ap.parse_args()
    plan = load_plan(a.plan)
    seed = a.seed if a.seed is not None else plan.get("seed", 22)
    skel = plan["source"] / plan["skeleton"]
    page_dir = skel.parent
    work = plan["output"] / "_character_swap"
    work.mkdir(parents=True, exist_ok=True)

    guide, B, scale, masks = build_guide(plan, skel, page_dir)
    frame, g, concept, outline = make_concept(plan, guide, seed)
    g.save(work / "guide.png")
    concept.save(work / f"concept_{seed}.png")
    print(f"guide and concept -> {work}")
    if a.concept_only:
        return
    if not (plan["output"] / plan["skeleton"]).exists():   # the whole game, once: the base the changed page goes into
        shutil.copytree(plan["source"], plan["output"], dirs_exist_ok=True)
    page_out = plan["output"] / Path(plan["skeleton"]).parent
    # every projected part: its cut (torso, legs) and the concept's outline (slightly grown, so edges stay soft)
    from PIL import ImageFilter
    grown = outline.point(lambda v: 255 if v > 60 else 0).filter(ImageFilter.MaxFilter(5))
    j = json.loads(skel.read_text(encoding="utf-8"))
    for sl in j["slots"]:
        n = sl["name"]
        if n in plan.get("keep", []) or n in plan.get("hide", []) or not sl.get("attachment"):
            continue
        masks[n] = Image.fromarray(np.minimum(np.asarray(masks[n].convert("L")), np.asarray(grown))) if n in masks else grown
    outline.save(work / "outline.png")
    changed = sp.project(skel, page_dir, page_out, frame, scale, B, masks=masks, hidden=set(plan.get("hide", [])),
                         keep=set(plan.get("keep", [])), trust=outline.filter(ImageFilter.MinFilter(3)))
    cloth_parts(plan, skel, page_out)
    print(f"{len(changed)} parts changed on the page(s) in {page_out}")
    restyle_parts(plan, skel, page_out, seed)
    still_animations(plan, skel, page_out)
    rebind_breeches(plan, page_out)
    if a.frames:
        frames_sheet(plan, skel, page_out, work / "frames.png")
        print(f"frames -> {work / 'frames.png'}")


if __name__ == "__main__":
    main()
