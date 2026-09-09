#!/usr/bin/env python3
"""Show RPN57 compatibility catalogs in a flat POPLIB image."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


WORDS_PER_ZONE = 0o2000
WORD_BYTES = 6
ZONE_BYTES = WORDS_PER_ZONE * WORD_BYTES
WORD_MASK = (1 << 48) - 1
HEADER_VALUE = int("1303100000000000", 8)
HEADER_MASK = WORD_MASK ^ 0o3777
MAGIC_WORD = int.from_bytes(b"RPN57\0", "big")
DIRECTORY_WORDS = 4
RECORD_WORDS = 4

LATIN_GOST = dict(
    zip(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
        (
            0o40, 0o42, 0o61, 0o77, 0o45, 0o100, 0o101, 0o55, 0o102,
            0o103, 0o52, 0o104, 0o54, 0o105, 0o56, 0o60, 0o106, 0o107,
            0o110, 0o62, 0o111, 0o112, 0o113, 0o65, 0o63, 0o114,
        ),
    )
)
GOST_LATIN = {code: character for character, code in LATIN_GOST.items()}


class CatalogError(ValueError):
    pass


@dataclass(frozen=True)
class Record:
    user: str
    name: str
    first_zone: int
    byte_count: int

    @property
    def zone_count(self) -> int:
        return max(1, (self.byte_count + ZONE_BYTES - 1) // ZONE_BYTES)

    @property
    def last_zone(self) -> int:
        return self.first_zone + self.zone_count - 1


@dataclass(frozen=True)
class Catalog:
    zone: int
    version: int
    records: tuple[Record, ...]


def decode_identifier(word: int) -> str:
    characters: list[str] = []
    for code in word.to_bytes(WORD_BYTES, "big"):
        if code == 0o17:
            characters.append(" ")
        else:
            characters.append(GOST_LATIN.get(code, f"\\{code:03o}"))
    return "".join(characters).rstrip()


def read_flat_words(path: Path) -> tuple[list[int], int]:
    image = path.read_bytes()
    if len(image) == 0 or len(image) % ZONE_BYTES != 0:
        raise CatalogError(
            f"{path}: size {len(image)} is not a whole number of "
            f"{ZONE_BYTES}-byte flat zones"
        )
    words = [
        int.from_bytes(image[offset:offset + WORD_BYTES], "big")
        for offset in range(0, len(image), WORD_BYTES)
    ]
    return words, len(image)


def find_catalogs(words: list[int]) -> list[Catalog]:
    catalogs: list[Catalog] = []
    zone_count = len(words) // WORDS_PER_ZONE
    for zone in range(zone_count):
        base = zone * WORDS_PER_ZONE
        header = words[base]
        if ((header & HEADER_MASK) != HEADER_VALUE
                or words[base + 1] != MAGIC_WORD):
            continue

        directory_size = header & 0o3777
        version = words[base + 2]
        record_count = words[base + 3]
        expected_size = DIRECTORY_WORDS + RECORD_WORDS * record_count
        if record_count > (WORDS_PER_ZONE - DIRECTORY_WORDS) // RECORD_WORDS:
            raise CatalogError(
                f"catalog at zone {zone:04o}: record count is too large"
            )
        if directory_size != expected_size:
            raise CatalogError(
                f"catalog at zone {zone:04o}: header says {directory_size} "
                f"words, records require {expected_size}"
            )

        records: list[Record] = []
        for index in range(record_count):
            offset = base + DIRECTORY_WORDS + index * RECORD_WORDS
            records.append(Record(
                decode_identifier(words[offset]),
                decode_identifier(words[offset + 1]),
                words[offset + 2],
                words[offset + 3],
            ))
        catalogs.append(Catalog(zone, version, tuple(records)))
    return catalogs


def render(path: Path, image_size: int, catalogs: list[Catalog]) -> str:
    lines = [
        f"{path}: {image_size // ZONE_BYTES} logical zones, "
        f"{image_size} bytes"
    ]
    for catalog in catalogs:
        suffix = "entry" if len(catalog.records) == 1 else "entries"
        lines.append(
            f"catalog zone {catalog.zone:04o}: RPN57 version "
            f"{catalog.version}, {len(catalog.records)} {suffix}"
        )
        lines.append("USER    FILE    FIRST LAST  BYTES")
        for record in catalog.records:
            lines.append(
                f"{record.user:<7} {record.name:<7} "
                f"{record.first_zone:04o}  {record.last_zone:04o}  "
                f"{record.byte_count}"
            )
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "image", nargs="?", type=Path, default=Path("poplib.bin"),
        help="flat six-byte-per-word image (default: poplib.bin)",
    )
    args = parser.parse_args()
    try:
        words, image_size = read_flat_words(args.image)
        catalogs = find_catalogs(words)
        if not catalogs:
            raise CatalogError(f"{args.image}: no RPN57 catalogs found")
    except (OSError, CatalogError) as error:
        parser.error(str(error))
    print(render(args.image, image_size, catalogs), end="")


if __name__ == "__main__":
    main()
