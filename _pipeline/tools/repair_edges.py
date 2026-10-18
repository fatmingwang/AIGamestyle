"""Repair the soft edges of an already restyled game, without running the AI again (Doc/Reskin_Game_Art.md).

    python repair_edges.py ..\\style_profiles\\zeus_10085.json [--only symbols/H3]

Restyles made before restyle_pilot.fix_edges existed can have a jagged, whitish fringe on their soft edges (hanging
flags, a wheel's rim). This applies fix_edges to every picture the plan's report says was restyled: whole static
pictures, and each restyled part of a Spine page inside its own footprint (so neighbouring parts never mix). Kept
pictures and parts are not touched, nor is any alpha. Running it twice gives the same result. The previews are
rebuilt afterwards.
"""
import argparse
import json
from pathlib import Path

from PIL import Image

from restyle_game import load_plan, previews, save
from restyle_pilot import fix_edges
from spine_page_restyle import footprint, parse_atlas


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
        save(fix_edges(Image.open(out / f).convert("RGBA"), original), out / f)
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
        page_name = Path(page_rel).name
        for page in parse_atlas(src / info["atlas"]):
            if page["file"] != page_name:
                continue
            for r in page["regions"]:
                if r["name"] not in styled:
                    continue
                box = footprint(r)
                after.paste(fix_edges(after.crop(box), before.crop(box)), box[:2])
        save(after, out / page_rel)
        n += 1
        print("spine ", page_rel, f"({len(styled)} parts)")
    previews(plan, report)
    print(f"{n} files repaired")


if __name__ == "__main__":
    main()
