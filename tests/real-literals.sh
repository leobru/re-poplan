#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 POPLAN IMAGE SOURCE_ROOT" >&2
    exit 2
fi

poplan=$1
image=$2
root=$3
temporary=$(mktemp -d "${TMPDIR:-/tmp}/re-poplan-reals.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

run_reals()
{
    printf '%s\n' \
        '3.14=>' \
        '.5=>' \
        '3.14$-3=>' \
        '3.14$+2=>' \
        '.5$2=>' \
        '0.0=>' \
        '123456789.25=>' \
        '1.234567890123=>'
}

run_reals | "$poplan" --image "$image" > "$temporary/hybrid.out"
run_reals | POPLAN_INTERPRET_ONLY=1 "$poplan" --image "$image" \
    > "$temporary/interpreted.out"

for mode in hybrid interpreted; do
    "$root/tools/normalize-output.py" "$temporary/$mode.out" \
        > "$temporary/$mode.normalized"
    printf '%s\n' \
        '** 3.1400000' \
        '** .50000000' \
        '** .00314000' \
        '** 314.00000' \
        '** 50.000000' \
        '** .00000000' \
        '** 123456789.2' \
        '** 1.2345679' \
        | diff -u - "$temporary/$mode.normalized"
done
