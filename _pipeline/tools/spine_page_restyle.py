"""Restyle a Spine atlas page part by part, so the .atlas and the skeleton .json stay valid unchanged.

    python spine_page_restyle.py <page.atlas> <out_dir> --prompt "..." [--reference hero.png] [--denoise 0.6]
           [--skip shadow glow light] [--min 40]

A Spine page (.png) is many parts packed together; its .atlas says where each part is (xy, size, rotate) and the
skeleton (.json) moves the parts by name. So the page is never restyled as one picture -- that would bleed parts into
each other and move their edges. Instead, for every part:

  1. cut it out (a rotated part is turned upright first: Spine 3.8 stores it turned 90 degrees counter-clockwise);
  2. restyle it alone (restyle_pilot: SDXL img2img + ControlNet canny), at its own size, with its own alpha kept;
     --reference makes every part follow one look (IP-Adapter): the object's restyled static symbol;
  3. put it back exactly where it was (turned back), so xy / size / orig / offset in the .atlas still match.

Parts named with a --skip word (shadows, glows, light rays: drawn additively or as soft masks) and parts smaller than
--min pixels are copied unchanged. The result is <out_dir>/<page>.png plus the unchanged .atlas (and .json when next
to it), ready to drop in place of the originals.
"""
import argparse
import shutil
import time
from pathlib import Path

from PIL import Image

import restyle_pilot as rp


def parse_atlas(path: Path):
    """Spine / libGDX atlas: pages and their regions. Handles the 3.x fields (xy, size, orig, offset, rotate) and the
    4.x ones (bounds, offsets, rotate: 90)."""
    pages, cur_page, cur = [], None, None
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip()
        if not line:
            cur_page = None
            cur = None
            continue
        if cur_page is None:   # a page's file name starts a block
            cur_page = {"file": line.strip(), "regions": []}
            pages.append(cur_page)
            continue
        if ":" in line and (raw.startswith(" ") or raw.startswith("\t") or cur is None):
            k, v = [t.strip() for t in line.split(":", 1)]
            target = cur if (cur is not None and (raw.startswith(" ") or raw.startswith("\t"))) else cur_page
            target[k] = v
            continue
        cur = {"name": line.strip()}
        cur_page["regions"].append(cur)
    for p in pages:
        for r in p["regions"]:
            if "bounds" in r:
                x, y, w, h = [int(t) for t in r["bounds"].split(",")]
            else:
                x, y = [int(t) for t in r["xy"].split(",")]
                w, h = [int(t) for t in r["size"].split(",")]
            rot = r.get("rotate", "false").strip()
            r["box"] = (x, y, w, h)
            r["rotated"] = rot in ("true", "90")
    return pages


def footprint(r):
    x, y, w, h = r["box"]
    return (x, y, x + h, y + w) if r["rotated"] else (x, y, x + w, y + h)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("atlas")
    ap.add_argument("out_dir")
    ap.add_argument("--prompt", required=True)
    ap.add_argument("--negative", default=rp.NEGATIVE)
    ap.add_argument("--reference", help="a picture every part's look follows (IP-Adapter)")
    ap.add_argument("--ref-weight", type=float, default=0.6)
    ap.add_argument("--denoise", type=float, default=0.6)
    ap.add_argument("--control", type=float, default=0.6)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--skip", nargs="*", default=["shadow", "glow", "light", "shine", "fx", "flash", "spark", "blur", "mask"])
    ap.add_argument("--min", type=int, default=40, help="parts with a side below this are copied unchanged")
    a = ap.parse_args()

    atlas = Path(a.atlas)
    out_dir = Path(a.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    ref = rp.upload(Image.open(a.reference).convert("RGB"), "ref_" + Path(a.reference).name) if a.reference else None
    for page in parse_atlas(atlas):
        src = Image.open(atlas.parent / page["file"]).convert("RGBA")
        out = src.copy()
        done = skipped = 0
        for r in page["regions"]:
            box = footprint(r)
            part = src.crop(box)
            upright = part.transpose(Image.ROTATE_270) if r["rotated"] else part
            small = min(upright.size) < a.min
            if small or any(w in r["name"].lower() for w in a.skip) or upright.getchannel("A").getextrema()[1] == 0:
                skipped += 1
                continue
            t = time.time()
            name = rp.upload(rp.prepare(upright), f"part_{atlas.stem}_{r['name']}.png".replace("/", "_"))
            gen = rp.run(rp.workflow(name, a.prompt, a.negative, a.denoise, a.seed, a.control, reference=ref, ref_weight=a.ref_weight))
            styled = rp.finish(gen, upright)
            back = styled.transpose(Image.ROTATE_90) if r["rotated"] else styled
            out.paste(back, box[:2])   # its exact footprint: the pixels around it are untouched
            done += 1
            print(f"  {r['name']:<24} {upright.size[0]}x{upright.size[1]}{' (rotated)' if r['rotated'] else ''}  {time.time() - t:.1f}s")
        out.save(out_dir / page["file"])
        print(f"{page['file']}: {done} part(s) restyled, {skipped} kept")
    shutil.copy2(atlas, out_dir / atlas.name)
    skel = atlas.with_suffix(".json")
    if skel.exists():
        shutil.copy2(skel, out_dir / skel.name)


if __name__ == "__main__":
    main()
