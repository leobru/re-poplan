#!/usr/bin/env python3
"""Convert selected ASCII KOI-7 glyphs to Unicode Cyrillic."""

from __future__ import annotations

import argparse
from pathlib import Path


ASCII_KOI7 = "`abcdefghijklmnopqrstuvwxyz{|}~"
KOI7_CYRILLIC = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧ"
TRANSLATION = str.maketrans(ASCII_KOI7, KOI7_CYRILLIC)


def convert(text: str) -> str:
    return text.translate(TRANSLATION)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()

    source = args.source.read_text(encoding="utf-8")
    args.destination.write_text(convert(source), encoding="utf-8")


if __name__ == "__main__":
    main()
