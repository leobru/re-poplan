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
