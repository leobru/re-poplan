#!/usr/bin/env python3
"""Extract POPLAN's static dictionary records from a disbesm6 listing."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


WORD_RE = re.compile(
    r"^\s*([0-7]{3,5})(?::)?\s+([0-7]{16})\b.*?конд\s+д'([^']*)'"
)
RAW_RE = re.compile(r"^\s*([0-7]{3,5})(?::)?\s+([0-7]{16})\b")
FUNCTION_TAG_MASK = 0o7700000000000000
FUNCTION_TAG = 0o6600000000000000


@dataclass(frozen=True)
class Entry:
    address: int
    name: str
    name_word: int
    class_word: int
    property_word: int
    value_word: int

    @property
    def is_function(self) -> bool:
        return self.value_word & FUNCTION_TAG_MASK == FUNCTION_TAG

    @property
    def environment(self) -> int:
        return (self.value_word >> 24) & 0o77777

    @property
    def entry(self) -> int:
        return self.value_word & 0o77777


def read_image(path: Path) -> dict[int, int]:
    data = path.read_bytes()
    if len(data) % 6 != 0:
        raise ValueError(f"{path}: size is not a multiple of one 48-bit word")
    return {
        index: int.from_bytes(data[offset : offset + 6], "big")
        for index, offset in enumerate(range(0, len(data), 6))
    }


def parse_listing(path: Path, image: Path | None = None) -> list[Entry]:
    raw_words: dict[int, int] = {}
    names: dict[int, tuple[str, int]] = {}

    for line in path.read_text(encoding="utf-8").splitlines():
        raw_match = RAW_RE.match(line)
        if raw_match:
            raw_words[int(raw_match.group(1), 8)] = int(raw_match.group(2), 8)
        name_match = WORD_RE.match(line)
        if name_match:
            address = int(name_match.group(1), 8)
            names[address] = (name_match.group(3).rstrip(), int(name_match.group(2), 8))

    if image is not None:
        raw_words = read_image(image)

    entries = []
    for address, (name, name_word) in sorted(names.items()):
        record = [raw_words.get(address + offset) for offset in range(1, 4)]
        if any(word is None for word in record):
            continue
        class_word, property_word, value_word = record
        entries.append(
            Entry(
                address,
                name,
                name_word,
                int(class_word),
                int(property_word),
                int(value_word),
            )
        )
    return entries


def render(entries: list[Entry], source: Path) -> str:
    lines = [
        f"# Static Dictionary From `{source.name}`",
        "",
        "| Record | Name fragment | Class | Property | Value | Env | Entry |",
        "|---:|---|---:|---:|---:|---:|---:|",
    ]
    for item in entries:
        if item.is_function:
            env = f"`{item.environment:05o}`"
            entry = f"`{item.entry:05o}`"
        else:
            env = ""
            entry = ""
        lines.append(
            f"| `{item.address:05o}` | `{item.name}` | "
            f"`{item.class_word:016o}` | `{item.property_word:016o}` | "
            f"`{item.value_word:016o}` | {env} | {entry} |"
        )
    lines.append("")
    lines.append(
        "Names are the fragments decoded by `disbesm6`; the raw words remain "
        "the contract where the decoded fragment is truncated."
    )
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("listing", type=Path)
    parser.add_argument(
        "--image",
        type=Path,
        help="raw 6-byte-per-word image; avoids data/instruction ambiguity",
    )
    args = parser.parse_args()
    print(render(parse_listing(args.listing, args.image), args.listing), end="")


if __name__ == "__main__":
    main()
