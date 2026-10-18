"""Check a restyled game can replace the original as is: python check_restyle.py <original> <restyled>

Every file of the original must be in the restyled folder; pictures with the same pixel size and the same alpha
channel (so silhouettes, masks and Spine parts line up); every other file (.atlas, .json, .pac, .prefab ...)
byte-identical. Prints what differs; exit 1 when anything does.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

PICTURE = {".png", ".jpg", ".jpeg", ".webp"}


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    bad, pictures, others, changed = [], 0, 0, 0
    for f in sorted(p for p in src.rglob("*") if p.is_file()):
        rel = f.relative_to(src)
        g = out / rel
        if not g.exists():
            bad.append(f"missing: {rel}")
            continue
        if f.suffix.lower() in PICTURE:
            pictures += 1
            a, b = Image.open(f), Image.open(g)
            if a.size != b.size:
                bad.append(f"size {a.size} -> {b.size}: {rel}")
                continue
            aa = np.asarray(a.convert("RGBA"))[..., 3]
            ba = np.asarray(b.convert("RGBA"))[..., 3]
            if not np.array_equal(aa, ba):
                bad.append(f"alpha differs ({int((aa != ba).sum())} px): {rel}")
            if f.read_bytes() != g.read_bytes():
                changed += 1
        else:
            others += 1
            if f.read_bytes() != g.read_bytes():
                bad.append(f"changed: {rel}")
    print(f"{pictures} pictures ({changed} restyled), {others} other files")
    for b in bad:
        print("  " + b)
    print("OK: drop-in replacement" if not bad else f"{len(bad)} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
