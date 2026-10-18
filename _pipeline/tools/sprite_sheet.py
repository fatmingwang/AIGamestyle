"""A sheet of 32 x 32 sprites at 4x, optionally after the game's own: python sprite_sheet.py <out.png> <dir> [--with-game]
[--big]   (--big also shows each sprite's 1024 source beside it)"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw

GAME = Path(__file__).resolve().parents[3] / "assets" / "media" / "sprites"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("dir")
    ap.add_argument("--with-game", action="store_true")
    ap.add_argument("--big", action="store_true")
    ap.add_argument("--cols", type=int, default=10)
    a = ap.parse_args()
    items = []
    if a.with_game:
        items += [(p.stem, p, None) for p in sorted(GAME.glob("*.png"))]
    for p in sorted(Path(a.dir).glob("*.png")):
        if p.stem.endswith("_big") or p.stem.startswith("_"):
            continue
        big = p.with_name(p.stem + "_big.png")
        items.append((p.stem, p, big if a.big and big.exists() else None))
    cell = 128
    per = 2 if a.big else 1
    cols = a.cols
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell * per, rows * (cell + 16)), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, (name, p, big) in enumerate(items):
        x, y = (i % cols) * cell * per, (i // cols) * (cell + 16)
        tile = Image.new("RGBA", (cell, cell), (60, 60, 66, 255))
        tile.alpha_composite(Image.open(p).convert("RGBA").resize((cell, cell), Image.NEAREST))
        sheet.paste(tile.convert("RGB"), (x, y))
        if big:
            b = Image.new("RGBA", (cell, cell), (60, 60, 66, 255))
            bi = Image.open(big).convert("RGBA")
            bi.thumbnail((cell, cell))
            b.alpha_composite(bi)
            sheet.paste(b.convert("RGB"), (x + cell, y))
        d.text((x + 3, y + cell + 2), name[:22], fill=(255, 255, 255))
    sheet.save(a.out)
    print(a.out, len(items))


if __name__ == "__main__":
    main()
