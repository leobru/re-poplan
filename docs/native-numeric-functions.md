# Native INTOF and SIGN — 2026-10-11

Native mode supports direct `INTOF(...)` and `SIGN(...)` calls, including nested
calls, ordinary function bodies, conditional arguments, and multiple stack
results. These use typed POP operations and do not load or execute BESM code.

## Evidence and semantics

| Facility | Historical evidence | Native implementation |
| --- | --- | --- |
| INTOF argument check | Resident `01724`, `03442..03446`, constant `03460` | Accept real words; integers and nonnumbers report `14000`, retaining the offending value |
| INTOF conversion | `03447..03450`, tagged-zero constant `03453` | Operation-local `NumericScratch`, NTR 7, addition of tagged zero; no host integer conversion |
| SIGN | Resident `02144`, `10365..10401`, constants `10452..10453` | Check numeric type, inspect mantissa sign and remaining bits; produce tagged -1, 0 or 1; nonnumbers report `10130` |
| Call compilation | Existing ordinary-call emission family `05064` | Pending argument group lowers directly to `integer_part` or `sign`; annotations are evidence, never execution PCs |

`INTOF(-3.9)` returns -4, `INTOF(-.1)` returns -1, and `INTOF(3)` reports
14000: an integer argument is not accepted even though the result is an integer.
`SIGN` accepts both integer and real numbers, including both zero representations.
Each primitive consumes exactly one argument; preceding stack results survive.
For example, `INTOF(11,2.7)=>` prints `11, 2`.

## Verification and scope

The reader test compares 74 primitive cases against instruction-only execution:
exact numeric result words, diagnostic selection and offending bits, argument
consumption, scratch-stack balance, and preservation of preceding native results.
The inputs include signed integer limits, zero, tiny reals, negative fractions,
and neighboring BESM words around conversion boundaries.

Both new fixtures run under native, guarded native from an image-free directory,
hybrid, instruction-only, and historical POPLAN. Every byte split checks native
output, operation counts, allocation outcomes, dictionary identities and binding
state, with repeated empty feeds to detect replay. Recovery tests cover integer
and UNDEF type errors, SETPOP, nested arithmetic errors, and subsequent execution.
As with earlier recovery fixtures, the instruction-only comparison omits its
missing catalog explanation lines; native and historical output include them.

`native-numeric-functions` executes 118 native operations and
`native-numeric-functions-recovery` executes 25. Earlier measurements are
unchanged; these are POP operation counts, not historical instruction counts.

Bare primitive values, aliases and mutable resident bindings remain unsupported.
Compound-object diagnostic rendering still fails explicitly. Complete primitive
scratch allocation and whole-session exhaustion accounting remain open; this
milestone does not claim completion of those compatibility gates. Other resident
numeric functions, loops and dynamic language facilities remain future work.

Validation logs and historical outputs are retained in
`artifacts/native-numeric-functions/`.

Verification completed: all 20 regular CTests passed (82.00 s), as did all 20
ASan/UBSan CTests (423.30 s, leak detection disabled and UBSan halt-on-error).
The focused reader/probe tests, `make test` smoke suite, `make cpp-quine`,
`make cpp-zone1224`, historical native-fixture comparisons, separate historical
trivial/full-zone1224 comparisons, and `git diff --check` passed. Builds produced
no compiler warnings. Command exit statuses are retained with the logs.
