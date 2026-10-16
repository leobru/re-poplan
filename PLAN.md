# Standalone modern POPLAN

**Current objective (2026-10-13):** create an independent interpreter under
`standalone/`, using separately tagged int64 and IEEE binary64 values, native
objects and corrected captured cells. This subproject may add its own C++ files.
Preserve both existing backends and their earlier measurements.

The initial standalone implementation runs the language/standard-function
fixtures, quine, Man-or-Boy K=10, recovered example library and fixed-seed game.
It is **not yet language-complete**. Build with `make standalone-test`.
[Architecture, tests and remaining gaps](standalone/docs/status.md) are the
current handoff. Next steps, in order:

1. **Implemented, audits remain:** resumable compiler/readers, incremental CLI,
   authoritative PROGLIST/NEXTCH, direct token values, macro and supplier timing.
2. **Implemented, audits remain:** lazy functional lists, mutation/sharing,
   traversal and callback continuations; iterative object formatting.
3. **In progress:** computed calls, REFOF, BOUNDSLIST updates, updater identity,
   protected functions, character forms, CANCEL binding identity and error
   callbacks/reset are implemented. LENGTH and DATALENGTH now follow their
   distinct list/strip contracts, with type and lazy-supplier tests. APPDATA shares
   DATALIST component extraction; COPY rejects interned words and functions.
   UPDATER supports arbitrary elements and explicit clearing; partial updaters
   capture independent arguments, and compositions require explicit updaters. The callback
   mutation difference is recorded in the status matrix. SECTION/ENDSEC, SWITCH and full mutable
   syntax/special functions remain unresolved.
4. **In progress:** [184-resident ledger](standalone/docs/language-inventory.md),
   byte-split/GC/interactive tests and historical semantic probes are in place.
   Complete per-entry branch audits and remaining diagnostic/input contracts
   before claiming full language coverage. Protected resident identifier assignment, declaration, references, local
   shadowing and cancellation are now implemented and tested; syntax identifier
   classification remains open. Update dispatch now rejects
   unpaired primitives before they can execute ordinary side effects.

The closure investigation and historical-baseline plan below remain the record
for the compatibility-native backend; they are not standalone limitations.

---

# Next Five Steps for Native POPLAN

**Current objective (2026-10-12):** use native execution to discover and validate
a correction to closure rebinding. Preserve historical native behavior as a
selectable baseline. Exact exhaustion matching is no longer a prerequisite.

## Closure investigation roadmap

1. Add independent historical/experimental closure selection and relaxed native
   activation limits; keep machine execution unchanged.
2. Associate nested function instances with mutable cells from their defining
   activation, retaining dynamic lookup for unrelated routines.
3. Compare semantic binding checkpoints at the first Man-or-Boy divergence;
   verify captured mutation, restoration, identity, recovery and collection.
4. Require canonical K=0..10 results, including -67 at K=10, and independent
   call/depth checks. Preserve historical tests and run full verification.
5. Use the native evidence to investigate an original-runtime correction;
   do not claim a BESM patch merely because the native experiment succeeds.

The first four items are implemented; see
[the experiment and validation](docs/native-closure-experiment.md).
Five unresolved address-bearing exhaustion diagnostics and broader historical
session accounting remain documented but deferred. Other native language
features remain incomplete.

## Original historical-baseline milestones

The following sequence and dated reports record the work that established the
baseline. Their requirements to preserve the closure failure and exact native
exhaustion are superseded by the investigation objective above. Earlier numerical
measurements remain unchanged.

## Goal and boundary

Build toward faithful functions, recursion, and Man-or-Boy behavior from commit
`3a07cb3`. Native mode currently supports numeric expressions; allocation and
dictionary foundations now belong to the native runtime (see status below).

Keep `--native` opt-in, preserve the double-first real-literal policy, and never
execute BESM instructions or fall back to `Machine`. Add declarations in
`include/poplan/native.hpp` and implementation in existing C++ files only.

## Implementation sequence

### 1. Connect native storage and recover allocation accounting

- Introduce `NativeRuntime` to own the allocator, dictionary, value stack,
  compiled programs, and stable-handle object storage.
- Recover startup reservations, collection/retry behavior, and the allocation
  costs of compiler operations, function descriptors, environments, and
  activations from listing and trace evidence.
- Implement the required collection paths with explicit roots in bindings,
  programs, values, and frames. Preserve historical free-list ordering and
  `COREUSED` accounting.
- Record compiler emission sites as semantic operations with provenance and
  logical costs; never infer operations from finished instruction words.

**Acceptance:** primitive differentials match allocation addresses, free lists,
collection/retry results, and accounting. Unknown costs remain blockers to
claiming exhaustion compatibility.

### 2. Add the reader, bindings, and top-level recovery

- Introduce a resumable `NativeReader`, retaining token boundaries, packed-name
  conversion, six-character significance, and historical operator-word behavior.
- Connect interning to executable bindings. Support `VARS`, variable reads,
  `->`, statement separators, and the stack behavior needed by function bodies.
- Port signed-number/unary handling from the original compiler rather than
  treating signs as conventional arithmetic syntax.
- Replace whole-input probe execution with statement compilation and explicit
  suspension/recovery states. Implement diagnostics for supported syntax and
  unbound bindings, followed by historical SETPOP recovery.

**Acceptance:** declarations, reassignment, name collisions, stack results, EOF,
split input, and recovery match the references. Unsupported facilities still
fail explicitly without machine fallback.

### 3. Implement named functions and conditional execution

- Add typed operations for binding access, assignment, calls, returns,
  comparisons, and conditional branches.
- Support `FUNCTI`, formal parameters, local declarations, `END`, and
  `IF … THEN … ELSE … CLOSE`.
- Introduce explicit `NativeFrame` records containing operation position,
  return continuation, environment references, and binding-restoration state.
- Preserve argument evaluation order, dynamic binding restoration, and
  zero/multiple results. POP recursion and unwinding must not use C++ recursion.
- Route arithmetic primitives and user functions through the same native call
  machinery.

**Acceptance:** constant functions, argument passing, local shadowing,
conditional branches, ordinary recursion, and recovery through nested calls
match semantic checkpoints and printed output.

### 4. Add closures and reproduce historical rebinding

- Implement nested function creation, retained environments, captured-variable
  mutation, function-valued arguments, and indirect calls.
- Preserve sharing and function identity through stable handles.
- Port the binding/rebinding decisions represented by ordinary call binding at
  `03206..03234` and restoration at `03235..03260` at their semantic source.
- Reproduce the documented closure-rebinding failure through environment logic,
  not fixture recognition, recursion cutoffs, or a corrected host closure model.

**Acceptance:** closure checkpoints preserve aliasing, mutations, restoration,
and the documented three-argument Man-or-Boy sequence leading to exhaustion.

### 5. Establish recursion and Man-or-Boy conformance

- Match the existing triangular fixtures' successful values and exact exhaustion
  boundaries, including no-local `124/125`, one-local `82/83`, five-formal
  `34/35`, and nested-function `26/27`.
- Match canonical Man-or-Boy success at `K=3` and historical failure at `K=4`,
  plus the four- and three-argument variants.
- Check the two-argument variant using bounded execution and semantic
  checkpoints rather than a wall-clock termination expectation.
- Update the native status matrix with implemented syntax, allocation evidence,
  remaining branches, and separately reported native operation counts.

**Acceptance:** historical failures remain failures; returning the
mathematically correct `-67` is not the compatibility target.

## Verification and delivery

At each completed step:

- Run focused primitive and semantic-checkpoint differentials, preserving
  sharing and cycles when canonicalizing handles.
- Compare native, hybrid, instruction-only, and historical output through
  `tools/run-dispak.sh`.
- Run native execution without an image through the machine/decoder isolation
  guard.
- Run complete CTest, smoke, quine, zone1224, ASan/UBSan with leak detection
  disabled, and whitespace checks.
- Keep reports outside `build/`, preserve unrelated edits, and record a
  reviewable milestone commit.

General list/string facilities, macros, mutable syntax, generated accessors,
and native POPMESS remain later work unless a listed milestone requires a
specific supporting primitive.

## Step 1 status — 2026-10-02

Implemented runtime ownership, stable reference/pair storage, startup reservation,
root traversal, nonmoving collection, historical free-list reconstruction and
single collection/retry with explicit exhaustion. Numeric execution now uses the
runtime. Focused differentials include both raw retry outcomes and cyclic/shared
graphs; source cost inventory is in `docs/native-allocation.md`.

At this earlier milestone step 1 was **partial**, not an exhaustion-compatibility
gate pass. Aggregate compiler/emission, environment and activation costs were
still unresolved. The completion below supersedes these TODOs. Operations
carry emitter-family provenance and explicitly unknown costs; execution reports
incomplete legacy accounting. Next work must recover the original sizing pass
and its caller-to-operation mapping before charging compiled programs or claiming
recursive thresholds. Frame-root storage does not implement POP call frames.


Milestone verification passed: 17 normal and 17 ASan/UBSan CTests, historical
smoke/expression comparisons, quine, zone1224 (including historical comparison),
native isolation guard and whitespace checks. Logs: `artifacts/native-storage/`.


## Step 1 completion — 2026-10-03

Step 1 is complete at its storage/primitive boundary. Compiler operations now
have concrete source emission sites and word costs, including zero-width labels
and the final return word. Runtime-owned compilation charges literal interning,
intermediate nodes, five-word descriptors, both environment arrays and code
extents in the tested finalizer order. It retains pending roots across collection
and releases intermediate chains with the historical bucket ordering.

Explicit activation accounting now distinguishes retained size, entry peak and
restoration peak, with bounded entry/exit, roots and error unwind. Instruction-only
differentials establish the costs for ordinary binding, argument transfer and
argument rebuilding. The earlier n+1 argument-storage estimate is corrected to
n; the earlier closure source attribution is corrected to `03206..03260`.

See `docs/native-accounting.md` for the complete source/cost inventory and test
scope. This completes the accounting foundation, not the native language: reader
allocation scheduling is step 2; actual calls, binding restoration and closure
semantics are steps 3–4; whole-session exhaustion compatibility is step 5.
`storage_accounting_complete` distinguishes charged code storage from the still
false whole-session `legacy_accounting_complete` flag. No unknown cost is being
silently counted as zero. Next implementation work is step 2.


Completion verification passed: all 18 normal and 18 ASan/UBSan CTests, the full
smoke suite, quine, zone1224 with historical comparison, native isolation guard
and whitespace checks. Logs: `artifacts/native-accounting/`.


## Step 2 semantic milestone — 2026-10-05

The shell now incrementally reads and compiles statements into native operations.
It supports `VARS`, binding reads and assignments, six-character collisions,
Cyrillic packed names, statement separators, multiple stack results, source-faithful
unary/operator reductions, `ERASE()`, and top-level diagnostics/SETPOP recovery.
EOF and arbitrary split input retain compiler state without replaying effects.
Earlier completed statements remain visible when a later facility is unsupported.

[Reader evidence and limits](docs/native-reader.md) records source boundaries,
raw binding/character checkpoints, historical fixtures and the tested empty-stack
quirk. Native integer formatting now uses the full 41-bit signed mantissa.
Reader temporary-object allocation scheduling, full diagnostic controls, deeper
underflow into non-stack memory and unported reader facilities remain open;
this milestone does not establish whole-session exhaustion compatibility.
The next language implementation is named functions and conditionals (step 3).


Milestone verification passed: 19 normal CTests (68.79 seconds), 19 ASan/UBSan
CTests (330.74 seconds, leak detection disabled), `make test`, `make cpp-quine`,
`make cpp-zone1224`, historical trivial/zone1224 comparisons, both native fixtures
through the image-independent isolation guard, and `git diff --check`.
Logs are in `artifacts/native-reader/`. Verification ran with the pre-existing
uncommitted machine shadow-state edits present; those edits are excluded from
this milestone commit.


## Step 3 progress — 2026-10-06

Implemented representation equality, ordered numeric comparisons, and nested
`IF / THEN / ELSEIF / ELSE / CLOSE` as typed POP operations. Conditional compiler
state is resumable; skipped branches do not execute side effects. Historical
truth testing, stack results, output separation and supported diagnostic recovery
are preserved. See [native-control evidence](docs/native-control.md).

The new probes also exposed and corrected the hybrid `06650` multiplicative
sign checks and missing left-operand diagnostic reload, verified against complete
instruction-only machine state.

Step 3 remains **partial**: named functions, formal parameters, local bindings,
explicit call frames, returns, dynamic restoration and recursive execution are
next. Steps 4–5 and exact whole-session allocation remain open.

This conditional milestone passed 19 regular and 19 ASan/UBSan CTests, smoke,
quine, zone1224, historical comparisons, image-free guarded execution and
whitespace checks. Validation and separate native operation counts are recorded
in `docs/native-control.md`; logs remain outside the build area in
`artifacts/native-control/`.


## Step 3 named-function milestone — 2026-10-06

Implemented ordinary named functions, formals, local declarations, END, RETURN,
explicit call frames, dynamic lookup/restoration, recursive calls and zero/multiple
results. Calls share the native POP operation loop with arithmetic and branches.
Function objects own compiled storage and participate in collection, including
cyclic roots. Recovered class-mask handling preserves declaration flags and
normal binding restoration. Historical SETPOP abandons saved bindings: active
values and classes deliberately survive recovery.

See [native-functions.md](docs/native-functions.md) for source evidence, fixtures
and remaining boundaries. Step 3's remaining edge cases include malformed
headers, duplicate/result parameters, nonfunction application and compound
diagnostic rendering. Primitive scratch/session accounting and exact exhaustion
remain open; nested definitions and closure rebinding remain step 4.

This function milestone passed all 19 regular and 19 ASan/UBSan CTests, smoke,
quine, zone1224, historical comparisons, image-free execution guards and
whitespace checks. `docs/native-functions.md` records the exact boundary and
separate operation counts; logs are in `artifacts/native-functions/`.


## Step 4 nested-function milestone — 2026-10-07

Implemented ordinary nested definitions, shared child descriptors, retained
program roots, captured-variable mutation, indirect calls and explicit nested
compiler contexts. Historical probes establish dynamic binding after escape;
repeated outer calls return the same child function identity. The reduced
Man-or-Boy fixture reproduces the 1, 0, -1, -2 sequence through that binding
mechanism. Canonical five-argument K=0..3 results match historical POPLAN.

See [nested-function evidence](docs/native-nested-functions.md) for the status
matrix, source emission sites, collection/suspension checks and remaining syntax
boundaries. Step 4's exact exhaustion acceptance remains open. The next work is
step 5: primitive/session activation accounting, exact triangular and Man-or-Boy
thresholds, and historical exhaustion diagnostics. Unbounded native probes
currently terminate with an explicit development failure at the native limit.

This milestone passed 19 regular and 19 ASan/UBSan CTests, smoke, quine,
zone1224, historical comparisons, native execution guards and whitespace checks.
Separate native operation counts and exact validation measurements are in the
nested-function report; logs are in `artifacts/native-nested/`.


## Step 5 recursion-boundary milestone — 2026-10-10

Implemented the three-word top-level execution context and the addressable
extent of ordinary argument rebuilding for the established two-load comparison
path. The source compiler records staging explicitly; the runtime borrows and
releases its compatibility storage without executing BESM instructions.

The five triangular variants now match historical last-success/first-failure
boundaries, including function-valued fake arguments. Canonical and reduced
Man-or-Boy success/failure pairs and a bounded two-argument decrement sequence
have native/guard/historical tests. See [recursion status](docs/native-recursion.md).

Step 5 remains partial: historical 13000 rendering and SETPOP recovery require
exact failure-phase/generated-address provenance. Other argument scheduling and
primitive scratch paths, whole-session accounting, and exact Man-or-Boy failure
activations remain open. Native exhaustion stays an explicit development failure.

Verification passed: 20 regular and 20 ASan/UBSan CTests, smoke, quine,
zone1224, historical fixtures/boundaries, native machine-isolation guards and
whitespace checks. Exact validation times and exclusions are recorded in the
recursion report; logs remain in `artifacts/native-recursion/`.


## Additional language milestone — 2026-10-10

Implemented historical AND/OR separators in IF and ELSEIF conditions, plus
direct NOT(...) calls. Explicit false/true branch fixups preserve short-circuit
side effects, nested conditionals, tagged-zero truth, and multiple stack results.
These are historical condition separators rather than general Boolean infix
expressions. Bare NOT function values remain explicitly unsupported.

See [Boolean syntax status](docs/native-booleans.md) for source evidence,
reference comparisons, suspension tests and recovery coverage. Step 5's exact
exhaustion rendering/recovery and complete accounting remain open; this language
addition does not change those claims.

Verification passed: 20 regular and 20 ASan/UBSan CTests, smoke, quine,
zone1224, historical comparisons, native isolation/suspension checks and
whitespace checks. Measurements and limitations are recorded in the Boolean
syntax report; logs remain in `artifacts/native-booleans/`.

## Direct numeric primitives — 2026-10-11

Implemented direct INTOF(...) and SIGN(...) calls using native operations and
existing operation-local BESM arithmetic. INTOF preserves negative-fraction
flooring and rejection of integer arguments. SIGN handles integer/real signs and
zero. Nested calls, ordinary function bodies, multiple stack results, diagnostics
and input suspension have focused tests.

See [numeric primitive evidence](docs/native-numeric-functions.md). First-class
resident aliases and complete legacy accounting remain open. This addition does
not change the partial status of step 5.

Verification passed: 20 regular and 20 ASan/UBSan CTests, smoke, quine,
zone1224, historical comparisons, native isolation and input-suspension tests,
and whitespace checks. See the numeric primitive report for measurements;
logs are in `artifacts/native-numeric-functions/`.

## Native type predicates — 2026-10-11

Implemented direct ISINTE, ISREAL and ISFUNC calls. Their native operations
preserve historical numeric tags, function classification, multiple stack
results and incremental input state. ISREAL excludes tagged integers. Six-character
name significance, nested calls, escaped functions and error recovery have
historical fixtures; raw primitive tests cover 126 adapted values/predicates.

See [type-predicate status](docs/native-type-predicates.md). First-class resident
predicate aliases and the outstanding step-5 accounting/exhaustion work remain
open; special-function classification does not implement special-function calls.

Verification passed: 20 regular and 20 ASan/UBSan CTests, smoke, quine,
zone1224, historical comparisons, native isolation/input-suspension tests,
and whitespace checks. Measurements and remaining limitations are in the
predicate report; logs remain in `artifacts/native-type-predicates/`.


## Step 5 failure-state and recovery milestone — 2026-10-12

Corrected ordered-comparison staging and simple call-argument rebuilding. Runtime
rebuild checks now follow historical read-watchpoint phases rather than rejecting
all high-water crossings. Partial binding installation preserves the mutations
made before exhaustion. All eight scoped cases match raw failure phases, binding
classes/values and function sharing. Canonical Man-or-Boy now fails with K=-24;
the three-argument variant with K=-34. Four-argument Man-or-Boy retains K=-28 and
B=UNDEF after its partial binding failure.

Exact 13000 diagnostics and SETPOP recovery are implemented for both nested
triangular failures and four-argument Man-or-Boy, including subsequent arithmetic
and user calls. See [exhaustion evidence](docs/native-exhaustion.md).

Step 5 is still not complete: three generated-continuation diagnostics and two
function-descriptor diagnostics need exact compiler layout. Reader/compiler
lifetimes, computed argument scheduling and remaining primitive scratch/access
phases also remain open. No guessed address or zero-cost assumption closes these
gaps; `legacy_accounting_complete` remains false.

Verification passed: 21 regular and 21 ASan/UBSan CTests, smoke, quine,
zone1224, historical fixture/boundary/recovery comparisons, native isolation,
and whitespace checks. Logs and checkpoints remain in `artifacts/native-step5/`.
The five unresolved address-bearing failures are not counted as exact diagnostic
or recovery passes.


## Closure correction experiment — 2026-10-12

Implemented the selectable activation-associated environment experiment and
independent relaxed activation limit. Canonical Man-or-Boy K=0..10 now matches
the correct-closure reference in experimental mode, including -67 at K=10,
1,750 calls and maximum nesting 1,024. Historical mode remains the default.
The reduced trace isolates installation of the outer K cell containing 1 after
the inner activation reached 0. Mutation persists through shared cells and
normal restoration; SETPOP retains installed cells. Collection handles escaped
instances, shared compiled bodies and unreachable environment cycles.

Verification passed: 23 regular CTests and smoke, 23 ASan/UBSan CTests
(600.12 seconds, leak detection disabled), historical comparisons, quine,
zone1224, image-free isolation guards and whitespace checks. Expanded focused
checks include shadowing, sibling captures, input suspension, deep recursion,
recovery and the independent host reference. See
[the experiment report](docs/native-closure-experiment.md) for limits and evidence.
The next investigation is an original-runtime representation/correction, not
completion of the deferred historical native exhaustion diagnostics.
