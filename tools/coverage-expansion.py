#!/usr/bin/env python3
"""Reproducible, non-writing device probes and observed fallback accounting."""
from __future__ import annotations

import argparse
from collections import Counter
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('normalize', ROOT / 'tools/normalize-output.py')
normalizer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(normalizer)
STEP = re.compile(r'^([0-7]{5})([LR]) .* word=([0-7]{16})$')
DISPATCH = re.compile(r'^ROUTINE ([0-7]{5}) -> ([0-7]{5})$')


def candidate_class(address, modified, listing):
    if address in modified or 0o32535 <= address <= 0o32566:
        return 'generated/modified'
    return 'image-matching' if address in listing else 'uncertain listing boundary'


def profile(path, image):
    """Count dispatches separately; preserve halves and observed word identity.

    Matching image bytes alone do not prove immutability. These are candidates,
    not a claim that stores to the address never occurred between observations.
    """
    visited, fallback, modified, edges = set(), Counter(), set(), Counter()
    pending = None
    def finish(dispatched=False):
        if pending is not None and not dispatched:
            fallback[pending] += 1
    with Path(path).open(errors='replace') as stream:
        for line in stream:
            line = line.rstrip()
            match = STEP.match(line)
            if match:
                finish()
                address, half, word = int(match[1], 8), match[2], int(match[3], 8)
                key = (address, half)
                if pending is not None:
                    edges[(pending, key)] += 1
                pending = key
                visited.add(key)
                if address >= len(image) or image[address] != word:
                    modified.add(address)
            else:
                match = DISPATCH.match(line)
                if match and pending == (int(match[1], 8), 'L'):
                    finish(True)
                    pending = None
    finish()
    return visited, fallback, modified, edges


def run_case(directory, name, source, executable, historical, trace, timeout):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'input.pop2').write_text(source)
    results = {}
    for mode in ['native', 'raw'] + (['historical'] if historical else []):
        env = {k: v for k, v in os.environ.items() if not k.startswith('POPLAN_')}
        # Use a distinct wrapper home; never share its writable POPLIB or queue.
        env['POPLAN_DISPAK_HOME'] = str(directory.resolve() / 'dispak-home')
        cmd = [str(executable), '--image', str(ROOT / 'build/poplan.bin')]
        if mode == 'raw':
            env['POPLAN_INTERPRET_ONLY'] = '1'
        if trace and mode != 'historical':
            env.update(POPLAN_CPU_TRACE='1', POPLAN_ROUTINE_TRACE='1')
        if mode == 'historical':
            cmd = [str(ROOT / 'tools/run-dispak.sh'), '--bootstrap',
                   '--coverage=' + str((directory / 'historical.cov').resolve()),
                   str(ROOT / 'poplan.b6')]
        with (directory / (mode + '.out')).open('w') as out, \
                (directory / (mode + '.trace')).open('w') as err:
            try:
                result = subprocess.run(cmd, input=source, text=True, stdout=out,
                                        stderr=err, env=env, cwd=ROOT, timeout=timeout)
                status = result.returncode
            except subprocess.TimeoutExpired:
                status = 'timeout'
        output = normalizer.normalize((directory / (mode + '.out')).read_text())
        (directory / (mode + '.normalized')).write_text(output)
        results[mode] = {'exit': status, 'output': output}
    (directory / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
    return results


def verdict(results, case):
    if any(value['exit'] != 0 for value in results.values()):
        return 'failed execution'
    native = results['native']['output']
    if 'historical' in results and native != results['historical']['output']:
        return 'historical mismatch'
    if native != results['raw']['output']:
        if case.get('diagnostic_mismatch'):
            message = ('АРГ.НD,ТL,NULL НЕ СПИСОК' if case['diagnostic'] == '12020'
                       else 'НЕПР.АРГ.РОРМЕSS') + '\n'
            if (message in native and native.replace(message, '', 1) == results['raw']['output']
                    and 'ОШИБКА ' + case['diagnostic'] in native and '** 4\n' in native):
                return 'known diagnostic mismatch'
        return 'raw mismatch'
    if case.get('diagnostic_mismatch'):
        return 'unexpected agreement (review experiment)'
    if 'expected' in case and native != case['expected']:
        return 'unexpected output'
    if case.get('diagnostic') and ('ОШИБКА ' + case['diagnostic'] not in native or '** 4\n' not in native):
        return 'missing diagnostic/recovery'
    return 'pass'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, default=ROOT / 'build/cpp/poplan')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/coverage-expansion')
    parser.add_argument('--report', type=Path,
                        help='report destination; defaults to docs/reports/coverage-expansion.md for full profiles, stdout for regressions')
    parser.add_argument('--regression', action='store_true', help='supported cases only, no baseline profiling')
    parser.add_argument('--historical', action='store_true')
    parser.add_argument('--report-only', action='store_true',
                        help='reanalyze saved runs; requires identical saved probe inputs')
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    cases = json.loads((ROOT / 'tests/inputs/coverage-expansion.json').read_text())
    if args.regression:
        cases = [case for case in cases if not case.get('diagnostic_mismatch')]
    sources = []
    if not args.regression:
        for path in [ROOT / 'quine.pop2', *sorted(ROOT.glob('zone*.pop2')),
                     ROOT / 'tests/inputs/language-coverage.pop2',
                     ROOT / 'tests/inputs/standard-functions-coverage.pop2']:
            sources.append(('baseline-' + path.stem, path.read_text(), None))
        game = (ROOT / 'ttt.pop2').read_text()
        assert 'INTOF(100*POPTIM())->RANSEED' in game
        sources.append(('baseline-fixed-game', game.replace('INTOF(100*POPTIM())->RANSEED',
                        '1->RANSEED') + '\nFOURS;\nYES\nYES\n0 0 0\n1 1 1\n^\n', None))
    sources += [(case['name'], case['input'], case) for case in cases]
    blob = (ROOT / 'build/poplan.bin').read_bytes()
    image = [int.from_bytes(blob[i:i+6], 'big') for i in range(0, len(blob), 6)]
    listing = set()
    if not args.regression:
        for line in (ROOT / 'build/poplan.lst').read_text().splitlines():
            match = re.match(r'^\s*([0-7]{1,5})[: ]+[0-7]{2} [0-7]{2,3} [0-7]{4,5}\s', line)
            if match:
                listing.add(int(match[1], 8))
    baseline, expanded, modified = set(), set(), set()
    profiles, rows, failed = [], [], False
    for name, source, case in sources:
        directory = args.output / name
        if args.report_only:
            if (directory / 'input.pop2').read_text() != source:
                raise ValueError(f'{name}: saved input differs; rerun coverage expansion')
            results = json.loads((directory / 'results.json').read_text())
            if args.historical and 'historical' not in results:
                raise ValueError(f'{name}: historical run missing')
        else:
            results = run_case(directory, name, source, args.executable.resolve(),
                               args.historical, not args.regression, args.timeout)
        status = verdict(results, case or {})
        failed |= status not in ('pass', 'known diagnostic mismatch')
        rows.append(f'| {name} | {status} |')
        print(name + ': ' + status, flush=True)
        if not args.regression:
            raw_seen, _, raw_modified, _ = profile(directory / 'raw.trace', image)
            if not raw_seen:
                raise ValueError(f'{name}: no word-bearing CPU trace; rerun coverage expansion')
            _, counts, native_modified, edges = profile(directory / 'native.trace', image)
            (baseline if case is None else expanded).update(raw_seen)
            modified.update(raw_modified | native_modified)
            if case is not None:
                profiles.append((name, counts, edges))
    lines = ['# Coverage expansion', '', '| Probe | Result |', '|---|---|', *rows, '']
    if not args.regression:
        delta = expanded - baseline
        lines += [f'New observed halves: {len(delta)}; new word addresses: '
                  f'{len({a for a, h in expanded} - {a for a, h in baseline})}.', '',
                  'Locations (raw execution; includes generated/modified code):',
                  '`' + ' '.join(f'{a:05o}{h}' for a, h in sorted(delta)) + '`', '',
                  '## Image-matching fallback candidates', '',
                  'Excluded: addresses observed with changed image words or outside the image,',
                  'and known generated code at 32535..32566. Matching sampled words is not',
                  'proof of immutability; inspect the listing and runtime stores before conversion.', '',
                  '| Probe | Address/half | Steps | Observed raw successors |', '|---|---|---:|---|']
        candidates, excluded, excluded_locations = [], Counter(), set()
        for name, counts, edges in profiles:
            for key, count in counts.items():
                address, half = key
                classification = candidate_class(address, modified, listing)
                if classification != 'image-matching':
                    excluded[name] += count
                    excluded_locations.add((address, half, classification))
                    continue
                successors = sorted({dst for (src, dst) in edges if src == key})
                candidates.append((count, name, address, half, successors))
        for count, name, address, half, successors in sorted(candidates, reverse=True):
            lines.append(f'| {name} | {address:05o}{half} | {count} | ' +
                         ', '.join(f'{a:05o}{h}' for a, h in successors) + ' |')
        totals, triggers = Counter(), {}
        for count, name, address, half, successors in candidates:
            totals[address] += count
            triggers.setdefault(address, set()).add(name)
        groups = []
        for address in sorted(totals):
            if not groups or address != groups[-1][-1] + 1:
                groups.append([])
            groups[-1].append(address)
        lines += ['', '## Ranked contiguous candidate regions', '',
                  '| Region | Fallback steps | Triggering probes |', '|---|---:|---|']
        for group in sorted(groups, key=lambda g: sum(totals[a] for a in g), reverse=True):
            lines.append(f'| {group[0]:05o}..{group[-1]:05o} | '
                         f'{sum(totals[a] for a in group)} | ' +
                         ', '.join(sorted(set.union(*(triggers[a] for a in group)))) + ' |')
        lines += ['', 'Excluded generated/modified or unclassified-listing fallback steps: `'
                  + json.dumps(excluded) + '`.', '']
        lines += ['Excluded locations (not ranked as static conversion candidates):', '',
                  '| Address/half | Classification |', '|---|---|']
        lines += [f'| {a:05o}{h} | {classification} |'
                  for a, h, classification in sorted(excluded_locations)]
        image_delta = sorted((a, h) for a, h in delta
                             if candidate_class(a, modified, listing) == 'image-matching')
        lines += [f'New image-matching, listing-decoded halves: {len(image_delta)}.',
                  '`' + ' '.join(f'{a:05o}{h}' for a, h in image_delta) + '`', '']
        for address in (0o12324, 0o12404):
            lines.append(f'- `{address:05o}` reached by probes: ' +
                         ('yes' if (address, 'L') in expanded else 'no'))
    lines += ['', 'Known diagnostic mismatches are experiments, not passing regressions.',
              'EOF-only probes establish clean input suspension, not a missing-END diagnostic.',
              'No output consumer or CLOSE on a disk-output constructor is invoked.',
              'JUMPOUT/device operation 100 and disk-output transfers remain excluded.', '']
    report = args.report
    if report is None and not args.regression:
        report = ROOT / 'docs/reports/coverage-expansion.md'
    if report is None:
        print('\n'.join(lines))
    else:
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text('\n'.join(lines))
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
