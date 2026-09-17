from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
IMAGE = ROOT / "build" / "poplan.bin"

DICTIONARY_CLASS = 0o6500000000000000

# Alphabetic standard functions and routines declared by Burstall and
# Popplestone, POP-2 Reference Manual, printed pages 214-245, which POPLAN
# recognizes under the same spelling (after its six-character truncation).
REFERENCE_DIRECT = {
    "ISCOMPND": ("ISCOMP", 0o01730),
    "ISINTEGER": ("ISINTE", 0o01740),
    "ISREAL": ("ISREAL", 0o01754),
    "LOGAND": ("LOGAND", 0o01774),
    "LOGOR": ("LOGOR", 0o02004),
    "LOGSHIFT": ("LOGSHI", 0o02010),
    "LOGNOT": ("LOGNOT", 0o02000),
    "INTOF": ("INTOF", 0o01724),
    "REALOF": ("REALOF", 0o02124),
    "BOOLAND": ("BOOLAN", 0o01474),
    "BOOLOR": ("BOOLOR", 0o01500),
    "NOT": ("NOT", 0o02054),
    "PARTAPPLY": ("PARTAP", 0o02064),
    "RECORDFNS": ("RECORD", 0o02130),
    "DATALIST": ("DATALI", 0o01574),
    "DATAWORD": ("DATAWO", 0o01600),
    "COPY": ("COPY", 0o01554),
    "STRIPFNS": ("STRIPF", 0o02160),
    "CONSREF": ("CONSRE", 0o01540),
    "DESTREF": ("DESTRE", 0o01614),
    "CONT": ("CONT", 0o01550),
    "CONSPAIR": ("CONSPA", 0o01534),
    "DESTPAIR": ("DESTPA", 0o01610),
    "FRONT": ("FRONT", 0o01660),
    "BACK": ("BACK", 0o01470),
    "ATOM": ("ATOM", 0o01464),
    "NULL": ("NULL", 0o02060),
    "CONS": ("CONS", 0o01530),
    "DEST": ("DEST", 0o01604),
    "HD": ("HD", 0o01674),
    "TL": ("TL", 0o02200),
    "FNTOLIST": ("FNTOLI", 0o01650),
    "INIT": ("INIT", 0o01714),
    "SUBSCR": ("SUBSCR", 0o02164),
    "INITC": ("INITC", 0o01720),
    "NEWANYARRAY": ("NEWANY", 0o02030),
    "NEWARRAY": ("NEWARR", 0o02034),
    "CONSWORD": ("CONSWO", 0o01544),
    "DESTWORD": ("DESTWO", 0o01620),
    "CHARWORD": ("CHARWO", 0o01520),
    "MEANING": ("MEANIN", 0o02024),
    "FNPROPS": ("FNPROP", 0o01644),
    "UPDATER": ("UPDATE", 0o02214),
    "FROZVAL": ("FROZVA", 0o01664),
    "FNPART": ("FNPART", 0o01640),
    "ISFUNC": ("ISFUNC", 0o01734),
    "POPMESS": ("POPMES", 0o02070),
    "INCHARITEM": ("INCHAR", 0o01710),
    "CHARIN": ("CHARIN", 0o01510),
    "ITEMREAD": ("ITEMRE", 0o01764),
    "CHAROUT": ("CHAROU", 0o01514),
    "SP": ("SP", 0o02150),
    "NL": ("NL", 0o02050),
    "PRINT": ("PRINT", 0o02104),
    "MACRESULTS": ("MACRES", 0o02014),
    "POPVAL": ("POPVAL", 0o02074),
    "SETPOP": ("SETPOP", 0o02140),
}

# POPLAN supplies these reference operations directly.
REFERENCE_OPERATIONS = {
    "<": 0o01400,
    ">": 0o01404,
    "=<": 0o01410,
    ">=": 0o01414,
    "+": 0o01420,
    "-": 0o01424,
    "*": 0o01430,
    "/": 0o01434,
    "//": 0o01444,
    "=": 0o01450,
    "::": 0o01454,
    "<>": 0o01460,
}

# The reference names on the left are not resident names. POPLAN exposes the
# same role through the generic operation or dialect spelling on the right.
REFERENCE_REPLACEMENTS = {
    "INTADD": ("+", 0o01420),
    "INTSUB": ("-", 0o01424),
    "INTMULT": ("*", 0o01430),
    "INTPLUS": ("+", 0o01420),
    "INTMINUS": ("-", 0o01424),
    "INTSIGN": ("SIGN", 0o02144),
    "INTGR": (">", 0o01404),
    "INTLE": ("<", 0o01400),
    "INTGREQ": (">=", 0o01414),
    "INTLEEQ": ("=<", 0o01410),
    "REALADD": ("+", 0o01420),
    "REALSUB": ("-", 0o01424),
    "REALMULT": ("*", 0o01430),
    "REALDIV": ("/", 0o01434),
    "REALPLUS": ("+", 0o01420),
    "REALMINUS": ("-", 0o01424),
    "REALSIGN": ("SIGN", 0o02144),
    "REALGR": (">", 0o01404),
    "REALLE": ("<", 0o01400),
    "REALGREQ": (">=", 0o01414),
    "REALLEEQ": ("=<", 0o01410),
    "SUBSCRC": ("SUBSCC", 0o02170),
    "OUTCHARITEM": ("GENOUT", 0o01670),
}

REFERENCE_MISSING = {"NONUNIQUE", "UNIQUE", "ENDDATA", "DELITEM", "NEXT"}

# CHARIN is now exercised through CARRYON's live-stream reader. POPMESS's
# no-argument CI/TO/LPO constructors are safe without external device setup.
STATIC_ONLY = set()


def image_words() -> list[int]:
    data = IMAGE.read_bytes()
    if len(data) % 6:
        raise AssertionError("POPLAN image is not an integral number of words")
    return [
        int.from_bytes(data[offset : offset + 6], "big")
        for offset in range(0, len(data), 6)
    ]


def dictionary_records(words: list[int]) -> dict[str, int]:
    records: dict[str, int] = {}
    for address in range(0o01400, 0o02740, 4):
        if words[address + 1] != DICTIONARY_CLASS:
            continue
        raw_name = words[address].to_bytes(6, "big").rstrip(b"\0")
        try:
            name = raw_name.decode("ascii")
        except UnicodeDecodeError:
            continue
        records[name] = address
    return records


class ReferenceStandardFunctionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = dictionary_records(image_words())

    def test_reference_inventory_partition(self):
        direct = set(REFERENCE_DIRECT)
        replacements = set(REFERENCE_REPLACEMENTS)
        self.assertTrue(direct.isdisjoint(replacements))
        self.assertTrue(direct.isdisjoint(REFERENCE_MISSING))
        self.assertTrue(replacements.isdisjoint(REFERENCE_MISSING))
        self.assertEqual(len(direct), 57)
        self.assertEqual(len(replacements), 23)
        self.assertEqual(len(REFERENCE_MISSING), 5)
        self.assertEqual(
            len(direct | replacements | REFERENCE_MISSING), 85
        )

    def test_reference_functions_recognized_by_poplan(self):
        for reference_name, (image_name, address) in REFERENCE_DIRECT.items():
            with self.subTest(reference_name=reference_name):
                self.assertEqual(self.records.get(image_name), address)

    def test_reference_operations_recognized_by_poplan(self):
        for operation, address in REFERENCE_OPERATIONS.items():
            with self.subTest(operation=operation):
                self.assertEqual(self.records.get(operation), address)
        # POPLAN spells the reference manual's exponent operation as `!`.
        self.assertEqual(self.records.get("!"), 0o01440)

    def test_poplan_replacements_are_present(self):
        for reference_name, (replacement, address) in REFERENCE_REPLACEMENTS.items():
            with self.subTest(reference_name=reference_name):
                self.assertEqual(self.records.get(replacement), address)

    def test_genuinely_missing_reference_functions_are_not_resident(self):
        for reference_name in REFERENCE_MISSING:
            with self.subTest(reference_name=reference_name):
                self.assertNotIn(reference_name[:6], self.records)

    def test_reference_names_replaced_by_poplan_are_not_resident(self):
        for reference_name in REFERENCE_REPLACEMENTS:
            if reference_name == "SUBSCRC":
                # Six-character lookup collides with SUBSCR; the character
                # strip selector is deliberately renamed SUBSCC in POPLAN.
                continue
            with self.subTest(reference_name=reference_name):
                self.assertNotIn(reference_name[:6], self.records)

    def test_executable_fixture_mentions_every_callable_direct_match(self):
        source = (
            ROOT / "tests" / "inputs" / "standard-functions-coverage.pop2"
        ).read_text(encoding="utf-8")
        for reference_name in REFERENCE_DIRECT.keys() - STATIC_ONLY:
            with self.subTest(reference_name=reference_name):
                self.assertRegex(source, rf"\b{re.escape(reference_name)}\b")

        for replacement in {value[0] for value in REFERENCE_REPLACEMENTS.values()}:
            if replacement.isalpha():
                with self.subTest(replacement=replacement):
                    self.assertRegex(source, rf"\b{re.escape(replacement)}\b")


if __name__ == "__main__":
    unittest.main()
