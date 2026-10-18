"""Before / after preview of a restyled Spine page: both pages, and each group of parts that share one original canvas
(the same "orig" size) put back together at their offsets -- e.g. a hat's brim, crown and feather as the whole hat.

    python spine_page_preview.py <original.atlas> <restyled_dir> <out.png> [--order shadow brim crown feather]
"""
import argparse
from collections import OrderedDict
from pathlib import Path

from PIL import Image, ImageDraw

from spine_page_restyle import footprint, parse_atlas


def assemble(page_img, regions, order):
    """Parts of one original canvas at their offsets (Spine 3.8: offset y counts from the bottom)."""
    ow, oh = [int(t) for t in regions[0]["orig"].split(",")]
    canvas = Image.new("RGBA", (ow, oh), (0, 0, 0, 0))
    rank = {n: i for i, n in enumerate(order)}
    for r in sorted(regions, key=lambda r: rank.get(r["name"], len(order))):
        part = page_img.crop(footprint(r))
        if r["rotated"]:
            part = part.transpose(Image.ROTATE_270)
        ox, oy = [int(t) for t in r.get("offset", "0,0").split(",")]
        canvas.alpha_composite(part, (ox, oh - oy - part.size[1]))
    return canvas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("atlas")
    ap.add_argument("restyled_dir")
    ap.add_argument("out")
    ap.add_argument("--order", nargs="*", default=[])
    a = ap.parse_args()
    atlas = Path(a.atlas)
    tiles = []
    for page in parse_atlas(atlas):
        before = Image.open(atlas.parent / page["file"]).convert("RGBA")
        after = Image.open(Path(a.restyled_dir) / page["file"]).convert("RGBA")
        tiles += [("page before", before), ("page after", after)]
        groups = OrderedDict()
        for r in page["regions"]:
            if "orig" in r:
                groups.setdefault(r["orig"], []).append(r)
        for orig, regs in groups.items():
            if len(regs) > 1:
                tiles += [(f"parts {orig} before", assemble(before, regs, a.order)), (f"after", assemble(after, regs, a.order))]
    cell = 420
    sheet = Image.new("RGBA", (cell * 2, (len(tiles) + 1) // 2 * (cell + 20)), (50, 50, 50, 255))
    d = ImageDraw.Draw(sheet)
    for i, (label, im) in enumerate(tiles):
        im = im.copy()
        im.thumbnail((cell, cell))
        x, y = (i % 2) * cell, (i // 2) * (cell + 20)
        sheet.alpha_composite(im, (x, y))
        d.text((x + 4, y + cell + 4), label, fill=(255, 255, 255, 255))
    sheet.convert("RGB").save(a.out)
    print(a.out)


if __name__ == "__main__":
    main()
