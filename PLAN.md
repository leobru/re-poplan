# Next Five Steps for Native POPLAN

**Current status:** step 2 reader/binding/recovery semantic milestone implemented;
next is step 3. Whole-session allocation fidelity remains an explicit open gate.

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
