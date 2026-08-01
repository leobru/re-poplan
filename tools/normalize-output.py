#!/usr/bin/env python3
"""Remove volatile POPLAN session framing from program output."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


BANNER_RE = re.compile(r"^ПОПЛАН 2\.1\s+ВРЕМЯ\s")
EXIT_RE = re.compile(r"^ВЫХОД\s")
PROMPT_RE = re.compile(r"^:+$")


def normalize(text: str) -> str:
    lines = text.removeprefix("\ufeff").splitlines()
    kept: list[str] = []

    for line in lines:
        line = line.removeprefix("\ufeff").rstrip()
        if (
            BANNER_RE.match(line)
            or EXIT_RE.match(line)
            or PROMPT_RE.fullmatch(line)
            or line == "ВСЕГО ВАМ ДОБРОГО"
        ):
            continue
        if line.startswith(":"):
            line = line[1:]
        kept.append(line)

    while kept and not kept[0]:
        kept.pop(0)
    while kept and not kept[-1]:
        kept.pop()
    return "\n".join(kept) + ("\n" if kept else "")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", nargs="?", type=Path)
    args = parser.parse_args()

    if args.input:
        text = args.input.read_text(encoding="utf-8-sig")
    else:
        text = sys.stdin.read()
    sys.stdout.write(normalize(text))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
