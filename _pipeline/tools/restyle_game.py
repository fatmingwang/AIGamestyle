"""Restyle a whole game's art with ComfyUI, from a plan (style_profiles/<plan>.json): Doc/Reskin_Game_Art.md
(setting up a PC: Doc/Reskin_Setup_New_PC.md).

    python restyle_game.py ../style_profiles/christmas_10085.json            # everything not done yet
    python restyle_game.py <plan> --dry-run                                  # what each file / part would get
    python restyle_game.py <plan> --only symbols/H                           # files whose path contains this
    python restyle_game.py <plan> --force                                    # redo what is already done

1. The whole game folder is copied to the plan's output (every file the plan does not restyle is there unchanged:
   .atlas, .json, .pac, .prefab, and the pictures marked "keep").
2. Static pictures (the plan's "static" list) are restyled one by one: same size, same alpha (restyle_pilot).
3. Spine pages are restyled part by part (spine_page_restyle's method), each part as its name says:
     keep    effects (glows, light rays, sparks, shadows ...) and flipbook frames (name_00, name_01 ...): unchanged,
             since a repaint breaks additive blending, and frames repainted one by one would flicker;
     text    parts with letters or numbers (logo/WIN, wild, x10, A ...): gently, the letters kept;
     object  everything else: fully, following the object's restyled static symbol (IP-Adapter) so the parts match it.
   The .atlas and the skeleton .json are never changed: every part keeps its place and size on the page.

Each file is done once (resume by running again; --force redoes). <output>/_restyle_report.json says what was done to
each file and part, and _previews/ has before / after sheets.
"""
import argparse
import fnmatch
import json
import re
import shutil
import time
from pathlib import Path

from PIL import Image, ImageDraw

import restyle_pilot as rp
from spine_page_restyle import footprint, parse_atlas

PICTURE = (".png", ".jpg", ".jpeg", ".webp")


def load_plan(path: Path):
    plan = json.loads(path.read_text(encoding="utf-8"))
    base = path.resolve().parent.parent.parent   # style_profiles -> _pipeline -> AIGamestyle: the plan's paths start there
    plan["source"] = (base / plan["source"]).resolve()
    plan["output"] = (base / plan["output"]).resolve()
    return plan


def static_items(plan):
    """The static list with globs expanded; the first entry naming a file wins."""
    seen, out = set(), []
    for item in plan["static"]:
        files = [item["file"]] if "file" in item else sorted(
            p.relative_to(plan["source"]).as_posix() for p in plan["source"].glob(item["glob"]))
        for f in files:
            if f in seen:
                continue
            seen.add(f)
            out.append(dict(item, file=f))
    return out


def part_kind(name: str, sp) -> str:
    low = name.lower()
    if re.search(sp["sequence_pattern"], name) or any(w in low for w in sp["keep_words"]):
        return "keep"
    if name in sp["text_names"] or low in [n.lower() for n in sp["text_names"]] or any(name.startswith(p) for p in sp["text_prefixes"]):
        return "text"
    return "object"


def prompt_for(plan, kind, subject):
    k = plan["kinds"][kind]
    return ", ".join(t for t in (subject, k.get("prompt_extra", ""), plan["style"]) if t)


def restyle_picture(plan, src_img, kind, subject, seed, reference=None, denoise=None, upload_name="x.png", control=None,
                    ref_weight=0.55):
    k = plan["kinds"][kind]
    name = rp.upload(rp.prepare(src_img), upload_name)
    gen = rp.run(rp.workflow(name, prompt_for(plan, kind, subject), plan["negative"], denoise or k["denoise"], seed,
                             control or k["control"], reference=reference, ref_weight=ref_weight))
    return rp.finish(gen, src_img)


def save(img: Image.Image, path: Path):
    if path.suffix.lower() in (".jpg", ".jpeg"):
        img.convert("RGB").save(path, quality=93)
    else:
        img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plan")
    ap.add_argument("--only", default="")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--seed", type=int, default=1225)
    a = ap.parse_args()
    plan = load_plan(Path(a.plan))
    src, out = plan["source"], plan["output"]
    sp = plan["spine"]
    report_path = out / "_restyle_report.json"
    report = json.loads(report_path.read_text(encoding="utf-8")) if report_path.exists() else {"static": {}, "spine": {}}

    if a.dry_run:
        for it in static_items(plan):
            print(f"static  {it['kind']:<6} {it['file']}")
        for pg in sp["pages"]:
            for page in parse_atlas(src / pg["atlas"]):
                for r in page["regions"]:
                    k = "keep" if r["name"] in pg.get("keep_parts", []) else part_kind(r["name"], sp)
                    if k != "keep" and min(r["box"][2:]) < sp["min_size"]:
                        k = "keep(small)"
                    print(f"spine   {k:<11} {pg['atlas']} :: {r['name']}")
        return

    # 1. the whole game, copied (the base every restyle is written over)
    if not out.exists():
        shutil.copytree(src, out)
        print(f"copied {src} -> {out}")

    # 2. static pictures
    for it in static_items(plan):
        f = it["file"]
        if a.only and a.only not in f:
            continue
        if it["kind"] == "keep" or (f in report["static"] and not a.force):
            continue
        t = time.time()
        img = Image.open(src / f).convert("RGBA")
        styled = restyle_picture(plan, img, it["kind"], it.get("subject", ""), a.seed, upload_name="s_" + f.replace("/", "_"))
        save(styled, out / f)
        report["static"][f] = {"kind": it["kind"], "seconds": round(time.time() - t, 1)}
        report_path.write_text(json.dumps(report, indent=1), encoding="utf-8")
        print(f"static {it['kind']:<6} {f}  {time.time() - t:.1f}s")

    # 3. Spine pages, part by part
    done_pages = set()
    for pg in sp["pages"]:
        atlas = src / pg["atlas"]
        if a.only and a.only not in pg["atlas"]:
            continue
        ref = None
        if pg.get("reference") and (out / pg["reference"]).exists():
            ref = rp.upload(Image.open(out / pg["reference"]).convert("RGB"), "ref_" + Path(pg["reference"]).name)
        for page in parse_atlas(atlas):
            page_rel = (Path(pg["atlas"]).parent / page["file"]).as_posix()
            if page_rel in done_pages or (page_rel in report["spine"] and not a.force):
                continue
            done_pages.add(page_rel)
            before = Image.open(src / page_rel).convert("RGBA")
            after = before.copy()
            parts = {}
            t0 = time.time()
            for r in page["regions"]:
                kind = "keep" if r["name"] in pg.get("keep_parts", []) else part_kind(r["name"], sp)
                box = footprint(r)
                part = before.crop(box)
                upright = part.transpose(Image.ROTATE_270) if r["rotated"] else part
                if kind == "keep" or min(upright.size) < sp["min_size"] or upright.getchannel("A").getextrema()[1] == 0:
                    parts[r["name"]] = "keep"
                    continue
                # the part alone: its own name first, so a plate stays a plate (the reference -- the whole object -- only
                # guides colours and finish, weakly, or it paints the whole object into every part)
                part_name = re.sub(r"[_/]+", " ", r["name"]).strip()
                subject = f"only the {part_name}, {pg['subject']}"
                # a part that is a base under other parts (a plate, a cookie's plain body) says exactly what it is, with
                # no reference: otherwise the whole object gets painted onto it and shows twice in the animation
                po = pg.get("parts", {}).get(r["name"], {})
                if po.get("subject"):
                    subject = po["subject"]
                part_ref = None if po.get("reference") is False else ref
                styled = restyle_picture(plan, upright, kind, subject, a.seed, reference=part_ref if kind == "object" else None,
                                         denoise=po.get("denoise") or (pg.get("denoise") if kind == "object" else None),
                                         control=pg.get("control") if kind == "object" else None,
                                         ref_weight=sp.get("reference_weight", 0.35),
                                         upload_name=f"p_{Path(pg['atlas']).stem}_{r['name']}.png".replace("/", "_"))
                after.paste(styled.transpose(Image.ROTATE_90) if r["rotated"] else styled, box[:2])
                parts[r["name"]] = kind
            save(after, out / page_rel)
            report["spine"][page_rel] = {"atlas": pg["atlas"], "parts": parts, "seconds": round(time.time() - t0, 1)}
            report_path.write_text(json.dumps(report, indent=1), encoding="utf-8")
            n = sum(1 for v in parts.values() if v != "keep")
            print(f"spine  {page_rel}: {n} restyled, {len(parts) - n} kept  {time.time() - t0:.1f}s")

    previews(plan, report)


def previews(plan, report):
    """_previews/: before | after pairs of every restyled file (pages included), several per sheet."""
    src, out = plan["source"], plan["output"]
    pv = out / "_previews"
    pv.mkdir(exist_ok=True)
    files = list(report["static"].keys()) + list(report["spine"].keys())
    cell, per = 300, 8
    for i in range(0, len(files), per):
        chunk = files[i:i + per]
        sheet = Image.new("RGB", (cell * 4, ((len(chunk) + 1) // 2) * (cell + 18)), (45, 45, 45))
        d = ImageDraw.Draw(sheet)
        for j, f in enumerate(chunk):
            for k, root in enumerate((src, out)):
                im = Image.open(root / f).convert("RGBA")
                im.thumbnail((cell, cell))
                x, y = ((j % 2) * 2 + k) * cell, (j // 2) * (cell + 18)
                tile = Image.new("RGBA", (cell, cell), (45, 45, 45, 255))
                tile.alpha_composite(im, ((cell - im.size[0]) // 2, (cell - im.size[1]) // 2))
                sheet.paste(tile.convert("RGB"), (x, y))
                d.text((x + 4, y + cell + 3), ("after " if k else "before ") + Path(f).name, fill=(255, 255, 255))
        sheet.save(pv / f"sheet_{i // per + 1:02d}.png")
    print(f"previews: {pv}")


if __name__ == "__main__":
    main()
