# Native nested functions and historical rebinding

The historical behavior documented below remains the default native baseline.
The [selectable closure experiment](native-closure-experiment.md) supersedes bug
reproduction as the ultimate goal; exact native exhaustion work is deferred.

The ordinary nested-function milestone of step 4 is implemented. Nested
`FUNCTI` definitions compile to stable native function objects. An enclosing
program retains each child descriptor and loads it at the declaration's position,
then assigns the child's name. Repeated outer calls share that descriptor.
Execution continues through the existing explicit POP frame loop.

This is historical dynamic binding: an escaped child reads the binding active
when it is called. There is no saved lexical activation snapshot. The documented
Man-or-Boy rebinding defect consequently occurs through the ordinary binding
mechanism, without recognizing a test program or imposing a special cutoff.

## Evidence and implementation matrix

| Behavior | Reference evidence | Native implementation / checks |
| --- | --- | --- |
| Nested compiler context | `04074..04103` saves and resets compiler context; `04054..04070` restores it | Explicit enclosing-program/definition vector; reader suspension preserves every context |
| Descriptor creation and installation | `17160/17161` emit capture-load and binding-store nodes through `04447`; finalizer selectors `05062/05057` | `captured_value` loads a program root by index, followed by `store`. No generated instructions are inspected or executed |
| Retained child descriptor | `05026/05030`, `05160`: binding/capture chains copied into environment fields 3/4 | Existing compatibility allocation charges the parent capture table; native root traversal retains the child's program/storage |
| Nested name is a local binding | Raw OUTER binding table contains formal X and local B; `03206..03234` saves values/classes and initializes B to UNDEF | Enclosing program adds B to its ordered binding table; B is installed only when execution reaches its declaration |
| Sharing and dynamic lookup | Historical `OUTER(12)` and `OUTER(34)` return equal functions; a separately compiled OTHER returns a different function | Stable handles; escaped calls see restored X, or a caller's active X; mutation changes that shared binding |
| Normal return / SETPOP | `03235..03260` normal restoration; historical SETPOP probes leave active X and B | Existing explicit-frame restoration and recovery apply unchanged to nested calls |
| Rebinding failure | Reduced three-argument Man-or-Boy and `docs/original-structure.md` | Same B descriptor repeatedly reads the currently active K: 1, 0, -1, -2, -3, -4 in the bounded fixture |
| Collection | Reference retains descriptors in the parent's capture table | Suspended compilation roots children; escaped child survives parent collection; completed/abandoned compiler contexts release roots |

The source trace for a simple nested definition visits `04447` with selector
`010` from `17160`, then selector `005` from `17161`; the finalizer visits
`05062` and `05057`. These are source IR selectors and provenance, not native
execution PCs. The raw binding probe's parent capture table has a header and one
child descriptor; the child has no binding table when it has no formals/locals.
Exploratory traces and descriptor dumps are in `artifacts/native-nested/`.

## Regression coverage

- `native-nested.pop2`: initialization timing, dynamic lookup from an independent
  callee, captured mutation, escaped functions, identity/sharing, three nested
  definition levels, shadowed formals, recursive triangular calculation, and
  restoration of the prior global nested-name binding.
- `native-nested-recovery.pop2`: nested SETPOP preserves active bindings and
  aliases, including a later indirect call through the escaped function.
- `native-rebinding.pop2`: reduced three-argument Man-or-Boy prints the historical
  decrement sequence. A counter and SETPOP **in the POP test input** stop it after
  six calls; the runtime contains no fixture-specific guard.
- `native-man-or-boy.pop2`: canonical five-functional-argument calls for K=0..3
  produce 1, 0, -2, 0, matching historical POPLAN.

All four fixtures compare native, hybrid, instruction-only and historical output.
Native runs also use the machine/decoder isolation guard from an empty directory.
Every byte split checks output, counts, allocation scheduling, dictionary
identities, values and classes; final dictionary checkpoints compare with the
instruction-only backend. Additional tests force collection during suspended
nested compilation, collect an outer function while its escaped child remains
callable, reclaim the child after its last aliases disappear, and abandon an
unfinished outer definition at EOF.

Native operation counts: 274 (`native-nested`), 40 (`native-nested-recovery`),
168 (`native-rebinding`), 208 (`native-man-or-boy`). Earlier measurements remain
unchanged. Implicit returns and reader/compiler work do not count as operations.

## Remaining boundary

Step 4's ordinary nested syntax, sharing, mutation and rebinding behavior are
covered. Its exact exhaustion acceptance is still open and joins step 5's
recursion accounting work. Unbounded reduced K=2 and canonical K=4 runs reach the
native activation limit with an explicit development failure; they do **not** yet
claim the historical 13000 diagnostic or threshold. `legacy_accounting_complete`
remains false. Reader/compiler scheduling and primitive scratch costs are still
incomplete.

Indirect calls through ordinary word/formal bindings are covered; arbitrary
computed callee expressions remain unsupported. Nested declarations inside
conditional bodies, duplicate nested-name bindings, result parameters, LAMBDA,
special functions, updaters, and general compound error rendering also remain
unsupported. Unsupported paths never fall back to the
machine backend. No hybrid dispatch entry or historical instruction count changes.

Validation logs are in `artifacts/native-nested/`, outside the build area.

Validation completed on 2026-10-07: all 19 regular CTests passed (80.35 s,
through `make test`), and all 19 ASan/UBSan CTests passed (389.07 s,
`ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`). Smoke tests,
`make cpp-quine`, `make cpp-zone1224`, all native-fixture historical comparisons,
separate historical trivial/full-zone1224 comparisons, image-free execution
guards and whitespace checks passed. Verification includes the pre-existing
uncommitted machine shadow-state edits; those edits are excluded from this
milestone commit. Exit-status files accompany the saved logs.
