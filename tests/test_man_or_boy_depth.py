import importlib.util
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "man-or-boy-depth.py"
SPEC = importlib.util.spec_from_file_location("man_or_boy_depth", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class ManOrBoyDepthTest(unittest.TestCase):
    def test_canonical_results_and_depths(self):
        expected_values = [1, 0, -2, 0, 1, 0, 1, -1, -10, -30, -67]
        expected_depths = [2, 4, 6, 8, 16, 32, 64, 128, 256, 512, 1024]
        expected_a_depths = [1, 2, 3, 4, 8, 16, 32, 64, 128, 256, 512]

        results = [MODULE.man_or_boy(k) for k in range(11)]

        self.assertEqual([result.value for result in results], expected_values)
        self.assertEqual(
            [result.maximum_depth for result in results], expected_depths
        )
        self.assertEqual(
            [result.maximum_a_depth for result in results], expected_a_depths
        )

    def test_k10_call_counts(self):
        result = MODULE.man_or_boy(10)
        self.assertEqual(result.a_calls, 722)
        self.assertEqual(result.b_calls, 721)
        self.assertEqual(result.leaf_calls, 307)
        self.assertEqual(result.total_calls, 1750)

    def test_four_argument_variant(self):
        expected_values = [0, -2, 0, 1, 0, -3, -9, -22, -52, -122, -285]
        expected_depths = [2, 4, 6, 12, 24, 48, 96, 192, 384, 768, 1536]
        expected_a_depths = [1, 2, 3, 6, 12, 24, 48, 96, 192, 384, 768]

        results = [MODULE.man_or_boy(k, arity=4) for k in range(11)]

        self.assertEqual([result.value for result in results], expected_values)
        self.assertEqual(
            [result.maximum_depth for result in results], expected_depths
        )
        self.assertEqual(
            [result.maximum_a_depth for result in results], expected_a_depths
        )

    def test_three_argument_variant(self):
        expected_values = [-2, 0, 1, 2, 4, 9, 22, 56, 145, 378, 988]
        expected_depths = [2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048]

        results = [MODULE.man_or_boy(k, arity=3) for k in range(11)]

        self.assertEqual([result.value for result in results], expected_values)
        self.assertEqual(
            [result.maximum_depth for result in results], expected_depths
        )

    def test_two_argument_variant_diverges(self):
        self.assertEqual(MODULE.man_or_boy(0, arity=2).value, 0)
        with self.assertRaises(MODULE.DoesNotTerminate):
            MODULE.man_or_boy(1, arity=2)

    def test_rejects_other_arities(self):
        with self.assertRaisesRegex(ValueError, "arity must be between 2 and 5"):
            MODULE.man_or_boy(0, arity=1)


if __name__ == "__main__":
    unittest.main()
