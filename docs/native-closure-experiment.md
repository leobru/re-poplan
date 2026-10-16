# Native closure rebinding experiment

The native backend's ultimate goal is to identify a correction to closure
rebinding. Historical behavior remains the baseline, selectable independently
from relaxed activation limits. Exact native exhaustion matching is deferred.
This experiment changes neither the machine backend nor BESM arithmetic.

## Interface and storage

```
poplan --native --native-closures=experimental < tests/inputs/man-or-boy.pop2
poplan --native --native-activation-limit=128 < tests/inputs/man-or-boy.pop2
poplan --native --native-closures=experimental --native-binding-trace < input.pop2
```

`--native-closures=historical` is the default. With no explicit activation limit
it retains the historical watchpoint/accounting behavior already implemented.
A positive `--native-activation-limit=N` replaces those activation checks with a
limit of N active POP function calls, excluding the top-level frame. Experimental
closures default to 16,384. Reaching the limit produces the explicit development
error `native activation limit exceeded`; it does not pretend to be historical
error 13000. Historical code/dictionary allocation limits still apply. These
options require `--native` and reject machine-only options and controls.

`NativeOptions` passes these choices to the session and runtime. The optional
trace stream records CALL/RETURN activation identifiers, CLOSURE instance and
prototype identifiers, and binding read/write/install/restore events with word
names, stable cell identifiers and values. These are semantic events, not PCs or
machine-step counts. Native object handles and activation identifiers never use
host pointer addresses.

Experimental environments and closure instances occupy host-managed storage;
no historical address or cost is invented for them. Compiled bodies keep their
existing charged storage and are shared among instances. The accounting flags
continue to describe existing compiled storage, not complete experimental memory
usage. `legacy_accounting_complete` remains false.

## Correction and source correspondence

Historically, `17160/17161` install the same compiled child descriptor on every
outer invocation. `03206..03234` installs the callee's ordinary arguments/locals;
`03235..03260` restores previous bindings on return. The native baseline models
this by loading the shared descriptor and using the currently active dictionary.
It does not restore an earlier defining activation before invoking that child.

The experiment retains the source-level nested-definition operation but makes
its execution instantiate the child with the current defining environment. Each
outer invocation owns distinct mutable cells for its formals and locals, including
its nested-function name. Descendants retain enclosing cells; their own formals
shadow those cells. Aliases share one closure identity; different instances of
the same compiled nested function have distinct identities.

A closure call saves the active binding view and installs the retained cells,
then its own fresh arguments and locals. Normal return restores the previous
view using each saved cell's **current value**, not a pre-call snapshot. Thus
captured writes persist. Unrelated functions still use dynamic lookup: a helper
called inside a closure sees its installed bindings. Globals not in the defining
environment remain dynamic. This is an experiment in shallow binding with
retained environments, not a wholesale lexical rewrite.

SETPOP abandons saved views, as before. The currently installed cells remain
rooted and visible to subsequent evaluation. Collection traces active and saved
cells, escaped instances, shared program roots and cyclic environments. Unreachable
self-cycles are reclaimed; losing a compiled prototype does not invalidate live
instances of its body. Collection is also triggered periodically during closure
creation, so a long evaluation need not wait for another compilation.

## The discriminating checkpoint

In `native-rebinding.pop2`, the first A activation has K=2. Its B decrements K to
1 and calls an inner A; inner B decrements its own K from 1 to 0. The base case
then calls the first B again. Historical execution reads the inner K=0 and
continues to -1, -2, etc. Experimental execution installs the **first activation's
same K cell**, still containing 1, and decrements it to 0.

The bounded fixture therefore prints decrement values `1, 0, 0`, returns 1 and
reports COUNT=3. Its historical counterpart still reaches the fixture's explicit
COUNT/SETPOP bound. Tests inspect cell identities at this transition, not merely
the final numerical result.

Canonical K=0..10 results are `1, 0, -2, 0, 1, 0, 1, -1, -10, -30, -67`.
For K=10 the semantic trace must show 1,750 calls, maximum function-call depth
1,024 and balanced returns. The measured count is 15,543 native operations;
this is separate from historical machine steps and does not revise earlier
measurements. These expectations come from the independent
correct-closure reference documented in `original-structure.md`. Enlarging only
the historical activation limit still fails at the requested native limit.
The two-argument variant remains structurally unbounded with either binding
model and is checked with an explicit finite activation budget.

This establishes a sufficient native correction for the tested ordinary nested
functions. It does **not** establish a localized BESM patch: the original image
also needs a representation that distinguishes defining activations. Recovering
how such a representation can fit its descriptor and restoration machinery is
separate work. Special functions, partial application, macros and the unported
language facilities remain outside this experiment.

## Verification

Tests also cover 2,000 levels of triangular recursion, the exact configured
1,023/1,024 activation boundary for canonical K=10, sibling closures, shadowing
and three nested definition levels. The independent host reference tool is run
as part of the comparison.

The two focused CTests cover the CLI/isolation/semantic trace and in-process
storage behavior. Cell tests force collection during every possible input split,
retain escaped instances after their parent/prototype is collected, reclaim
cyclic environments, exercise dynamic helpers, preserve SETPOP cells, and collect
during canonical Man-or-Boy. Existing historical native tests remain unchanged.

Validation results are recorded after completion below; logs and trace evidence
belong in `artifacts/native-closures/`, outside the build area.

Completed 2026-10-12: all 23 regular CTests passed (109.10 seconds), followed
by the complete smoke suite. The expanded focused tests passed again after
adding shadowing/sibling/reduced-variant and activation-boundary assertions.
All 23 ASan/UBSan CTests passed (600.12 seconds), with
`ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`.
`make cpp-quine`, `make cpp-zone1224`, historical native fixture/recursion
comparisons, separate historical trivial/full-zone1224 comparisons and
`git diff --check` passed. Image-free guarded execution covers both closure
modes and rejects machine execution. All command completion statuses were checked.

These runs include the pre-existing uncommitted machine shadow-state edits;
this experiment does not modify them. Earlier status evidence and measurements
are preserved. Logs, the independent reference table, the reduced historical
and experimental binding traces, and the separate native operation count are
in `artifacts/native-closures/`.
