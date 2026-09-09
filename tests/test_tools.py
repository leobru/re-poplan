from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


normalize_output = load_module(
    "normalize_output", ROOT / "tools" / "normalize-output.py"
)
analyze_trace = load_module("analyze_trace", ROOT / "tools" / "analyze-trace.py")
summarize_coverage = load_module(
    "summarize_coverage", ROOT / "tools" / "summarize-coverage.py"
)
convert_koi7_lower = load_module(
    "convert_koi7_lower", ROOT / "tools" / "convert-koi7-lower.py"
)
show_poplib_catalog = load_module(
    "show_poplib_catalog", ROOT / "tools" / "show-poplib-catalog.py"
)
make_poplib = load_module("make_poplib", ROOT / "tools" / "make-poplib.py")


class NormalizeOutputTests(unittest.TestCase):
    def test_removes_session_framing(self):
        text = (
            "\ufeffПОПЛАН 2.1  ВРЕМЯ 12.34.56    ДОБРЫЙ ДЕНЬ\n"
            "::::\n"
            ":program output\n"
            ":\n"
            "ВЫХОД 12.34.57   ВРЕМЯ СЕАНСА 00.00.01\n"
            "ВСЕГО ВАМ ДОБРОГО\n"
        )
        self.assertEqual(normalize_output.normalize(text), "program output\n")


class ConvertKoi7LowerTests(unittest.TestCase):
    def test_converts_requested_koi7_range(self):
        self.assertEqual(
            convert_koi7_lower.convert("abcxyz ABC [] `{|}~"),
            "АБЦЬЫЗ ABC [] ЮШЭЩЧ",
        )


class ShowPoplibCatalogTests(unittest.TestCase):
    def test_finds_and_renders_catalog(self):
        directory_size = 8
        words = [0] * show_poplib_catalog.WORDS_PER_ZONE
        words[:8] = [
            show_poplib_catalog.HEADER_VALUE | directory_size,
            show_poplib_catalog.MAGIC_WORD,
            2,
            1,
            int.from_bytes(bytes((0o60, 0o56, 0o60, 0o104, 0o102, 0o42)),
                           "big"),
            int.from_bytes(bytes((0o100, 0o56, 0o111, 0o107, 0o110, 0o17)),
                           "big"),
            0o7,
            9172,
        ]

        catalogs = show_poplib_catalog.find_catalogs(words)
        output = show_poplib_catalog.render(
            Path("poplib.bin"), show_poplib_catalog.ZONE_BYTES, catalogs
        )

        self.assertEqual(len(catalogs), 1)
        self.assertEqual(catalogs[0].records[0].user, "POPLIB")
        self.assertEqual(catalogs[0].records[0].name, "FOURS")
        self.assertIn("POPLIB  FOURS   0007  0010  9172", output)

    def test_rejects_inconsistent_directory_size(self):
        words = [0] * show_poplib_catalog.WORDS_PER_ZONE
        words[:4] = [
            show_poplib_catalog.HEADER_VALUE | 4,
            show_poplib_catalog.MAGIC_WORD,
            2,
            1,
        ]
        with self.assertRaisesRegex(
                show_poplib_catalog.CatalogError, "records require 8"):
            show_poplib_catalog.find_catalogs(words)


class MakePoplibTests(unittest.TestCase):
    def test_restores_unicode_cyrillic_to_internal_koi7(self):
        self.assertEqual(
            make_poplib.encode_source("ФУНКЦИЯ\n".encode()),
            b"funkciq\012",
        )

    def test_library_names_use_poplan_six_character_word(self):
        self.assertEqual(
            make_poplib.name_word("MEMOFNS"),
            make_poplib.name_word("MEMOFN"),
        )


class AnalyzeTraceTests(unittest.TestCase):
    def analyze(self, text: str):
        with tempfile.NamedTemporaryFile("w", encoding="utf-8") as stream:
            stream.write(text)
            stream.flush()
            return analyze_trace.analyze(Path(stream.name))

    def test_counts_calls_and_detects_nested_call(self):
        result = self.analyze(
            "01000: vjm 02000(15) (=0) r[15]=00000\n"
            "02000: vjm 03000(15) (=0) r[15]=01000\n"
            "03000: uj  (15) (=0) r[15]=02001\n"
            "02001: uj  (15) (=0) r[15]=01001\n"
            "01001: *74 (=0)\n"
        )

        self.assertEqual(result.targets[0o2000].calls, 1)
        self.assertTrue(result.targets[0o2000].nested_call_observed)
        self.assertFalse(result.targets[0o3000].nested_call_observed)

    def test_utc_link_return_does_not_absorb_caller_calls(self):
        result = self.analyze(
            "01000: vjm 02000(15) (=0) r[15]=00000\n"
            "02000: utc (15) (=0) r[15]=01001\n"
            "02000: vzm 01001(16) (=0) r[16]=00000\n"
            "01001: vjm 03000(15) (=0) r[15]=01001\n"
            "03000: uj  (15) (=0) r[15]=01002\n"
            "01002: *74 (=0)\n"
        )

        self.assertFalse(result.targets[0o2000].nested_call_observed)


class SummarizeCoverageTests(unittest.TestCase):
    def test_combines_halves_and_counts_exclusive_words(self):
        with tempfile.TemporaryDirectory() as directory:
            first_path = Path(directory) / "first.cov"
            second_path = Path(directory) / "second.cov"
            first_path.write_text(
                "01000: L \n01001: LR\n01002: --\n", encoding="ascii"
            )
            second_path.write_text(
                "01000:  R\n01001: --\n01002: LR\n", encoding="ascii"
            )

            first = summarize_coverage.read_coverage(first_path)
            second = summarize_coverage.read_coverage(second_path)
            report = summarize_coverage.render([first, second])

        self.assertEqual(first.words, {0o1000, 0o1001})
        self.assertEqual(first.left, {0o1000, 0o1001})
        self.assertEqual(first.right, {0o1001})
        self.assertIn("| `first.cov` | 2 | 2 | 2 | 1 | 1 |", report)
        self.assertIn("| `second.cov` | 2 | 2 | 1 | 2 | 1 |", report)
        self.assertIn("Union word addresses: 3", report)


if __name__ == "__main__":
    unittest.main()
