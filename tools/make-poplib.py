#!/usr/bin/env python3
"""Build a compact flat POPLIB source-library image."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


WORDS_PER_ZONE = 0o2000
WORD_BYTES = 6
ZONE_BYTES = WORDS_PER_ZONE * WORD_BYTES
FIRST_SOURCE_ZONE = 0o7
OVERLAY_ZONE = 0o6
HEADER_VALUE = int("1303100000000000", 8)
MAGIC = b"RPN57\0"
VERSION = 2
ASCII_KOI7 = "`abcdefghijklmnopqrstuvwxyz{|}~"
KOI7_CYRILLIC = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧ"
CYRILLIC_KOI7 = dict(zip(KOI7_CYRILLIC, ASCII_KOI7))

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


@dataclass
class Entry:
    user: str
    name: str
    path: Path
    input_size: int
    payload: bytes = b""
    zone: int = 0


def encode_source(source: bytes) -> bytes:
    """Encode the values returned by CHARIN after its GOST decoder."""
    encoded = bytearray()
    for character in source.decode("utf-8"):
        if character == "\r":
            continue
        if character == "\n":
            # E71 terminates each terminal buffer with 0377; the original
            # 21274 decoder maps that marker to POPLAN's line character 0012.
            encoded.append(0o12)
            continue
        if character in CYRILLIC_KOI7:
            # zone1224.pop2 stores these historical KOI-7 positions as
            # Unicode Cyrillic. Restore the original internal byte.
            encoded.append(ord(CYRILLIC_KOI7[character]))
            continue
        if "a" <= character <= "z":
            character = character.upper()
        code = ord(character)
        # POPLAN's internal character values are KOI-7 for the ASCII subset.
        # Damaged non-syntax glyphs in the recovered source become spaces,
        # matching terminal conversion of an unknown character.
        encoded.append(code if 0o40 <= code <= 0o137 else 0o40)
    return bytes(encoded)


def name_word(name: str) -> int:
    name = name.upper()
    if not name:
        raise ValueError("identifier must not be empty")
    try:
        # POPLAN passes a library identifier as one six-character word.
        # Longer source identifiers have already been truncated at 14705.
        codes = [LATIN_GOST[character] for character in name[:6]]
    except KeyError as error:
        raise ValueError(
            f"identifier contains a non-Latin letter: {name!r}"
        ) from error
    codes.extend([0o17] * (6 - len(codes)))
    return int.from_bytes(bytes(codes), "big")


def parse_entry(values: list[str]) -> Entry:
    user, name, path_text, length_text = values
    path = Path(path_text)
    data_size = path.stat().st_size
    input_size = data_size if length_text == "-" else int(length_text, 0)
    if input_size < 0 or input_size > data_size:
        raise ValueError(
            f"source length {input_size} is outside {path} ({data_size} bytes)"
        )
    name_word(user)
    name_word(name)
    payload = encode_source(path.read_bytes()[:input_size])
    if len(payload) > 2 * ZONE_BYTES:
        raise ValueError(
            f"source {user}/{name} needs more than the two overlay buffer zones"
        )
    return Entry(user, name, path, input_size, payload)


def short_instruction(reg: int, opcode: int, address: int = 0) -> int:
    if not 0 <= reg <= 0o17 or not 0 <= opcode <= 0o77:
        raise ValueError("invalid short instruction")
    address &= 0o77777
    if address <= 0o7777:
        extension = 0
    elif address >= 0o70000:
        extension = 1 << 18
    else:
        raise ValueError(f"short instruction cannot address {address:05o}")
    return (reg << 20) | (opcode << 12) | extension | (address & 0o7777)


def long_instruction(reg: int, opcode: int, address: int = 0) -> int:
    if not 0 <= reg <= 0o17 or opcode & ~0o370:
        raise ValueError("invalid long instruction")
    return ((reg << 20) | (1 << 19) | ((opcode & 0o370) << 12)
            | (address & 0o77777))


class OverlayAssembler:
    def __init__(self, start: int) -> None:
        self.start = start
        self.halves: list[int] = []
        self.labels: dict[str, int] = {}
        self.fixups: list[tuple[int, int, int, str]] = []

    def address(self) -> int:
        return self.start + len(self.halves) // 2

    def label(self, name: str) -> None:
        if len(self.halves) % 2:
            # BESM branches select words, never right halves.  VTM r0 is a
            # side-effect-free alignment half because architectural r0 is
            # always observed as zero.
            self.long(0, 0o240, 0)
        self.labels[name] = self.address()

    def short(self, reg: int, opcode: int, address: int = 0) -> None:
        self.halves.append(short_instruction(reg, opcode, address))

    def long(self, reg: int, opcode: int, address: int = 0) -> None:
        self.halves.append(long_instruction(reg, opcode, address))

    def branch(self, opcode: int, label: str, reg: int = 0) -> None:
        self.fixups.append((len(self.halves), reg, opcode, label))
        self.halves.append(0)

    def finish(self) -> list[int]:
        if len(self.halves) % 2:
            self.long(0, 0o300, self.address() + 1)
        for index, reg, opcode, label in self.fixups:
            self.halves[index] = long_instruction(
                reg, opcode, self.labels[label])
        return [
            (self.halves[index] << 24) | self.halves[index + 1]
            for index in range(0, len(self.halves), 2)
        ]


def build_overlay(entries: list[Entry]) -> list[int]:
    words = [0] * WORDS_PER_ZONE

    # Fields consumed by POPLAN's resident object loader. They establish the
    # object signature, nominal origin, relocation descriptor and entry vector.
    fields = {
        0o6: 0o32,
        0o7: 0o34,
        0o10: 0o101,
        0o11: 0o113,
        0o12: int("0002704112630442", 8),
        0o13: 0o36,
        0o14: 0o227,
        0o43: int("0000105000001222", 8),
        0o103: int("2000350000000000", 8),
        0o106: int("0000126300001263", 8),
        0o110: int("0000000000570000", 8),
        0o111: int("0000000000077735", 8),
        0o112: int("0000057400017674", 8),
        0o113: int("0360741703607417", 8),
    }
    for address, value in fields.items():
        words[address] = value

    entry_address = 0o75113
    supplier_address = 0o75200
    descriptor_state = 0o75240
    remaining_state = 0o75241
    descriptor_initial = 0o75242
    minus_one = 0o75243
    tag = 0o75244
    halt = 0o75245
    function_descriptor = 0o75246
    negative_zero = 0o75247
    constant_base = 0o75260

    assembler = OverlayAssembler(entry_address)
    assembler.long(1, 0o240, 0o15117)
    for index, entry in enumerate(entries):
        next_label = f"next_{index}"
        setup_label = f"setup_{index}"
        user_constant = constant_base + index * 5
        file_constant = user_constant + 1
        assembler.short(1, 0o10, 0o77077)
        assembler.short(0, 0o12, user_constant)
        assembler.branch(0o270, next_label)
        assembler.short(1, 0o10, 0o77100)
        assembler.short(0, 0o12, file_constant)
        assembler.branch(0o260, setup_label)
        assembler.label(next_label)

    # An unmatched emulator-native record retains CHARIN as a safe supplier.
    assembler.short(0, 0o10, 0o1513)
    assembler.branch(0o300, "return_supplier")

    for index, entry in enumerate(entries):
        assembler.label(f"setup_{index}")
        control_base = constant_base + index * 5 + 2
        zones = (len(entry.payload) + ZONE_BYTES - 1) // ZONE_BYTES
        for zone_index in range(zones):
            assembler.short(0, 0o70, control_base + zone_index)
            assembler.long(0, 0o240, 0)
        assembler.short(0, 0o10, descriptor_initial)
        assembler.short(0, 0o0, descriptor_state)
        assembler.short(0, 0o10, control_base + 2)
        assembler.short(0, 0o0, remaining_state)
        assembler.short(0, 0o10, function_descriptor)
        assembler.branch(0o300, "return_supplier")

    assembler.label("return_supplier")
    assembler.long(1, 0o240, 0o15117)
    assembler.long(0o17, 0o250, -0o306)
    assembler.short(1, 0o0, 0o77105)
    assembler.long(0, 0o300, 0o14677)

    entry_words = assembler.finish()
    entry_offset = entry_address - 0o74000
    if entry_offset + len(entry_words) > supplier_address - 0o74000:
        raise ValueError("too many directory entries for the zone-six overlay")
    words[entry_offset:entry_offset + len(entry_words)] = entry_words

    supplier = OverlayAssembler(supplier_address)
    supplier.short(0, 0o10, remaining_state)
    supplier.short(0, 0o13, minus_one)
    supplier.short(0, 0o0, remaining_state)
    supplier.short(0, 0o12, negative_zero)
    supplier.branch(0o260, "source_end")
    supplier.long(0o16, 0o240, descriptor_state)
    supplier.long(0o15, 0o310, 0o21431)
    supplier.long(0, 0o240, 0)
    supplier.short(0, 0o12, tag)
    supplier.long(0o15, 0o240, 0o3235)
    supplier.long(0, 0o300, 0o3275)
    supplier.label("source_end")
    supplier.short(0, 0o10, halt)
    supplier.long(0o15, 0o240, 0o3235)
    supplier.long(0, 0o300, 0o3275)
    supplier_words = supplier.finish()
    supplier_offset = supplier_address - 0o74000
    words[supplier_offset:supplier_offset + len(supplier_words)] = supplier_words

    words[descriptor_initial - 0o74000] = int("6400000000070000", 8)
    # ARX uses end-around carry, so all ones would be additive zero.
    words[minus_one - 0o74000] = (1 << 48) - 2
    words[tag - 0o74000] = int("6400000000000000", 8)
    words[halt - 0o74000] = int("6400000000000136", 8)
    words[function_descriptor - 0o74000] = (
        int("6600000000000000", 8) | supplier_address
    )
    words[negative_zero - 0o74000] = (1 << 48) - 1
    for index, entry in enumerate(entries):
        base = constant_base + index * 5 - 0o74000
        words[base] = name_word(entry.user)
        words[base + 1] = name_word(entry.name)
        zone_count = (len(entry.payload) + ZONE_BYTES - 1) // ZONE_BYTES
        for zone_index in range(zone_count):
            page = 0o34 + zone_index
            words[base + 2 + zone_index] = (
                (1 << 39) | (page << 30) | (0o57 << 12)
                | (entry.zone + zone_index)
            )
        words[base + 4] = len(entry.payload) + 1
    return words


def bytes_to_words(data: bytes) -> list[int]:
    padded = data + bytes((-len(data)) % WORD_BYTES)
    words = [
        int.from_bytes(padded[index:index + WORD_BYTES], "big")
        for index in range(0, len(padded), WORD_BYTES)
    ]
    words.extend([0] * (WORDS_PER_ZONE - len(words)))
    return words


def flat_zone(words: list[int]) -> bytes:
    if len(words) != WORDS_PER_ZONE:
        raise ValueError("logical zone must contain exactly 02000 words")
    output = bytearray()
    for value in words:
        output += value.to_bytes(WORD_BYTES, "big")
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--entry", action="append", nargs=4, required=True,
        metavar=("USER", "FILE", "PATH", "LENGTH"),
        help="add a source; LENGTH is input bytes or '-' for the whole file",
    )
    args = parser.parse_args()

    entries = [parse_entry(values) for values in args.entry]
    next_zone = FIRST_SOURCE_ZONE
    for entry in entries:
        entry.zone = next_zone
        next_zone += max(1, (len(entry.payload) + ZONE_BYTES - 1) // ZONE_BYTES)

    directory_word_count = 4 + 4 * len(entries)
    if directory_word_count > WORDS_PER_ZONE:
        raise ValueError("directory does not fit in zone 0")
    directory = [HEADER_VALUE | directory_word_count,
                 int.from_bytes(MAGIC, "big"), VERSION, len(entries)]
    for entry in entries:
        directory.extend((name_word(entry.user), name_word(entry.name),
                          entry.zone, len(entry.payload)))
    directory.extend([0] * (WORDS_PER_ZONE - len(directory)))

    logical_zones: dict[int, list[int]] = {
        0: directory,
        OVERLAY_ZONE: build_overlay(entries),
    }
    for entry in entries:
        for offset in range(0, len(entry.payload), ZONE_BYTES):
            logical_zones[entry.zone + offset // ZONE_BYTES] = bytes_to_words(
                entry.payload[offset:offset + ZONE_BYTES]
            )
        if not entry.payload:
            logical_zones[entry.zone] = [0] * WORDS_PER_ZONE

    final_zone = max(logical_zones)
    image = bytearray()
    for zone in range(final_zone + 1):
        image += flat_zone(logical_zones.get(zone, [0] * WORDS_PER_ZONE))
    args.output.write_bytes(image)


if __name__ == "__main__":
    main()
