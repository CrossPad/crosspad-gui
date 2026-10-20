#!/usr/bin/env python3
"""Regenerate the Montserrat fonts the UI uses, with European diacritics.

LVGL ships Montserrat as ASCII plus the FontAwesome symbols, so a kit called
"ŚWIĘTE SŁOWA" draws as empty boxes. These fonts carry the same glyphs LVGL's
do — same TTF, same size, same bpp, same symbol list, so every LV_SYMBOL_*
keeps working — plus Latin-1 Supplement and Latin Extended-A. They replace
LVGL's own copies under the same symbol names (cmake/crosspad_gui_fonts.cmake
drops the built-in sources from the build), which is why no call site changes
and no app has to pick a "font with Polish letters".

RANGE, and why it has holes: a font's line_height is the tallest glyph over the
shortest, so one letter with a high accent raises every line in the UI. Å å Ů ů
(ring above) and Ģ ķ Ļ ņ Ŗ (cedilla below) alone cost 2 more pixels per line at
every size than the rest of Latin Extended-A put together, so they are left
out; everything else in both blocks is in. The cost of what remains is one
pixel per line (14 px: 16 -> 17) and ~50 kB of flash.

Usage: python3 tools/fonts/generate.py [--sizes 10 12 14 16 18 24] [--out DIR]
Needs node; lv_font_conv is fetched with npx on first run.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_OUT = HERE.parent.parent / "src" / "fonts"
DEFAULT_SIZES = (10, 12, 14, 16, 18, 24)
LV_FONT_CONV = "lv_font_conv@1.5.3"

TEXT_FONT = "Montserrat-Medium.ttf"
SYMBOL_FONT = "FontAwesome5-Solid+Brands+Regular.woff"

# ASCII, then Latin-1 Supplement without Å (0xC5) and å (0xE5), then Latin
# Extended-A without Ģ (0x122), Ķ ķ (0x136-0x137), Ļ ļ (0x13B-0x13C),
# Ņ ņ (0x145-0x146), Ŗ ŗ (0x156-0x157) and Ů ů (0x16E-0x16F), then the
# punctuation a name or a label actually uses.
TEXT_RANGE = ",".join([
    "0x20-0x7F",
    "0x2022",
    "0xA0-0xC4", "0xC6-0xE4", "0xE6-0xFF",
    "0x100-0x121", "0x123-0x135", "0x138-0x13A", "0x13D-0x144",
    "0x147-0x155", "0x158-0x16D", "0x170-0x17F",
    "0x2013-0x2014", "0x2018-0x2019", "0x201C-0x201E", "0x20AC",
])

# Exactly the list LVGL's own built_in_font_gen.py uses: every LV_SYMBOL_*.
SYMBOL_RANGE = (
    "61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,"
    "61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,"
    "61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,"
    "61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,"
    "61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,"
    "63650"
)


def convert(size: int, out: Path, runner: list[str]) -> Path:
    name = f"lv_font_montserrat_{size}"
    dst = out / f"{name}.c"
    cmd = runner + [
        "--no-compress", "--no-prefilter",
        "--bpp", "4", "--size", str(size),
        "--font", str(HERE / TEXT_FONT), "-r", TEXT_RANGE,
        "--font", str(HERE / SYMBOL_FONT), "-r", SYMBOL_RANGE,
        "--format", "lvgl", "--lv-include", "lvgl.h",
        "--lv-font-name", name,
        "-o", str(dst), "--force-fast-kern-format",
    ]
    subprocess.run(cmd, check=True)
    return dst


def line_metrics(path: Path) -> tuple[int, int]:
    text = path.read_text()
    height = int(text.split(".line_height = ")[1].split(",")[0])
    base = int(text.split(".base_line = ")[1].split(",")[0])
    return height, base


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--sizes", type=int, nargs="+", default=list(DEFAULT_SIZES))
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    if shutil.which("npx") is None:
        print("npx not found — install node (nvm use 22)", file=sys.stderr)
        return 2
    runner = ["npx", "--yes", LV_FONT_CONV]

    args.out.mkdir(parents=True, exist_ok=True)
    for size in args.sizes:
        dst = convert(size, args.out, runner)
        height, base = line_metrics(dst)
        print(f"{dst.name}: line_height={height} base_line={base} "
              f"({dst.stat().st_size // 1024} kB of C)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
