#!/usr/bin/env python3
"""POPMESS transfers: independent disk copies, fallback audit, optional historical oracle."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('coverage_expansion', ROOT / 'tools/coverage-expansion.py')
coverage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(coverage)


def verify(binary, directory, historical, case="devices"):
    directory.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('POPLAN_')}
    source = (ROOT / f'tests/inputs/popmess-{case}.pop2').read_text()
    expected = (ROOT / f'tests/expected/popmess-{case}.out').read_text()
    blank = bytes(6144 * 3)
    def run(args, text='', environment=env):
        return subprocess.run([str(binary), '--image', str(ROOT / 'build/poplan.bin'), *args],
                              input=text, text=True, capture_output=True, cwd=directory,
                              env=environment, timeout=60)
    disks = []
    for mode in ('hybrid', 'raw'):
        disk = directory / (mode + '.bin')
        disk.write_bytes(blank)
        controls = {'POPLAN_INTERPRET_ONLY': '1'} if mode == 'raw' else {
            'POPLAN_CPU_TRACE': '1', 'POPLAN_ROUTINE_TRACE': '1'}
        result = run(['--disk-rw', f'57={disk}'], source, dict(env, **controls))
        (directory / (mode + '.out')).write_text(result.stdout)
        (directory / (mode + '.trace')).write_text(result.stderr)
        assert result.returncode == 0, result.stderr[-2000:]
        comparison = expected
        if mode == 'raw' and case == 'errors':
            # Established raw catalog limitation, not a transfer discrepancy.
            for description in ('АРГ.ПОТРЕБИТЕЛЯ ЛИТЕР НЕ ЦЕЛОЕ',
                                'ЗАПИСЬ В ЗАКРЫТЫЙ ФАЙЛ', 'ЧТЕНИЕ ЗАКРЫТОГО ФАЙЛА'):
                comparison = comparison.replace(description + '\n', '')
        assert coverage.normalizer.normalize(result.stdout) == comparison, result.stdout
        disks.append(disk.read_bytes())
    assert disks[0] == disks[1] and disks[0] != blank, 'disk contents must agree and change'
    blob = (ROOT / 'build/poplan.bin').read_bytes()
    image = [int.from_bytes(blob[i:i+6], 'big') for i in range(0, len(blob), 6)]
    _, fallback, _, _ = coverage.profile(directory / 'hybrid.trace', image)
    assert not fallback, [(f'{a:05o}{h}', n) for (a, h), n in fallback.items()]

    if historical:
        # The wrapper creates a private writable unit 057 from this flat image.
        (directory / 'poplib.bin').write_bytes(blank)
        home = directory / 'historical-home'
        result = subprocess.run([str(ROOT / 'tools/run-dispak.sh'), '--bootstrap', str(ROOT / 'poplan.b6')],
                                input=source, text=True, capture_output=True, cwd=directory,
                                env=dict(env, POPLAN_DISPAK_HOME=str(home)), timeout=60)
        (directory / 'historical.out').write_text(result.stdout)
        assert result.returncode == 0, result.stderr
        assert coverage.normalizer.normalize(result.stdout) == expected, result.stdout
        # DISPAK physical format: four prefix zones, eight control words per
        # zone, little-endian 64-bit containers for 48-bit data words.
        physical = (home / '.besm6/2157').read_bytes()
        recovered = bytearray()
        for zone in range(3):
            start = (zone + 4) * 8256 + 64
            assert len(physical) >= start + 8192
            for offset in range(start, start + 8192, 8):
                bits = int.from_bytes(physical[offset:offset+8], 'little') & ((1 << 48) - 1)
                recovered.extend(bits.to_bytes(6, 'big'))
        assert recovered == disks[0], 'historical and C++ disk contents differ'

    if case == 'errors':
        print('POPMESS diagnostic recovery and disk contents: pass')
        return
    verify(binary, directory / 'errors', historical, 'errors')

    # Console suppliers suspend at EOF without inventing a character result.
    for supplier in ('TI', 'CI'):
        source = f'VARS F;POPMESS([{supplier}])->F;F()=>\n'
        for controls in ({}, {'POPLAN_INTERPRET_ONLY': '1'}):
            result = run([], source, dict(env, **controls))
            assert result.returncode == 0 and coverage.normalizer.normalize(result.stdout) == '', result
        if historical:
            result = subprocess.run(
                [str(ROOT / 'tools/run-dispak.sh'), '--bootstrap', str(ROOT / 'poplan.b6')],
                input=source, text=True, capture_output=True, cwd=directory,
                env=dict(env, POPLAN_DISPAK_HOME=str(directory / 'eof-home')), timeout=60)
            assert result.returncode == 0 and coverage.normalizer.normalize(result.stdout) == '', result

    disk = directory / 'protected.bin'
    disk.write_bytes(blank)
    write = 'VARS F;POPMESS([MO 8:570000])->F;F(65);POPMESS([% "CLOSE", F %]);\n'
    # Both execution modes must reach the same host errors, without changing a
    # protected image, creating files, or extending the mounted image.
    for controls in ({}, {'POPLAN_INTERPRET_ONLY': '1'}):
        local = dict(env, **controls)
        for args, text, error in (
            (['--disk', f'57={disk}'], write, 'read-only'),
            ([], write.replace('570000', '560000'), 'not mounted'),
            (['--disk-rw', f'57={disk}'], write.replace('570000', '570003'), 'cannot read complete disk zone'),
            (['--disk', f'57={disk}'], 'VARS F;POPMESS([MI 8:570003])->F;F()=>\n', 'cannot read complete disk zone'),
        ):
            result = run(args, text, local)
            assert result.returncode != 0 and error in result.stderr, result
            assert disk.read_bytes() == blank
    malformed = directory / 'malformed.bin'
    malformed.write_bytes(b'x')
    for args in (
        ['--disk', f'57={malformed}'], ['--disk', f'57={directory / "missing"}'],
        ['--disk', f'57={disk}', '--disk-rw', f'57={disk}'],
        ['--disk', f'80={disk}'], ['--disk', f'100={disk}'], ['--disk', '57='],
        ['--native', '--disk', f'57={disk}'], ['--io-demo', '--disk', f'57={disk}'],
    ):
        result = run(args)
        assert result.returncode != 0, args
    # A new process sees persisted writes through a read-only mount.
    result = run(['--disk', f'57={directory / "hybrid.bin"}'],
                 'VARS F;POPMESS([MI 8:570000])->F;F()=>\n')
    assert result.returncode == 0 and coverage.normalizer.normalize(result.stdout) == '** 65\n', result
    print('POPMESS transfers, persistent disk contents, host errors and zero fallback: pass')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=ROOT / 'build/cpp/poplan')
    parser.add_argument('--historical', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        verify(args.executable.resolve(), args.output.resolve(), args.historical)
    else:
        with tempfile.TemporaryDirectory(prefix='popmess-devices-') as temporary:
            verify(args.executable.resolve(), Path(temporary), args.historical)


if __name__ == '__main__':
    main()
