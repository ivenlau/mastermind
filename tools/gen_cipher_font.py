#!/usr/bin/env python3
"""Generate the application's LVGL Chinese subset fonts.

The character inventory is extracted from string literals in main/ (plus a few
explicit extras), written to assets/fonts/cipher_chars.txt, and converted with
lv_font_conv (pinned 1.5.3). Run this after any UI text change, then run
tests/test_font_inventory.py to prove the inventory covers the sources.

Usage:
    python tools/gen_cipher_font.py [--fonts-dir DIR] [--check]

--fonts-dir: directory containing NotoSansSC-Regular.otf and NotoSansSC-Bold.otf
(defaults to <repo>/../fonttools/fonts, where the workspace keeps downloaded
sources). Source font: Noto Sans SC, SIL Open Font License 1.1.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAIN_DIR = REPO / "main"
CHARS_FILE = REPO / "assets" / "fonts" / "cipher_chars.txt"
OUT_DIR = REPO / "assets" / "fonts"
DEFAULT_FONTS_DIR = REPO.parent / "fonttools" / "fonts"
FONTTOOLS_DIR = REPO.parent / "fonttools"

# C string literals (handles \" escapes); inventory = printable non-ASCII chars.
STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

# Always include: rainbow marker / star glyph used on the game board.
EXTRA_CHARS = "★"

# name, source OTF, pixel size. 12/14/16 Regular for dense text, 20/28 Bold for
# titles and big numerals.
FONT_JOBS = [
    ("cipher_font_12", "NotoSansSC-Regular.otf", 12),
    ("cipher_font_14", "NotoSansSC-Regular.otf", 14),
    ("cipher_font_16", "NotoSansSC-Regular.otf", 16),
    ("cipher_font_20", "NotoSansSC-Bold.otf", 20),
    ("cipher_font_28", "NotoSansSC-Bold.otf", 28),
]


def extract_chars() -> set[str]:
    chars: set[str] = set()
    for src in sorted(MAIN_DIR.glob("*.c")) + sorted(MAIN_DIR.glob("*.h")):
        text = src.read_text(encoding="utf-8")
        for m in STRING_RE.finditer(text):
            for ch in m.group(1):
                if ord(ch) > 127 and ch.isprintable():
                    chars.add(ch)
    chars.update(EXTRA_CHARS)
    return chars


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts-dir", type=Path, default=DEFAULT_FONTS_DIR)
    ap.add_argument("--check", action="store_true",
                    help="only verify the inventory is up to date")
    args = ap.parse_args()

    chars = extract_chars()
    inventory = "".join(sorted(chars))
    old = CHARS_FILE.read_text(encoding="utf-8").strip() if CHARS_FILE.exists() else ""
    if old != inventory.strip():
        if args.check:
            print("FAIL: cipher_chars.txt is stale; rerun tools/gen_cipher_font.py")
            return 1
        CHARS_FILE.parent.mkdir(parents=True, exist_ok=True)
        CHARS_FILE.write_text(inventory + "\n", encoding="utf-8")
        print(f"wrote {CHARS_FILE} ({len(chars)} chars)")
    else:
        print(f"inventory up to date ({len(chars)} chars)")

    if args.check:
        return 0

    # 直接用 node 调 lv_font_conv.js:npx 会经 cmd 二次解析,带空格的路径
    # (本工作区路径含空格)会被截断。
    node = shutil.which("node") or "node"
    conv_js = FONTTOOLS_DIR / "node_modules" / "lv_font_conv" / "lv_font_conv.js"
    if not conv_js.exists():
        print(f"FAIL: missing {conv_js}; run npm install lv_font_conv in {FONTTOOLS_DIR}")
        return 1
    for name, otf, size in FONT_JOBS:
        font_path = args.fonts_dir / otf
        if not font_path.exists():
            print(f"FAIL: missing source font {font_path}")
            return 1
        out = OUT_DIR / f"{name}.c"
        cmd = [
            node, str(conv_js),
            "--font", str(font_path),
            "--range", "0x20-0x7E",
            "--symbols", inventory,
            "--size", str(size),
            "--bpp", "4",
            "--format", "lvgl",
            "--no-compress",
            "--lv-font-name", name,
            "--lv-include", "lvgl.h",
            "-o", str(out),
        ]
        print(" ".join(cmd))
        r = subprocess.run(cmd, cwd=FONTTOOLS_DIR, capture_output=True, text=True)
        if r.stdout.strip():
            print(r.stdout.strip())
        if r.stderr.strip():
            # lv_font_conv reports glyphs missing from the source font here.
            print(r.stderr.strip(), file=sys.stderr)
        if r.returncode != 0:
            print(f"FAIL: lv_font_conv exited {r.returncode} for {name}")
            return r.returncode
    print("all fonts generated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
