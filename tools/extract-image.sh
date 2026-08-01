#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
output=${1:-"$root/build/poplan.bin"}
besmtool=${BESMTOOL:-besmtool}
expected_size=92160

if ! command -v "$besmtool" >/dev/null 2>&1; then
    echo "error: besmtool not found: $besmtool" >&2
    exit 1
fi

mkdir -p "$(dirname -- "$output")"
temporary="$output.tmp.$$"
trap 'rm -f "$temporary"' EXIT HUP INT TERM

"$besmtool" dump 2148 \
    --start=01201 \
    --length=017 \
    --to-file="$temporary"

actual_size=$(wc -c < "$temporary")
if [ "$actual_size" -ne "$expected_size" ]; then
    echo "error: expected $expected_size bytes, got $actual_size" >&2
    exit 1
fi

mv "$temporary" "$output"
trap - EXIT HUP INT TERM

echo "Wrote $output"
sha256sum "$output"

