#!/usr/bin/env python3
"""Compare saved historical/standalone workload results, allowing modern formatting."""
import math
import pathlib
import re
import sys

reports = pathlib.Path(sys.argv[1])
aliases = str.maketrans('АВЕКМНОРСТУХ', 'ABEKMHOPCTYX')

def read(name, backend):
    return (reports / f'{name}-{backend}.out').read_text().translate(aliases)

def rows(text):
    result = []
    for line in text.splitlines():
        if '**' not in line:
            continue
        tail = line.split('**', 1)[1]
        if tail.strip() and re.fullmatch(r'[\s\d.,+\-\[\]Ee]+', tail):
            result.append([float(number) for number in re.findall(r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?', tail)])
    return result

historical = rows(read('zone1224', 'historical'))
modern = rows(read('zone1224', 'standalone'))
assert len(historical) == len(modern) and len(modern) > 5, (historical, modern)
for left, right in zip(historical, modern):
    assert len(left) == len(right), (left, right)
    assert all(math.isclose(a,b,rel_tol=1e-7,abs_tol=1e-7) for a,b in zip(left,right)), (left,right)
print(f'Example library: {len(modern)} numeric result rows agree within historical print precision.')

def boards(text):
    result = []
    for line in text.splitlines():
        line = line.lstrip(': ')
        if re.fullmatch(r'[0-3]\s+[.XO\s]+', line):
            result.append(''.join(line.split()))
    return result

left=read('game','historical')
right=read('game','standalone')
assert boards(left)==boards(right) and len(boards(right))==12, (boards(left),boards(right))
for text in [left,right]:
    compact=''.join(text.split()).replace(':','')
    assert 'MYMOVE003' in compact and 'MYMOVE333' in compact
    assert 'BACKTOPOP2THEN' in compact
print('Fixed-seed game: all 12 board rows, both computer moves and normal exit agree.')
