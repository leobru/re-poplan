#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 POPLAN IMAGE SOURCE_ROOT" >&2
    exit 2
fi

poplan=$1
image=$2
root=$3
temporary=$(mktemp -d "${TMPDIR:-/tmp}/re-poplan-standard-functions.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

input="$root/tests/inputs/standard-functions-coverage.pop2"
expected="$root/tests/expected/standard-functions-coverage.out"

"$poplan" --image "$image" < "$input" > "$temporary/hybrid.out"
POPLAN_INTERPRET_ONLY=1 "$poplan" --image "$image" \
    < "$input" > "$temporary/interpreted.out"

for mode in hybrid interpreted; do
    "$root/tools/normalize-output.py" "$temporary/$mode.out" \
        > "$temporary/$mode.normalized"
    diff -u "$expected" "$temporary/$mode.normalized"
done

# Profile the new successful timer, reader and device-constructor workload.
POPLAN_CPU_TRACE=1 POPLAN_ROUTINE_TRACE=1 "$poplan" --image "$image" \
    < "$input" > "$temporary/frontier.out" 2> "$temporary/frontier.trace"
python3 - "$temporary/frontier.trace" <<'PY'
import re
import sys
from pathlib import Path

lines = Path(sys.argv[1]).read_text().splitlines()
regions = ((0o6476, 0o6502), (0o10235, 0o10247), (0o7514, 0o7542),
           (0o12304, 0o12323), (0o12417, 0o12423),
           (0o12433, 0o12443), (0o12512, 0o12516))
seen = set()
for index, line in enumerate(lines):
    match = re.match(r"^([0-7]{5})[LR] ", line)
    if not match:
        continue
    address = int(match[1], 8)
    for lo, hi in regions:
        if lo <= address <= hi:
            seen.add(lo)
            assert index + 1 < len(lines)
            assert lines[index + 1].startswith("ROUTINE " + match[1]), line
assert seen == {lo for lo, hi in regions}, seen
PY

# Trace the diagnostic workload separately: generated code and the guarded
# PRSTRI prologue are allowed, but these converted static clusters are not.
printf 'NUMBERREAD()=>\nABC\n' | \
    POPLAN_CPU_TRACE=1 POPLAN_ROUTINE_TRACE=1 "$poplan" --image "$image" \
    > "$temporary/diagnostic.out" 2> "$temporary/diagnostic.trace"
python3 - "$temporary/diagnostic.trace" <<'PY'
import re
import sys
from pathlib import Path

lines = Path(sys.argv[1]).read_text().splitlines()
regions = ((0o3101, 0o3152), (0o7622, 0o7645), (0o10002, 0o10017))
seen = set()
for index, line in enumerate(lines):
    match = re.match(r"^([0-7]{5})[LR] ", line)
    if not match:
        continue
    address = int(match[1], 8)
    for lo, hi in regions:
        if lo <= address <= hi:
            seen.add(lo)
            assert index + 1 < len(lines)
            assert lines[index + 1].startswith("ROUTINE " + match[1]), line
assert seen == {lo for lo, hi in regions}, seen
PY

# Invalid tokens and EOF deliberately exercise NUMBERREAD's diagnostic path,
# separately from the successful fixture and its static-code profile.
for token in ABC ''; do
    printf 'NUMBERREAD()=>\n%s\n' "$token" > "$temporary/invalid.pop2"
    "$poplan" --image "$image" < "$temporary/invalid.pop2" \
        > "$temporary/invalid.hybrid"
    POPLAN_INTERPRET_ONLY=1 "$poplan" --image "$image" \
        < "$temporary/invalid.pop2" > "$temporary/invalid.raw"
    for mode in hybrid raw; do
        "$root/tools/normalize-output.py" "$temporary/invalid.$mode" \
            > "$temporary/invalid.$mode.normalized"
    done
    diff -u "$temporary/invalid.raw.normalized" \
        "$temporary/invalid.hybrid.normalized"
    test "$token" = '' || grep -q '05010' "$temporary/invalid.hybrid.normalized"
done
