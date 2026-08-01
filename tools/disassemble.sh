#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 IMAGE [TRACE]" >&2
    exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=$1
trace=${2:-}
disassembler=${DISBESM6:-disbesm6}

if ! command -v "$disassembler" >/dev/null 2>&1; then
    echo "error: disbesm6 not found: $disassembler" >&2
    exit 1
fi

set -- -a0 -n "$root/poplan.sym"
if [ -n "$trace" ]; then
    set -- "$@" "--trace=$trace"
fi

exec "$disassembler" "$@" "$image"

