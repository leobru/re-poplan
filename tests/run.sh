#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
runner=${DISPAK_RUNNER:-"$root/tools/run-dispak.sh"}
timeout_seconds=${TIMEOUT_SECONDS:-10}
temporary=$(mktemp -d "${TMPDIR:-/tmp}/re-poplan-tests.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

run_poplan()
{
    timeout "$timeout_seconds" "$runner" --bootstrap "$root/poplan.b6"
}

if ! command -v "$runner" >/dev/null 2>&1; then
    echo "error: POPLAN runner not found: $runner" >&2
    exit 1
fi

"$root/tools/show-poplib-catalog.py" "$root/poplib.bin" \
    > "$temporary/poplib-catalog.out"
grep -F 'POPLIB  FOURS   0007  0010  9172' \
    "$temporary/poplib-catalog.out" >/dev/null
grep -F 'POPLIB  DEBUG   0011  0011  1168' \
    "$temporary/poplib-catalog.out" >/dev/null
grep -F 'POPLIB  MEMOFN  0012  0012  2115' \
    "$temporary/poplib-catalog.out" >/dev/null
grep -F 'POPLIB  EXAMPL  0013  0013  1727' \
    "$temporary/poplib-catalog.out" >/dev/null

run_poplan < /dev/null > "$temporary/startup.out"
grep -F "ПОПЛАН 2.1" "$temporary/startup.out" >/dev/null

run_poplan < "$root/quine.pop2" > "$temporary/quine.out"
"$root/tools/normalize-output.py" "$temporary/quine.out" \
    > "$temporary/quine.normalized"
diff -u "$root/tests/expected/quine.out" "$temporary/quine.normalized"

run_poplan < "$root/tests/inputs/primitives.pop2" \
    > "$temporary/primitives.out"
"$root/tools/normalize-output.py" "$temporary/primitives.out" \
    > "$temporary/primitives.normalized"
diff -u "$root/tests/expected/primitives.out" \
    "$temporary/primitives.normalized"

run_poplan < "$root/tests/inputs/syntax-error.pop2" \
    > "$temporary/syntax-error.out"
grep -F "****НЕОПР. ИД-Р +*" "$temporary/syntax-error.out" >/dev/null
grep -F "****ОШИБКА 04020" "$temporary/syntax-error.out" >/dev/null
grep -F "НЕТ РАЗДЕЛИТЕЛЯ" "$temporary/syntax-error.out" >/dev/null
grep -F "7200000000016750" "$temporary/syntax-error.out" >/dev/null

run_poplan < "$root/tests/inputs/triangular.pop2" \
    > "$temporary/triangular.out"
"$root/tools/normalize-output.py" "$temporary/triangular.out" \
    > "$temporary/triangular.normalized"
diff -u "$root/tests/expected/triangular.out" \
    "$temporary/triangular.normalized"

sed 's/TRI(10,ZERO,ZERO,ZERO,ZERO,ZERO)=>/TRI(26,ZERO,ZERO,ZERO,ZERO,ZERO)=>/' \
    "$root/tests/inputs/triangular.pop2" \
    | run_poplan > "$temporary/triangular-zero-fakes-26.out"
grep -F '** 351' "$temporary/triangular-zero-fakes-26.out" >/dev/null

sed 's/TRI(10,ZERO,ZERO,ZERO,ZERO,ZERO)=>/TRI(27,ZERO,ZERO,ZERO,ZERO,ZERO)=>/' \
    "$root/tests/inputs/triangular.pop2" \
    | run_poplan > "$temporary/triangular-zero-fakes-27.out"
grep -F 'ОШИБКА 13000' "$temporary/triangular-zero-fakes-27.out" >/dev/null

sed 's/TRI(10,ZERO,ZERO,ZERO,ZERO,ZERO)=>/TRI(26,ZERO,ZERO,ZERO,ZERO,ZERO)=>/' \
    "$root/tests/inputs/triangular-nested-zero-fakes.pop2" \
    | run_poplan > "$temporary/triangular-unpermuted-26.out"
grep -F '** 351' "$temporary/triangular-unpermuted-26.out" >/dev/null

sed 's/TRI(10,ZERO,ZERO,ZERO,ZERO,ZERO)=>/TRI(27,ZERO,ZERO,ZERO,ZERO,ZERO)=>/' \
    "$root/tests/inputs/triangular-nested-zero-fakes.pop2" \
    | run_poplan > "$temporary/triangular-unpermuted-27.out"
grep -F 'ОШИБКА 13000' "$temporary/triangular-unpermuted-27.out" >/dev/null

sed 's/TRI(10,0,0,0,0,0)=>/TRI(26,0,0,0,0,0)=>/' \
    "$root/tests/inputs/triangular-nested-numeric-fakes.pop2" \
    | run_poplan > "$temporary/triangular-numeric-fakes-26.out"
grep -F '** 351' "$temporary/triangular-numeric-fakes-26.out" >/dev/null

sed 's/TRI(10,0,0,0,0,0)=>/TRI(27,0,0,0,0,0)=>/' \
    "$root/tests/inputs/triangular-nested-numeric-fakes.pop2" \
    | run_poplan > "$temporary/triangular-numeric-fakes-27.out"
grep -F 'ОШИБКА 13000' "$temporary/triangular-numeric-fakes-27.out" >/dev/null

sed 's/TRI(10,0,0,0,0,0)=>/TRI(34,0,0,0,0,0)=>/' \
    "$root/tests/inputs/triangular-five-fakes.pop2" \
    | run_poplan > "$temporary/triangular-fakes-34.out"
grep -F '** 595' "$temporary/triangular-fakes-34.out" >/dev/null

sed 's/TRI(10,0,0,0,0,0)=>/TRI(35,0,0,0,0,0)=>/' \
    "$root/tests/inputs/triangular-five-fakes.pop2" \
    | run_poplan > "$temporary/triangular-fakes-35.out"
grep -F 'ОШИБКА 13000' "$temporary/triangular-fakes-35.out" >/dev/null

sed 's/TRI(10)=>/TRI(82)=>/' \
    "$root/tests/inputs/triangular-one-local.pop2" \
    | run_poplan > "$temporary/triangular-local-82.out"
grep -F '** 3403' "$temporary/triangular-local-82.out" >/dev/null

sed 's/TRI(10)=>/TRI(83)=>/' \
    "$root/tests/inputs/triangular-one-local.pop2" \
    | run_poplan > "$temporary/triangular-local-83.out"
grep -F 'ОШИБКА 13000' "$temporary/triangular-local-83.out" >/dev/null

sed 's/TRI(10)=>/TRI(124)=>/' \
    "$root/tests/inputs/triangular-no-local.pop2" \
    | run_poplan > "$temporary/triangular-no-local-124.out"
grep -F '** 7750' "$temporary/triangular-no-local-124.out" >/dev/null

sed 's/TRI(10)=>/TRI(125)=>/' \
    "$root/tests/inputs/triangular-no-local.pop2" \
    | run_poplan > "$temporary/triangular-no-local-125.out"
grep -F 'ОШИБКА 13000' "$temporary/triangular-no-local-125.out" >/dev/null

{
    cat "$root/ttt.pop2"
    printf '%s\n' 'FOURS;' 'YES' '^'
} | run_poplan > "$temporary/ttt.out"
grep -F "ТО ЕNТЕR РRОGRАМ, ТУРЕ  FОURS;" "$temporary/ttt.out" >/dev/null
grep -F "DО УОU КNОW НОW ТО РLАУ АGАINSТ ТНIS РRОGRАМ" \
    "$temporary/ttt.out" >/dev/null
grep -F "DО УОU WАNТ ТО SТАRТ" "$temporary/ttt.out" >/dev/null

sed 's/A(10,/A(3,/' "$root/tests/inputs/man-or-boy.pop2" \
    | run_poplan > "$temporary/man-or-boy-3.out"
grep -F '** 0' "$temporary/man-or-boy-3.out" >/dev/null

sed 's/A(10,/A(4,/' "$root/tests/inputs/man-or-boy.pop2" \
    | run_poplan > "$temporary/man-or-boy-4.out"
grep -F 'ОШИБКА 13000' "$temporary/man-or-boy-4.out" >/dev/null

sed 's/A(10,ONE/A(2,ONE/' "$root/tests/inputs/man-or-boy-four.pop2" \
    | run_poplan > "$temporary/man-or-boy-four-2.out"
grep -F '** 0' "$temporary/man-or-boy-four-2.out" >/dev/null

sed 's/A(10,ONE/A(3,ONE/' "$root/tests/inputs/man-or-boy-four.pop2" \
    | run_poplan > "$temporary/man-or-boy-four-3.out"
grep -F 'ОШИБКА 13000' "$temporary/man-or-boy-four-3.out" >/dev/null

sed 's/A(10,ONE/A(1,ONE/' "$root/tests/inputs/man-or-boy-three.pop2" \
    | run_poplan > "$temporary/man-or-boy-three-1.out"
grep -F '** 0' "$temporary/man-or-boy-three-1.out" >/dev/null

sed 's/A(10,ONE/A(2,ONE/' "$root/tests/inputs/man-or-boy-three.pop2" \
    | run_poplan > "$temporary/man-or-boy-three-2.out"
grep -F 'ОШИБКА 13000' "$temporary/man-or-boy-three-2.out" >/dev/null

run_poplan < "$root/tests/inputs/man-or-boy-three-debug.pop2" \
    > "$temporary/man-or-boy-three-debug.out"
"$root/tools/normalize-output.py" \
    "$temporary/man-or-boy-three-debug.out" \
    > "$temporary/man-or-boy-three-debug.normalized"
sed 's/^:*//' "$temporary/man-or-boy-three-debug.normalized" \
    | grep '^FUNСТI' \
    | head -n 4 \
    > "$temporary/man-or-boy-three-debug-prefix.out"
diff -u "$root/tests/expected/man-or-boy-three-debug-prefix.out" \
    "$temporary/man-or-boy-three-debug-prefix.out"

python3 -m unittest discover -s "$root/tests" -p 'test_*.py'

cmake -S "$root" -B "$root/build/cpp" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$root/build/cpp"
ctest --test-dir "$root/build/cpp" --output-on-failure

echo "POPLAN smoke tests passed"
