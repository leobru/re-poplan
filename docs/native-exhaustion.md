# Native exhaustion phases and recovery — 2026-10-12

The historical behavior documented below remains the default native baseline.
The [selectable closure experiment](native-closure-experiment.md) supersedes bug
reproduction as the ultimate goal; exact native exhaustion work is deferred.

Step 5 remains **partial**. Native execution now matches the failure phase and
surviving binding state of all eight scoped exhaustion cases. Three cases also
produce exact POP diagnostics and SETPOP recovery. Generated continuation and
function-address rendering, full compiler layout, other argument scheduling and
primitive scratch lifetimes remain unfinished.

## Corrected source accounting

The previous high-water check was too conservative for Man-or-Boy. Historical
startup installs a read watchpoint at `067000`; writes and the stack pointer may
cross it before a read triggers the supervisor. DISPAK checks stack operands
before their implicit decrement. Native operations now record these read phases
as offsets from their logical activation accounting origin, not as an emulated
register bank or hardware stack.

For n staged arguments and activation base b (relative to `066001`), ordinary
rebuilding has these ordered checks:

| Source phase | Watched offset | Diagnostic value / status |
| --- | --- | --- |
| `20132`, extent calculation | b+n+4 | `067000`; rendering and recovery implemented |
| `20133`, argument copy, i=0..n-1 | b+i+1 | Previous intermediate value; function-address rendering remains open for scoped failures |
| `20135`, restore | b+n+3 | Last argument; phase identified, general rendering not implemented |
| `20136`, continuation restore | b+n+2 | Generated continuation; exact compiler layout remains required |

`native_rebuild_exhaustion` checks equality at each read in source order. It does
not reject every offset greater than the watched cell. The older frame-cost and
conservative addressability APIs remain separately tested; runtime rebuilding
uses the new phase checks. `NativeExhaustion` records phase, activation extent,
argument count and argument index for semantic checkpoints.

The compiler now distinguishes equality from ordered comparisons of two simple
loads. Equality rebuilds both operands. Ordered comparisons use the specialized
numeric entry (`06623/06631/06637/06645`), then rebuild one Boolean result.
The post-result rebuild is placed after the comparison in both execution and
storage-emission order. Simple literal/binding argument groups of ordinary calls
also carry their source argument count into the `05067` rebuild annotation.
Computed argument scheduling remains explicitly unaccounted.

This corrects canonical Man-or-Boy's failure state from K=-23 to K=-24. No
fixture recognition, recursion counter, machine dispatch, generated-instruction
recognition, or image dependency was introduced.

## Partial binding installation

`03217..03231` saves each old binding, exchanges its class, sets it to UNDEF,
and then installs an argument. Native execution now preserves this ordering.
The temporary reads at `03230..03232` can fail after some slots have changed;
SETPOP retains those changes instead of restoring a snapshot of the whole frame.

The four-argument Man-or-Boy variant fails at `03231` while restoring its binding
loop count. K is -28, local B is already UNDEF, and several function arguments
have already been installed. The diagnostic value is the untagged word 3.
Native execution now prints that value with error 13000, abandons saved bindings,
clears its evaluation/compiler state and accepts subsequent input. Frame-ledger
resources are released even though the active language bindings survive.

## Scoped conformance matrix

| Failure input | Phase and surviving bindings | Exact POP diagnostic and recovery |
| --- | --- | --- |
| Triangular no-local, 125 | Match; N=0 | Open: generated continuation |
| Triangular one-local, 83 | Match; N=0 | Open: generated continuation |
| Triangular five-fakes, 35 | Match; N=0 | Open: generated continuation |
| Nested triangular numeric-fakes, 27 | Match; N=0 | Pass; culprit `067000` |
| Nested triangular function-fakes, 27 | Match; N=0 | Pass; culprit `067000` |
| Canonical Man-or-Boy, K=4 | Match; surviving K=-24; function sharing preserved | Open: function descriptor address |
| Four-argument Man-or-Boy, K=3 | Match; surviving K=-28; B=UNDEF | Pass; culprit untagged 3 |
| Three-argument Man-or-Boy, K=2 | Match; surviving K=-34; function sharing preserved | Open: function descriptor address |

The existing successful boundary values and bounded two-argument sequence remain
covered. `poplan-native-exhaustion-state` adapts the instruction-only backend at
the read-watchpoint boundary; it compares phases, all dynamic word classes and
bindings, and function aliasing in both directions. The C++ reference backend
does not itself implement the supervisor watchpoint, so letting it run past the
boundary is not a valid exhaustion oracle. Historical traces establish the
adapter's stopping points independently.

Native and guarded-native recovery tests then evaluate preserved bindings,
perform arithmetic and call a user function again. Historical comparisons omit
only the supervisor's preceding emulator-instruction trace line; the POP error
code, raw culprit word, formatted value, SETPOP and following outputs match.
The unsupported address-bearing cases retain explicit development failures.

Failing-run native operation counts (not historical instruction counts):
no-local 1255; one-local 835; five-fakes 535; both nested variants 498;
canonical Man-or-Boy 515; four-argument 536; three-argument 583. Earlier saved
measurements are not rewritten.

## Remaining work

Exact logical compiler layout is still needed for three continuation diagnostics
and two function-descriptor diagnostics. This includes reader/compiler temporary
lifetimes and optimizer emission scheduling, not just formatting hexadecimal or
octal handles. Other computed-argument and primitive scratch paths remain open.
Binding-table-address failures and unported activation access phases still fail
explicitly. Whole-session `legacy_accounting_complete` remains false. These
limitations prevent declaring step 5 complete.

Evidence and validation logs are retained in `artifacts/native-step5/`.

Verification completed: all 21 regular CTests passed (87.53 s), and all 21
ASan/UBSan CTests passed (485.16 s, leak detection disabled and UBSan
halt-on-error). Focused reader/accounting/failure-state/recursion tests,
`make test` smoke, quine, zone1224, historical native fixtures and boundaries,
separate historical trivial/full-zone comparisons, and whitespace checks passed.
The exact-address exclusions above remain exclusions; these test results do not
establish whole-session allocation conformance.
