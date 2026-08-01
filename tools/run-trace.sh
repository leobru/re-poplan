#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 INPUT [OUTPUT [TRACE [COVERAGE]]]" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
input=$1
output=${2:-"$root/build/$(basename "$input").out"}
trace=${3:-"$root/build/$(basename "$input").trace"}
coverage=${4:-"$root/build/$(basename "$input").cov"}
runner=${DISPAK_RUNNER:-"$root/tools/run-dispak.sh"}
timeout_seconds=${TIMEOUT_SECONDS:-10}

if ! command -v "$runner" >/dev/null 2>&1; then
    echo "error: POPLAN runner not found: $runner" >&2
    exit 1
fi

mkdir -p "$(dirname -- "$output")"
mkdir -p "$(dirname -- "$trace")"
mkdir -p "$(dirname -- "$coverage")"

timeout "$timeout_seconds" "$runner" \
    --bootstrap \
    -t -t \
    --coverage="$coverage" \
    "$root/poplan.b6" \
    < "$input" \
    > "$output" \
    2> "$trace"

echo "Output:   $output"
echo "Trace:    $trace"
echo "Coverage: $coverage"
