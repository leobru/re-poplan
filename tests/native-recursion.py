#!/usr/bin/env python3
"""Recursion boundaries, supported exhaustion recovery, and bounded checkpoints."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, guarded, root = map(Path, sys.argv[1:4])
historical = '--historical' in sys.argv[4:]
spec = importlib.util.spec_from_file_location('normalize', root / 'tools/normalize-output.py')
normalizer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(normalizer)
env = {k: v for k, v in os.environ.items() if not k.startswith('POPLAN_')}

with tempfile.TemporaryDirectory(prefix='poplan-recursion-') as directory:
    env['POPLAN_DISPAK_HOME'] = str(Path(directory) / 'historical-home')

    def run(executable, args, source):
        return subprocess.run([str(executable), *args], input=source, text=True,
                              capture_output=True, env=env, cwd=directory, timeout=30)

    for name, last in (('no-local', 124), ('one-local', 82), ('five-fakes', 34),
                       ('nested-numeric-fakes', 26), ('nested-zero-fakes', 26)):
        template = (root / 'tests/inputs' / ('triangular-' + name + '.pop2')).read_text()
        for depth in (last, last + 1):
            source = template.replace('TRI(10', 'TRI(' + str(depth))
            wanted = '** ' + str(depth * (depth + 1) // 2) + '\n'
            recoverable = depth > last and name.startswith('nested-')
            if recoverable:
                source += '\nN=>\n2+2=>\n' + template.strip().splitlines()[-1].replace('TRI(10', 'TRI(3') + '\n'
                wanted_failure = ('****ОШИБКА 13000\n0000000000067000  .00000000\n\nSЕТРОР\n'
                                  '** 0\n** 4\n** 6\n')
            for executable in (binary, guarded):
                result = run(executable, ['--native'], source)
                if depth == last:
                    assert result.returncode == 0 and result.stdout == wanted, (name, depth, result)
                elif recoverable:
                    assert result.returncode == 0 and result.stdout == wanted_failure, (name, depth, result)
                else:
                    assert result.returncode != 0 and not result.stdout, (name, depth, result)
                    assert 'argument rebuilding storage exhausted' in result.stderr, result
                    assert 'diagnostic 13000 rendering incomplete' in result.stderr, result
            if depth == last:
                for controls in ({}, {'POPLAN_INTERPRET_ONLY': '1'}):
                    old = dict(env)
                    env.update(controls)
                    result = run(binary, ['--image', str(root / 'build/poplan.bin')], source)
                    env.clear()
                    env.update(old)
                    assert result.returncode == 0 and normalizer.normalize(result.stdout) == wanted, (name, result)
            if historical:
                result = run(root / 'tools/run-dispak.sh', ['--bootstrap', str(root / 'poplan.b6')], source)
                assert result.returncode == 0, result
                if depth == last:
                    assert normalizer.normalize(result.stdout) == wanted, (name, result)
                elif recoverable:
                    # The supervisor prints an emulator instruction before POP's
                    # diagnostic. It is host tracing, not language output.
                    actual = normalizer.normalize(result.stdout)
                    actual = actual[actual.index('****ОШИБКА 13000'):]
                    assert actual == wanted_failure, (name, result)
                else:
                    assert 'ОШИБКА 13000' in result.stdout and 'SЕТРОР' in result.stdout, (name, result)
        print(name + ': boundary ' + str(last) + '/' + str(last + 1)
              + (' passed with recovery' if name.startswith('nested-') else ' passed; continuation rendering remains unsupported'))

    for suffix, first_failure, wanted in (('', 4, '** 0\n'), ('-four', 3, '** 0\n'),
                                          ('-three', 2, '** 0\n')):
        template = (root / 'tests/inputs' / ('man-or-boy' + suffix + '.pop2')).read_text()
        for depth in (first_failure - 1, first_failure):
            source = template.replace('A(10,', 'A(' + str(depth) + ',')
            recoverable = suffix == '-four' and depth == first_failure
            if recoverable:
                source += '\nK=>\nB=>\nX1=X2=>\nA(0,ONE,MINUS,MINUS,ONE)=>\nK=>\n'
                wanted_failure = ('****ОШИБКА 13000\n0000000000000003  .00000000\n\nSЕТРОР\n'
                                  '**-28\n**НЕОПРЕДЕЛЕН\n** 1\n** 0\n**-28\n')
            for executable in (binary, guarded):
                result = run(executable, ['--native'], source)
                if depth < first_failure:
                    assert result.returncode == 0 and result.stdout == wanted, (suffix, depth, result)
                elif recoverable:
                    assert result.returncode == 0 and result.stdout == wanted_failure, (suffix, depth, result)
                else:
                    assert result.returncode != 0 and 'storage exhausted' in result.stderr, (suffix, depth, result)
            if historical:
                result = run(root / 'tools/run-dispak.sh', ['--bootstrap', str(root / 'poplan.b6')], source)
                assert result.returncode == 0, result
                if depth < first_failure:
                    assert normalizer.normalize(result.stdout) == wanted, (suffix, depth, result)
                else:
                    assert 'ОШИБКА 13000' in result.stdout, (suffix, depth, result)
        print('man-or-boy' + suffix + ': success/failure K=' + str(first_failure - 1)
              + '/' + str(first_failure) + ('; partial binding recovery passed' if suffix == '-four' else '; function diagnostic rendering remains open'))

    # Bound the inherently recursive two-argument variant in the POP test,
    # never by a fixture-specific runtime recursion rule or timeout outcome.
    source = (root / 'tests/inputs/man-or-boy-two.pop2').read_text()
    source = source.replace('K-1->K;', 'K-1->K; K=>\n    COUNT+1->COUNT;\n    IF COUNT=6 THEN SETPOP() CLOSE;')
    source = 'VARS COUNT; 0->COUNT;\n' + source.replace('A(10,', 'A(1,') + '\nCOUNT=>\n'
    wanted = '** 0\n\n**-1\n\n**-2\n\n**-3\n\n**-4\n\n**-5\n\nSЕТРОР\n** 6\n'
    for executable in (binary, guarded):
        result = run(executable, ['--native'], source)
        assert result.returncode == 0 and result.stdout == wanted, result
    if historical:
        result = run(root / 'tools/run-dispak.sh', ['--bootstrap', str(root / 'poplan.b6')], source)
        assert result.returncode == 0 and normalizer.normalize(result.stdout) == wanted, result
    print('man-or-boy-two: K=1 bounded decrement checkpoints passed')
