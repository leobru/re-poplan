from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "build" / "poplan.bin"

DICTIONARY_CLASS = 0o6500000000000000
PROPERTY_TAG_MASK = 0o7770000000000000
SYNTAX_PROPERTY = 0o6400000000000000

# Syntax words in Burstall and Popplestone, POP-2 Reference Manual,
# printed pages 212 and 217-245. POPLAN stores six character name words.
REFERENCE_SYNTAX = {
    "AND": ("AND", 0o2314),
    "CANCEL": ("CANCEL", 0o2320),
    "CLOSE": ("CLOSE", 0o2324),
    "COMMENT": ("COMMEN", 0o2460),
    "ELSE": ("ELSE", 0o2330),
    "ELSEIF": ("ELSEIF", 0o2334),
    "END": ("END", 0o2340),
    "EXIT": ("EXIT", 0o2350),
    "FUNCTION": ("FUNCTI", 0o2354),
    "GOON": ("GOON", 0o2360),
    "GOTO": ("GOTO", 0o2364),
    "IF": ("IF", 0o2370),
    "LAMBDA": ("LAMBDA", 0o2374),
    "MACRO": ("MACRO", 0o2404),
    "NONOP": ("NONOP", 0o2414),
    "OPERATION": ("OPERAT", 0o2420),
    "OR": ("OR", 0o2424),
    "RETURN": ("RETURN", 0o2430),
    "THEN": ("THEN", 0o2444),
    "VARS": ("VARS", 0o2450),
}

POPLAN_EXTENSIONS = {
    "ENDSEC": 0o2344,
    "LOOPIF": 0o2400,
    "NONMAC": 0o2410,
    "SECTIO": 0o2434,
    "SWITCH": 0o2440,
}


def image_words() -> list[int]:
    data = IMAGE.read_bytes()
    if len(data) % 6:
        raise AssertionError("POPLAN image is not an integral number of words")
    return [
        int.from_bytes(data[offset : offset + 6], "big")
        for offset in range(0, len(data), 6)
    ]


def dictionary_records(words: list[int]) -> dict[str, tuple[int, int]]:
    records: dict[str, tuple[int, int]] = {}
    for address in range(0o1460, 0o2740, 4):
        if words[address + 1] != DICTIONARY_CLASS:
            continue
        raw_name = words[address].to_bytes(6, "big").rstrip(b"\0")
        try:
            name = raw_name.decode("ascii")
        except UnicodeDecodeError:
            continue
        records[name] = (address, words[address + 2])
    return records


class ReferenceLanguageKeywordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = dictionary_records(image_words())

    def assert_syntax_record(self, name: str, address: int):
        self.assertIn(name, self.records)
        actual_address, property_word = self.records[name]
        self.assertEqual(actual_address, address)
        self.assertEqual(
            property_word & PROPERTY_TAG_MASK,
            SYNTAX_PROPERTY,
        )

    def test_supported_reference_syntax_words_are_dictionary_records(self):
        for reference_name, (image_name, address) in REFERENCE_SYNTAX.items():
            with self.subTest(reference_name=reference_name):
                self.assert_syntax_record(image_name, address)

    def test_reference_routine_synonym_is_not_resident(self):
        self.assertNotIn("ROUTIN", self.records)

    def test_language_program_contains_every_supported_syntax_word(self):
        source = (ROOT / "tests" / "inputs" / "language-coverage.pop2").read_text(
            encoding="utf-8"
        )
        for reference_name in REFERENCE_SYNTAX:
            with self.subTest(reference_name=reference_name):
                self.assertRegex(source, rf"\b{re.escape(reference_name)}\b")
        self.assertNotRegex(source, r"\bROUTINE\b")

    def test_poplan_syntax_extensions_are_dictionary_records(self):
        for image_name, address in POPLAN_EXTENSIONS.items():
            with self.subTest(image_name=image_name):
                self.assert_syntax_record(image_name, address)


if __name__ == "__main__":
    unittest.main()
