#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 POPLAN IMAGE SOURCE_ROOT" >&2
    exit 2
fi

poplan=$1
image=$2
root=$3
temporary=$(mktemp -d "${TMPDIR:-/tmp}/re-poplan-library.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

printf '%s\n' '2+2=>' > "$temporary/demo.pop2"
"$root/tools/make-poplib.py" "$temporary/poplib.bin" \
    --entry POPLIB DEMO "$temporary/demo.pop2" -
test "$(wc -c < "$temporary/poplib.bin")" -eq 49152

printf '%s\n' '[POPLIB DEMO].LIBRARY.COMPILE;' \
    | (cd "$temporary" && "$poplan" --image "$image") \
    > "$temporary/output"
"$root/tools/normalize-output.py" "$temporary/output" \
    > "$temporary/normalized"
grep -F '** 4' "$temporary/normalized" >/dev/null
