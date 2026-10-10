# Native named functions and explicit frames

The ordinary named-function portion of step 3 is implemented. It uses the same
POP operation loop as arithmetic and conditionals, with explicit `NativeFrame`
records for user calls. There is no native BESM execution, hardware register bank,
C++ recursion for POP calls, or reference-backend fallback.

## Implemented behavior and evidence

| Facility | Evidence | Native behavior |
| --- | --- | --- |
| `FUNCTI` / `FUNCTION`, formals, local `VARS`, `END` | Six-character resident name `02354`; function compiler family `04001..04116`; historical function fixtures | Resumable header/body compiler state; publish the completed function at END; declarations warn on redefinition |
| Function storage | Finalizer `05021..05147`, environment copies `05025/05160` | Stable function handle owns its five-word descriptor, binding table, POP operations and code-word accounting. Implicit final return uses the finalizer's existing one-word seed |
| Named calls | Binding-call emission `05064`; ordinary entry `03206..03234`, empty-environment entry `03261` | Arguments execute left-to-right; formals bind from the POP stack in reverse order. Extra values remain on the shared stack |
| Locals and dynamic scope | `03217..03223` | Save previous bindings/classes, initialize locals to UNDEF, expose active bindings to callees |
| Return and restoration | `03235..03260`, return emission `05060` | Forward binding-table restoration on explicit RETURN or body completion; preserve zero/multiple stack results |
| Variable classification | Class mask at `03265`, exchanges at `03221..03222` and `03253..03255` | Change only masked class bits; preserve dictionary links and declaration flags. Newly introduced formals/locals keep their previous classification outside normal calls |
| Recovery | Historical nested SETPOP/type-error fixtures | Discard frames and saved bindings without normal restoration. Active values and classes remain visible after recovery |
| Function identity | Historical aliases/redefinition fixture | Aliases keep the original function; redefining a name creates a distinct stable handle. Bare function printing yields FUNCTI |

Frames contain a program identifier, operation position, function reference,
saved bindings and saved classes. The caller frame's position is its return
continuation. Calls and returns advance this explicit frame vector. Native
activation accounting charges the recovered binding-frame retained/peak costs;
root sets include the active function and saved values.

Function-owned programs are not permanent roots. Collection follows their
operations' roots and compiled storage only when a function is reachable.
Unreachable cyclic function/reference graphs release their program metadata and
logical storage. A new function's pending compilation remains rooted during
allocation/collection before its function handle is published.

## Validation boundary

`native-functions.pop2` covers constants, empty functions, formal ordering, local
UNDEF initialization, dynamic lookup, shadowing/restoration, explicit return,
multiple results, extra arguments, side-effecting argument evaluation, ordinary
recursion, function aliases, identity and redefinition. `native-functions-recovery`
checks nested diagnostics, SETPOP, and retained classes/values of formals and
locals, including names that were previously unclassified.

Both fixtures run natively without an image under the machine-execution guard,
and compare with hybrid, instruction-only and historical POPLAN. Every byte
split checks output, operation counts, native allocation outcomes, identities,
classes and bindings. Independent raw sessions supply final binding/class
checkpoints. Additional C++ checks cover active-frame roots, cyclic reclamation,
execution after collection, and EOF during an unfinished redefinition.

Step 3 is still bounded: malformed function headers, duplicate formals/locals,
result-parameter syntax, nonfunction application, and compound diagnostic
rendering remain explicit development failures. At this milestone nested definitions/closures were still open. The subsequent
[nested-function milestone](native-nested-functions.md) implements ordinary
nesting and historical shared rebinding. LAMBDA, special functions and updaters
remain later work.

Whole-session accounting remains incomplete: reader temporaries, compiler
optimization scheduling, primitive call scratch and exact recursive exhaustion
still require conformance work. The existing activation limit gives a bounded
native failure, not a claim of historical threshold/error compatibility.
`legacy_accounting_complete` remains false. This earlier milestone made no
Man-or-Boy claim; the nested-function report records the later K=0..3 results.

Logs and experimental output belong in `artifacts/native-functions/`.
Historical machine-step measurements remain unchanged; native operation counts
are reported separately.

The function fixture executes 695 native operations; the recovery fixture
executes 41. Calls and explicit RETURN operations count, while implicit body-end
returns and reader/compiler work do not. Existing earlier measurements are
unchanged.

Validation completed on 2026-10-06: all 19 regular CTests passed (74.49 s,
through `make test`), and all 19 ASan/UBSan CTests passed (370.24 s,
`ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`). Smoke tests,
`make cpp-quine`, `make cpp-zone1224`, historical comparisons of all native
fixtures, separate historical trivial/full-zone1224 comparisons, and whitespace
checks passed. Exit-status files and logs are retained outside the build area.
