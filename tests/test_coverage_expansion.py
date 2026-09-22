from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('expansion', ROOT / 'tools/coverage-expansion.py')
expansion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(expansion)


class ExpansionTests(unittest.TestCase):
    def test_generated_and_uncertain_exclusions(self):
        self.assertEqual(expansion.candidate_class(0o32535, set(), {0o32535}), 'generated/modified')
        self.assertEqual(expansion.candidate_class(0o12324, {0o12324}, {0o12324}), 'generated/modified')
        self.assertEqual(expansion.candidate_class(0o12324, set(), set()), 'uncertain listing boundary')
        self.assertEqual(expansion.candidate_class(0o12324, set(), {0o12324}), 'image-matching')

    def test_halves_dispatch_identity_and_edges(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'trace'
            path.write_text(
                '01000L acc=0 r15=0 word=0000000000000001\n'
                'ROUTINE 01000 -> 01001\n'
                '01001L acc=0 r15=0 word=0000000000000001\n'
                '01001R acc=0 r15=0 word=0000000000000002\n'
                'ROUTINE 01001 -> 01002\n'  # Right halves cannot dispatch.
                '40000L acc=0 r15=0 word=0000000000000000\n')
            seen, raw, changed, edges = expansion.profile(path, [1] * 0o36000)
        self.assertEqual(len(seen), 4)
        self.assertNotIn((0o1000, 'L'), raw)
        self.assertEqual(raw[(0o1001, 'R')], 1)
        self.assertEqual(changed, {0o1001, 0o40000})
        self.assertEqual(edges[((0o1001, 'L'), (0o1001, 'R'))], 1)
        self.assertEqual(seen - {(0o1000, 'L'), (0o1001, 'L')},
                         {(0o1001, 'R'), (0o40000, 'L')})

    def test_unmatched_dispatch_does_not_hide_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'trace'
            path.write_text('01000L acc=0 r15=0 word=0000000000000001\n'
                            'ROUTINE 01001 -> 01002\n')
            _, raw, _, _ = expansion.profile(path, [1] * 0o2000)
        self.assertEqual(raw[(0o1000, 'L')], 1)

    def test_timeout_is_not_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(expansion.subprocess, 'run', side_effect=subprocess.TimeoutExpired('test', 1)):
                result = expansion.run_case(Path(temporary), 'test', '', Path('/missing'), False, False, 1)
            self.assertEqual(result['native']['exit'], 'timeout')
            self.assertEqual(expansion.verdict(result, {}), 'failed execution')
            self.assertTrue((Path(temporary) / 'results.json').exists())

    def test_mismatch_is_not_pass(self):
        result = {'native': {'exit': 0, 'output': 'a'}, 'raw': {'exit': 0, 'output': 'b'}}
        self.assertEqual(expansion.verdict(result, {}), 'raw mismatch')
        result['raw']['exit'] = 1
        self.assertEqual(expansion.verdict(result, {}), 'failed execution')

    def test_known_difference_is_exact_and_not_a_pass(self):
        native = 'ОШИБКА 12046\nНЕПР.АРГ.РОРМЕSS\n** 4\n'
        result = {'native': {'exit': 0, 'output': native},
                  'raw': {'exit': 0, 'output': native.replace('НЕПР.АРГ.РОРМЕSS\n', '')}}
        case = {'diagnostic_mismatch': True, 'diagnostic': '12046'}
        self.assertEqual(expansion.verdict(result, case), 'known diagnostic mismatch')
        result['raw']['output'] += 'unexpected\n'
        self.assertEqual(expansion.verdict(result, case), 'raw mismatch')


if __name__ == '__main__':
    unittest.main()
