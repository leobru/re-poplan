# Native recursion accounting: step 5 progress

Native execution now reproduces the measured triangular success/failure
boundaries. Step 5 is **not complete**: failures still report an explicit native
development limitation instead of claiming historical diagnostic/recovery
conformance. Full compiler argument scheduling and primitive scratch accounting
also remain open.

## Recovered accounting

The missing charges were not a POP recursion counter. Instruction-only
checkpoints at `01025 -> 02750` enter a completed top-level compilation with
r17=`066002`; the ordinary `03206/03261` header leaves r17=`066004` before the
first user call. Relative to the accounting origin `066001`, this contributes
three retained words. Native execution now owns and unwinds that context.

The straight-line comparison of two loaded values stages them before the
ordinary `20124` argument rebuild. The compiler records this source decision
when its two operands are adjacent literal/binding-load operations. It adds
`05067` to the existing compilation storage inventory, and the comparison
carries a two-argument rebuild annotation. Calls and computed operands are not
assigned an inferred staging cost.

`native_argument_frame_cost(2)` previously established six words as the
rebuild's **stack-pointer** peak. That is insufficient to establish the range
of addressable storage. The historical failing nested trace has:

```
20127: its 16               r17 advances to 066777
20131: its 17               r17 advances to 067000
20132: arx (17)             acc=0000000000067000 r17=067000
20565: uj 20150
```

The access at `20132` triggers the `067000` watchpoint. Thus ordinary argument
rebuilding requires one additional addressable word beyond its pointer-extent
count: n+5, or seven words for two operands. The new helper keeps that requirement
separate from the already tested n+4 pointer-peak cost. It does not change the
original frame-cost API or emulate a hardware stack.

Native execution borrows that temporary extent before performing the comparison.
Failure leaves the pending frame ledger unchanged; unwinding releases the
execution context and user frames while retaining active POP bindings, as before.
The POP operation count is unchanged by these accounting annotations. No native
execution path calls Machine, decodes instructions, or recognizes instruction
words.

## Status matrix

| Case | Historical / native last success | First failure | Status |
| --- | --- | --- | --- |
| Triangular, one formal | 124: 7750 | 125 | Boundary matches |
| Triangular, one unused local | 82: 3403 | 83 | Boundary matches |
| Triangular, six formals | 34: 595 | 35 | Boundary matches |
| Nested triangular, numeric fake arguments | 26: 351 | 27 | Boundary matches |
| Nested triangular, function fake arguments | 26: 351 | 27 | Boundary matches |
| Canonical five-functional-argument Man-or-Boy | K=3: 0 | K=4 | Success/failure agrees; exact failing activation/diagnostic open |
| Four-functional-argument variant | K=2: 0 | K=3 | Success/failure agrees; exact failing activation/diagnostic open |
| Three-functional-argument variant | K=1: 0 | K=2 | Success/failure agrees; exact failing activation/diagnostic open |
| Two-functional-argument variant, K=1 | Counter-bounded sequence 0,-1,-2,-3,-4,-5 | Test input calls SETPOP after six B calls | Checkpoints agree; no timeout or runtime fixture cutoff |

`tests/native-recursion.py` runs both native executables from an image-free
working directory, including the machine/decoder guard. Successful triangular
values also compare with hybrid and instruction-only output. Its `--historical`
mode independently verifies the success values and historical 13000 failures
through `tools/run-dispak.sh`. Exact failing output is deliberately not asserted
as equal. The normal CTest entry does not require historical execution.

C++ tests check rebuild addressability at the boundary and verify that failing
triangular runs release all accounting frames while leaving N bound to zero.
The existing every-byte suspension, dictionary, collection and native fixtures
continue to run. Historical measurements and earlier operation counts remain
unchanged.

## Remaining work

Historical error 13000 can print a generated continuation address (for example
`065607` in the no-local probe) or the collision address `067000`. Exact logical
compiler layout and failure-phase provenance are needed to render that value
faithfully. Native currently stops with an explicit development error and does
not perform the historical SETPOP recovery for exhaustion.

Only the established two-load comparison staging path is annotated. Transfer
areas across computed arguments, other optimizer branches, arithmetic/printing
scratch, reader/compiler lifetimes, and precise Man-or-Boy failure activations
remain to be ported. Binding-entry overflow is still checked before changing
that frame; historical failures inside a partially installed binding table need
separate checkpoints and phase-by-phase unwinding. `legacy_accounting_complete` stays false; passing these
boundary probes is not whole-runtime allocation conformance.

Logs and exploratory source/checkpoints are in `artifacts/native-recursion/`.

Validation completed on 2026-10-10: all 20 regular CTests passed (80.63 s,
through `make test`) and all 20 ASan/UBSan CTests passed (425.66 s,
`ASAN_OPTIONS=detect_leaks=0`, `UBSAN_OPTIONS=halt_on_error=1`). Smoke,
`make cpp-quine`, `make cpp-zone1224`, historical native-fixture/boundary
comparisons, separate historical trivial/full-zone1224 comparisons and whitespace
checks passed. Exact exhaustion output is an explicitly excluded conformance
claim, as described above. Verification includes the pre-existing machine
shadow-state edits; this work does not change them.
