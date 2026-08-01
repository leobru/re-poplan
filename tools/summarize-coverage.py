#!/usr/bin/env python3
"""Summarize and combine dispak word/halfword coverage maps."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


LINE_RE = re.compile(r"^(?P<address>[0-7]{5}): (?P<flags>LR|L | R|--)$")
STATIC_LIMIT = 0o36000


@dataclass(frozen=True)
class Coverage:
    path: Path
    words: frozenset[int]
    left: frozenset[int]
    right: frozenset[int]

    @property
    def static_words(self) -> int:
        return sum(address < STATIC_LIMIT for address in self.words)


def read_coverage(path: Path) -> Coverage:
    words: set[int] = set()
    left: set[int] = set()
    right: set[int] = set()

    for line_number, line in enumerate(
        path.read_text(encoding="ascii").splitlines(), start=1
    ):
        match = LINE_RE.fullmatch(line)
        if not match:
            raise ValueError(f"{path}:{line_number}: invalid coverage line")
        address = int(match.group("address"), 8)
        flags = match.group("flags")
        if "L" in flags:
            left.add(address)
            words.add(address)
        if "R" in flags:
            right.add(address)
            words.add(address)

    return Coverage(path, frozenset(words), frozenset(left), frozenset(right))


def render(coverages: list[Coverage]) -> str:
    occurrences: dict[int, int] = {}
    for coverage in coverages:
        for address in coverage.words:
            occurrences[address] = occurrences.get(address, 0) + 1

    union = set(occurrences)
    static_union = sum(address < STATIC_LIMIT for address in union)
    lines = [
        "# POPLAN Coverage Corpus",
        "",
        "| Map | Words | Static words | Left halves | Right halves | Exclusive |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for coverage in coverages:
        exclusive = sum(occurrences[address] == 1 for address in coverage.words)
        lines.append(
            f"| `{coverage.path.name}` | {len(coverage.words)} | "
            f"{coverage.static_words} | {len(coverage.left)} | "
            f"{len(coverage.right)} | {exclusive} |"
        )

    lines.extend(
        [
            "",
            f"- Union word addresses: {len(union)}",
            f"- Union static-image addresses below `36000`: {static_union}",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("coverage", nargs="+", type=Path)
    args = parser.parse_args()
    print(render([read_coverage(path) for path in args.coverage]), end="")


if __name__ == "__main__":
    main()
