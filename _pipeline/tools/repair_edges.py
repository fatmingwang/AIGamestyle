"""Repair the soft edges of an already restyled game, without running the AI again (Doc/Reskin_Game_Art.md).

    python repair_edges.py ..\\style_profiles\\zeus_10085.json [--only symbols/H3]

Restyles made before restyle_pilot.fix_edges existed can have a jagged, whitish fringe on their soft edges (hanging
flags, a wheel's rim); parts the plan marks "keep_skin" get the original's skin back (restyle_pilot.keep_skin); and on a premultiplied page
the parts are written premultiplied again (outputs made before that fix held plain colours there, which the game drew
as glowing edges). This applies fix_edges to every picture the plan's report says was restyled: whole static
pictures, and each restyled part of a Spine page inside its own footprint (so neighbouring parts never mix). Kept
pictures and parts are not touched, nor is any alpha. Running it twice gives the same result. The previews are
rebuilt afterwards.
"""
import argparse

import numpy as np
import json
from pathlib import Path

from PIL import Image

from restyle_game import load_plan, previews, save
from restyle_pilot import fix_edges, is_premultiplied, keep_skin, premultiply, unpremultiply
from spine_page_restyle import footprint, parse_atlas


def plain(img):
    """A restyled part in plain colours: outputs made before the premultiplied-alpha fix hold plain colours on a
    premultiplied page (colour above alpha); later ones hold premultiplied colours. Either way, plain comes back."""
    a = np.asarray(img, dtype=np.int32)
    straight = ((a[..., :3].max(-1) > a[..., 3] + 2) & (a[..., 3] > 0)).any()
    out = img if straight else unpremultiply(img)
    b = np.asarray(out).copy()
    b[b[..., 3] == 0, :3] = 0
    return Image.fromarray(b, "RGBA")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plan")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    plan = load_plan(Path(a.plan))
    src, out = plan["source"], plan["output"]
    report = json.loads((out / "_restyle_report.json").read_text(encoding="utf-8"))
    n = 0
    for f, info in report["static"].items():
        if a.only and a.only not in f or info.get("kind") == "keep":
            continue
        original = Image.open(src / f).convert("RGBA")
        if original.getchannel("A").getextrema()[0] == 255:
            continue   # no soft edge (a .jpg background): nothing to repair, and no needless re-encode
        pma = is_premultiplied(original)
        cur = Image.open(out / f).convert("RGBA")
        if pma:
            original, cur = unpremultiply(original), plain(cur)
        fixed = fix_edges(cur, original)
        save(premultiply(fixed) if pma else fixed, out / f)
        n += 1
        print("static", f)
    for page_rel, info in report["spine"].items():
        if a.only and a.only not in page_rel and a.only not in info["atlas"]:
            continue
        styled = [k for k, v in info["parts"].items() if v != "keep"]
        if not styled:
            continue
        before = Image.open(src / page_rel).convert("RGBA")
        after = Image.open(out / page_rel).convert("RGBA")
        pma = is_premultiplied(before)   # the source's setting: the repaired parts are written premultiplied
        if pma:
            before = unpremultiply(before)
        page_name = Path(page_rel).name
        pg = next((x for x in plan["spine"]["pages"] if x["atlas"] == info["atlas"] or info["atlas"] in x.get("also", [])), {})
        skin_parts = {k for k, v in pg.get("parts", {}).items() if v.get("keep_skin")}
        for page in parse_atlas(src / info["atlas"]):
            if page["file"] != page_name:
                continue
            for r in page["regions"]:
                if r["name"] not in styled:
                    continue
                box = footprint(r)
                cur = after.crop(box)
                fixed = fix_edges(plain(cur) if pma else cur, before.crop(box))
                if r["name"] in skin_parts:   # the plan's "keep_skin": the original's bare skin back
                    fixed = keep_skin(fixed, before.crop(box))
                after.paste(premultiply(fixed) if pma else fixed, box[:2])
        save(after, out / page_rel)
        n += 1
        print("spine ", page_rel, f"({len(styled)} parts)")
    previews(plan, report)
    print(f"{n} files repaired")


if __name__ == "__main__":
    main()
