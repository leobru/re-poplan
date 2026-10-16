#!/usr/bin/env python3
"""Selectable closure rebinding, independent results, and image-free isolation."""
import os
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tempfile

binary, guarded, root = (Path(p).resolve() for p in sys.argv[1:4])
env = {k: v for k, v in os.environ.items() if not k.startswith('POPLAN_')}
experimental = ['--native', '--native-closures=experimental']
template = (root / 'tests/inputs/man-or-boy.pop2').read_text()
expected = [1, 0, -2, 0, 1, 0, 1, -1, -10, -30, -67]
oracle = runpy.run_path(str(root / 'tools/man-or-boy-depth.py'))['man_or_boy']
assert [oracle(k).value for k in range(11)] == expected
assert (oracle(10).maximum_depth, oracle(10).total_calls) == (1024, 1750)
source = template[:template.index('A(10,ONE')] + ''.join(
    f'A({k},ONE,MINUS,MINUS,ONE,ZERO)=>\n' for k in range(11))
wanted = ''.join('**' + (' ' if n >= 0 else '') + str(n) + '\n' for n in expected)

with tempfile.TemporaryDirectory(prefix='poplan-closures-') as directory:
    def run(executable, args, source):
        return subprocess.run([str(executable), *args], input=source, text=True,
                              capture_output=True, env=env, cwd=directory, timeout=30)

    for executable in (binary, guarded):
        result = run(executable, experimental, source)
        assert result.returncode == 0 and result.stdout == wanted, result
        triangular = ('FUNCTI TRI N; IF N=0 THEN 0 ELSE N+TRI(N-1) CLOSE; END;\n'
                      'TRI(2000)=>\n')
        result = run(executable, experimental, triangular)
        assert result.returncode == 0 and result.stdout == '** 2001000\n', result
        for limit, success in ((1024, True), (1023, False)):
            result = run(executable, experimental + [f'--native-activation-limit={limit}'], template)
            assert (result.returncode == 0) == success, result
            assert result.stdout == '**-67\n' if success else 'native activation limit exceeded' in result.stderr
        for limit in (32, 128):
            result = run(executable, ['--native', f'--native-activation-limit={limit}'],
                         template.replace('A(10,', 'A(4,'))
            assert result.returncode == 1 and 'native activation limit exceeded' in result.stderr, result
        result = run(executable, experimental + ['--native-activation-limit=8'], template)
        assert result.returncode == 1 and 'native activation limit exceeded' in result.stderr, result
        # Historical native mode remains the default, including failure diagnostics.
        for fixture in ('native-nested', 'native-nested-recovery', 'native-rebinding', 'native-man-or-boy'):
            text = (root / 'tests/inputs' / (fixture + '.pop2')).read_text()
            result = run(executable, ['--native', '--native-closures=historical'], text)
            assert result.returncode == 0, result
            assert result.stdout == (root / 'tests/expected' / (fixture + '.out')).read_text(), result
        nested = (root / 'tests/inputs/native-nested.pop2').read_text()
        result = run(executable, experimental, nested)
        assert result.returncode == 0 and result.stdout.split() == (
            '**НЕОПРЕДЕЛЕН ** 1 ** 13 ** 77 ** 99 **НЕОПРЕДЕЛЕН ** 1 ** 35 '
            '** 0 ** 14 ** 99 ** 15, 23 ** 99 ** 0 ** 15 ** 6, 20 ** 99 ** 55 ** 77').split(), result
        siblings = ('VARS F G; FUNCTI MAKE X; FUNCTI INC; X+1->X; END; '
                    'FUNCTI GET; X; END; INC->F; GET->G; END;\n'
                    'MAKE(5); F(); G()=>\n')
        result = run(executable, experimental, siblings)
        assert result.returncode == 0 and result.stdout == '** 6\n', result
        reduced = (root / 'tests/inputs/native-rebinding.pop2').read_text()
        result = run(executable, experimental + ['--native-binding-trace'], reduced)
        assert result.returncode == 0, result
        assert result.stdout.split() == '** 1 ** 0 ** 0 ** 1 ** 3'.split(), result
        # B from the first A activation is installed a second time with K=1,
        # instead of reading the inner activation's K=0 again.
        installs = re.findall(r'BIND install word=\d+ name=K cell=(\d+) value=([^\n]+)', result.stderr)
        outer_cell = installs[0][0]
        restored = next(i for i, (cell, value) in enumerate(installs)
                        if cell == outer_cell and value.strip() == '1')
        assert any(cell != outer_cell and value.strip() == '0'
                   for cell, value in installs[:restored]), installs
        result = run(executable, experimental + ['--native-binding-trace'], template)
        depth = maximum = calls = 0
        for line in result.stderr.splitlines():
            if line.startswith('CALL '):
                calls += 1
                depth += 1
                maximum = max(maximum, depth)
            elif line.startswith('RETURN '):
                depth -= 1
        assert result.returncode == 0 and result.stdout == '**-67\n', result
        assert (depth, maximum, calls) == (0, 1024, 1750), (depth, maximum, calls)
        for suffix, arity, depth in (('three', 3, 2), ('four', 4, 3)):
            reduced_source = (root / 'tests/inputs' / f'man-or-boy-{suffix}.pop2').read_text()
            reduced_source = reduced_source.replace('A(10,', f'A({depth},')
            result = run(executable, experimental, reduced_source)
            assert result.returncode == 0 and result.stdout == f'** {oracle(depth, arity).value}\n', result
        two = (root / 'tests/inputs/man-or-boy-two.pop2').read_text().replace('A(10,', 'A(1,')
        result = run(executable, experimental + ['--native-activation-limit=64'], two)
        assert result.returncode == 1 and 'native activation limit exceeded' in result.stderr, result
        for args in (['--native-closures=experimental'], ['--native-activation-limit=32'],
                     experimental + ['--image', 'missing.bin'], ['--native', '--native-activation-limit=0'],
                     ['--native', '--native-activation-limit=-1'], ['--native', '--native-activation-limit=1x'],
                     ['--native', '--native-activation-limit=999999999999999999999999'],
                     ['--native', '--native-closures=unknown'], ['--native', '--io-demo']):
            result = run(executable, args, '2+2=>\n')
            assert result.returncode == 2 and 'usage:' in result.stderr, (args, result)
        print(executable.name + ': K=0..10, depth=1024, calls=1750, rebinding checkpoint, limits and CLI passed')
