#!/usr/bin/env python3
"""Native development slice, with independent raw and guarded execution."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary, guarded, root = map(Path, sys.argv[1:4])
historical = '--historical' in sys.argv[4:]
expressions = (root / 'tests/inputs/native-expressions.pop2').read_text()
expression_expected = (root / 'tests/expected/native-expressions.out').read_text()
sys.path.insert(0, str(root / 'tools'))
import importlib.util
spec = importlib.util.spec_from_file_location('normalize_output', root / 'tools/normalize-output.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
normalize = module.normalize

fixtures = [(name, (root / 'tests/inputs' / (name + '.pop2')).read_text(),
             (root / 'tests/expected' / (name + '.out')).read_text())
            for name in ('native-bindings', 'native-recovery', 'native-control', 'native-control-recovery', 'native-functions', 'native-functions-recovery', 'native-nested', 'native-nested-recovery', 'native-rebinding', 'native-man-or-boy', 'native-booleans', 'native-booleans-recovery', 'native-numeric-functions', 'native-numeric-functions-recovery', 'native-type-predicates', 'native-type-predicates-recovery')]

controls = ('POPLAN_INTERPRET_ONLY', 'POPLAN_DISABLE_TRANSLATED_ROUTINES',
            'POPLAN_CPU_TRACE', 'POPLAN_CPU_DETAIL_TRACE', 'POPLAN_ROUTINE_TRACE',
            'POPLAN_E70_TRACE')
env = {k: v for k, v in os.environ.items() if k not in controls}
source = '2+2=>\n3-5=>\n7*8=>\n1/2=>\n'
expected = '** 4\n**-2\n** 56\n** .50000000\n'

def run(executable, args, text, cwd, environment=env):
    return subprocess.run([str(executable), *args], input=text, text=True,
                          capture_output=True, cwd=cwd, env=environment, timeout=30)

with tempfile.TemporaryDirectory(prefix='poplan-native-') as directory:
    # No image or library is present in the working directory.
    for executable in (binary, guarded):
        result = run(executable, ['--native'], source, directory)
        assert result.returncode == 0, result.stderr
        assert result.stdout == expected, repr(result.stdout)
        # Statements now execute as they arrive. Unsupported later facilities
        # cannot retract earlier output, and EOF does not invent a delimiter.
        result = run(executable, ['--native'], '2+2=> LAMBDA; END;', directory)
        assert result.returncode != 0 and result.stdout == '** 4\n', result
        assert 'compiler incomplete' in result.stderr, result.stderr
        result = run(executable, ['--native'], 'FUNCTI F; 1 END; F+1=>', directory)
        assert result.returncode != 0 and 'compound diagnostic rendering' in result.stderr, result
        for text in ('NOT=>', 'NOT->F;', 'INTOF=>', 'INTOF->F;', 'SIGN=>', 'SIGN->F;', 'ISINTE=>', 'ISREAL->F;', 'ISFUNC=>'):
            result = run(executable, ['--native'], text, directory)
            assert result.returncode != 0 and 'requires a parenthesized call' in result.stderr, result
        for text in ('1.+2=>', '2//3=>'):
            result = run(executable, ['--native'], text, directory)
            assert result.returncode != 0, result
            assert 'compiler incomplete' in result.stderr, result.stderr
        for text in ('1+', '2', '(2+3'):
            result = run(executable, ['--native'], text, directory)
            assert result.returncode == 0 and not result.stdout, result
        result = run(executable, ['--native'], expressions, directory)
        assert result.returncode == 0 and result.stdout == expression_expected, result
        for name, text, wanted in fixtures:
            result = run(executable, ['--native'], text, directory)
            assert result.returncode == 0 and normalize(result.stdout) == wanted, (name, result)
        # Explicit pending operations, not C++ recursion, handle grouping.
        deep = '(' * 5000 + '2+3' + ')' * 5000 + '=>'
        result = run(executable, ['--native'], deep, directory)
        assert result.returncode == 0 and result.stdout == '** 5\n', result
        nested = 'IF TRUE THEN ' * 300 + '7' + ' ELSE 1/0 CLOSE' * 300 + '=>'
        result = run(executable, ['--native'], nested, directory)
        assert result.returncode == 0 and result.stdout == '** 7\n', result
        # No branch executes while its enclosing compilation is suspended.
        result = run(executable, ['--native'], 'VARS X; 1->X; IF TRUE THEN 9->X; X=>', directory)
        assert result.returncode == 0 and not result.stdout, result
        reals = '3.14=> .5=> 3.14$-3=> 3.14$+2=> .5$2=> 0.0=> 123456789.25=> 1.234567890123=> 1.0+2=> -.5=>'.replace(' ', '\n') + '\n'
        real_expected = ('** 3.1400000\n** .50000000\n** .00314000\n** 314.00000\n'
                         '** 50.000000\n** .00000000\n** 123456789.2\n** 1.2345679\n'
                         '** 3.0000000\n**-.50000000\n')
        result = run(executable, ['--native'], reals, directory)
        assert result.returncode == 0 and result.stdout == real_expected, result
        for text in ('3.14=> 1.0$+=>', '.=>', '1.0$1000=>', '549755813888.0=>'):
            result = run(executable, ['--native'], text, directory)
            assert result.returncode != 0, result
        result = run(executable, ['--native'], '1/0=>\n2+2=>\n', directory)
        assert result.returncode == 0 and 'ОШИБКА 13016' in result.stdout and result.stdout.endswith('** 4\n'), result
        for control in controls:
            result = run(executable, ['--native'], source, directory,
                         dict(env, **{control: ''}))
            assert result.returncode == 2 and control in result.stderr, result
        for args in (['--native', '--image', 'absent.bin'], ['--native', '--io-demo']):
            result = run(executable, args, source, directory)
            assert result.returncode == 2 and not result.stdout, result
    # The guard must actually reject the reference path, so a missing linkage
    # or inactive assertion cannot make the native test pass vacuously.
    result = run(guarded, ['--image', str(root / 'build/poplan.bin')], source, directory)
    assert result.returncode != 0 and 'native isolation guard' in result.stderr, result
    for environment in (env, dict(env, POPLAN_INTERPRET_ONLY='1')):
        result = run(binary, ['--image', str(root / 'build/poplan.bin')], source, root, environment)
        assert result.returncode == 0, result.stderr
        assert normalize(result.stdout) == expected, repr(result.stdout)
        result = run(binary, ['--image', str(root / 'build/poplan.bin')], reals, root, environment)
        assert result.returncode == 0, result.stderr
        assert normalize(result.stdout) == real_expected, repr(result.stdout)
        result = run(binary, ['--image', str(root / 'build/poplan.bin')], expressions, root, environment)
        assert result.returncode == 0 and normalize(result.stdout) == expression_expected, result
        for name, text, wanted in fixtures:
            result = run(binary, ['--image', str(root / 'build/poplan.bin')], text, root, environment)
            # The instruction-only runner omits catalog explanations in this
            # fixture; historical POPLAN supplies them. Compare the remaining
            # diagnostic codes, values, source context and recovery exactly.
            reference_wanted = wanted
            if name in ('native-recovery', 'native-control-recovery', 'native-functions-recovery', 'native-booleans-recovery', 'native-numeric-functions-recovery', 'native-type-predicates-recovery') and 'POPLAN_INTERPRET_ONLY' in environment:
                for explanation in ('АРГУМЕНТ + НЕ ЧИСЛО', 'НЕТ РАЗДЕЛИТЕЛЯ', 'ДЕКЛАРАЦИЯ НЕ СЛОВА', 'ДЛИНА ЦЕЛОГО>12', 'ПРИСВАИВАНИЕ ЗАЩИЩЕННОЙ ПЕРЕМЕННОЙ', 'Х<У,Х НЕ ЧИСЛО', 'Х<У,У НЕ ЧИСЛО', 'Х>У,У НЕ ЧИСЛО', 'ОШИБКА В ИМПЕРАТИВЕ', 'INТОF(Х),Х НЕ ВЕЩЕСТВЕННОЕ', 'SIGN(Х) Х НЕ ЧИСЛО'):
                    reference_wanted = reference_wanted.replace(explanation + '\n', '')
            assert result.returncode == 0 and normalize(result.stdout) == reference_wanted, (name, result)
    if historical:
        result = run(root / 'tools/run-dispak.sh', ['--bootstrap', str(root / 'poplan.b6')], expressions, root)
        assert result.returncode == 0 and normalize(result.stdout) == expression_expected, result
        for name, text, wanted in fixtures:
            result = run(root / 'tools/run-dispak.sh', ['--bootstrap', str(root / 'poplan.b6')], text, root)
            assert result.returncode == 0 and normalize(result.stdout) == wanted, (name, result)
print('native probe, raw/hybrid comparison, CLI rejection and execution guard passed')
