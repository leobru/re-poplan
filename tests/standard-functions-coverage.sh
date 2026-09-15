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
