#!/usr/bin/env python3
"""Font inventory gate: every non-ASCII UI character in main/ must be listed in
assets/fonts/cipher_chars.txt (the lv_font_conv input). Fails when someone adds
Chinese text without regenerating fonts via tools/gen_cipher_font.py."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MAIN_DIR = REPO / "main"
CHARS_FILE = REPO / "assets" / "fonts" / "cipher_chars.txt"
STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')
EXTRA_CHARS = "★"


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
    if not CHARS_FILE.exists():
        print("FAIL: assets/fonts/cipher_chars.txt missing")
        return 1
    listed = {ch for ch in CHARS_FILE.read_text(encoding="utf-8").strip()}
    used = extract_chars()
    missing = sorted(used - listed)
    if missing:
        print("FAIL: characters used in main/ but absent from cipher_chars.txt:")
        print("  " + " ".join(f"{c}(U+{ord(c):04X})" for c in missing))
        print("rerun: python tools/gen_cipher_font.py")
        return 1
    stale = sorted(listed - used)
    if stale:
        # Not fatal: extras are allowed, but report for visibility.
        print("note: listed but unused chars: " + " ".join(stale))
    print(f"font inventory covers {len(used)} used chars: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
