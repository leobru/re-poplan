# Standalone POPLAN verification — 2026-10-13

This report concerns the new `standalone/` executable, not the compatibility
backend selected by `poplan --native`. The standalone completeness gate remains
open; [known gaps](../../standalone/docs/status.md) are part of this result.

## Completed checks

- `make standalone-test`: both CTests pass. The Python conformance test contains
  54 independent runs; the C++ test checks numeric boundaries, injected services,
  shared captured cells, stable identities, cycle collection and error recovery.
- `make standalone-sanitize`: both CTests pass with ASan/UBSan and
  `ASAN_OPTIONS=detect_leaks=0`.
- A temporary copy of **only** `standalone/` builds in Release mode and passes
  both tests. Installation into another temporary prefix also runs `2+2=>` and
  loads the installed plain-source DEBUG library from outside the source tree.
- Empty-working-directory runs and a linked-symbol guard find no dependency on
  the image, Machine, Word48 or the compatibility-native runtime.
- `ctest --test-dir build/cpp --output-on-failure`: all 23 existing tests pass.
- `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/cpp-sanitize --output-on-failure`: all 23 existing sanitizer tests pass.
- `make test`, `make cpp-quine`, `make cpp-zone1224`: pass.
- `git diff --check`: pass.

Logs and saved historical outputs are in `artifacts/standalone/`, outside the
build area. The source snapshots and standalone expectations are in
`standalone/tests/fixtures/`, so the test suite remains self-contained.

## Historical workload comparison

Historical executions use `tools/run-dispak.sh` with a separate writable home.
The trivial check produces `** 4` on both implementations.

`standalone/tests/compare_workloads.py artifacts/standalone` verifies:

- All 11 numeric result rows of the recovered example library agree within
  `1e-7` relative/absolute tolerance, accounting for historical print precision.
- Fixed-seed tic-tac-toe agrees on both computer moves (`0 0 3`, `3 3 3`), all
  12 printed board rows, and normal return to POP2. The comparison canonicalizes
  historical lookalike glyphs and ignores spacing within board rows.

These are semantic workload comparisons, not byte-for-byte historical output
compatibility. IEEE rounding, shortest real formatting, Unicode characters and
host-service behavior deliberately differ. Man-or-Boy uses corrected closures;
its success is not expected to match the original broken closure implementation.

## New standalone measurement

The canonical K=10 fixture returns **-67**, executes **14,413 native operations**
and reaches frame depth **1,024**. These counts belong to the standalone operation
graph. They do not replace earlier compatibility-native or machine-step counts.
No simulated instructions execute in this binary.

## Coverage continuation — 2026-10-14

The standalone runtime now has resumable compiler/reader tasks, authoritative
PROGLIST/NEXTCH source lists, memoized lazy lists, computed calls, typed POPVAL,
stable identifier records across CANCEL, dynamic REFOF references, and ERRFUN
callback recovery. The [status matrix](../../standalone/docs/status.md) and
[resident ledger](../../standalone/docs/language-inventory.md) record the open
contracts; this is not a language-completeness claim.

The latest focused audit separates LENGTH (manual §7.3, list traversal) from
DATALENGTH (§6.3, strip component count). DATALENGTH rejects records, lists,
words, references, functions and numbers; strings count Unicode characters.
Tests cover custom/empty strips, nested and lazy lists, supplier memoization,
cyclic-list rejection and recovery. Two session-test character suppliers were
corrected to use DATALENGTH for their string inputs.

- `make standalone-test`: all four tests pass; 83 conformance checks.
- `make standalone-sanitize`: all four tests pass with ASan/UBSan and
  leak detection disabled. The initial session run caught the two string LENGTH
  calls above; the corrected suite was rebuilt and rerun successfully.
- Reference CTest: 23/23 pass.
- `make test`, `make cpp-quine`, `make cpp-zone1224`: pass.
- Recovered examples: 11 numeric rows agree with saved historical results within
  historical print precision. Fixed-seed game: 12 board rows, both computer moves
  and normal exit agree.
- A new historical length probe through `tools/run-dispak.sh` gives `3, 4, 2`
  for the string, strip and list examples. Historical/hybrid trivial checks pass.
- Man-or-Boy K=10: **-67**, **14,983 native operations**, maximum frame depth
  **1,122**. These measurements include the new source/reader continuations;
  the earlier measurements above remain unchanged.
- Tracked diff and standalone-file whitespace checks pass.

Evidence is in `artifacts/standalone-coverage/length-*`, outside the build area.

## Component audit — 2026-10-14

APPDATA now uses the same component extraction as DATALIST. In particular,
partial functions expose only frozen arguments, not the underlying callable;
empty partials invoke no callbacks. Word/string traversal produces Unicode
character values, and reference traversal resolves its visible component.
COPY now rejects words, preserving the interning invariant; shallow pair copies
retain their shared tails.

Evidence: manual §§6.1, 7.3, 7.4 and 7.7, plus historical probes saved as
`appdata-partial-historical.out`, `appdata-mutation-historical.out` and
`copy-word-historical.out` in `artifacts/standalone-coverage/`.
The mutation probe exposes a deliberate boundary: the original APPDATA reads
components live between callbacks, while standalone retains the snapshot used
by the manual's DATALIST/APPLIST example. Tests assert that snapshot explicitly.
Other function-class component contracts remain open in the resident ledger.

The four standalone tests pass. Current recovered-library and fixed-seed game
outputs still agree with the saved historical workloads (11 numeric rows,
12 board rows and both computer moves). Validation logs for this increment use
the `components-` prefix under `artifacts/standalone-coverage/`.

Final validation: **88** conformance checks; **4/4** standalone CTests and
**4/4** ASan/UBSan CTests (`detect_leaks=0`); **23/23** reference CTests;
`make test`, `make cpp-quine`, `make cpp-zone1224`, historical/hybrid trivial
checks, and tracked/standalone whitespace checks all pass. No machine backend
implementation changed in this increment.

## Updater and function-copy audit — 2026-10-14

UPDATER assignment now accepts any element, following manual §7.7 and historical
readback probes for integer 17 and UNDEF. A separate explicit-assignment flag
prevents a cleared default updater from being regenerated. Calling an assigned
nonfunction updater raises a recoverable diagnostic; installing a replacement
function works, and standard-function protection remains enforced.

Historical COPY probes reject partial, user-defined and standard functions.
Standalone now does the same, eliminating invalid copies whose cached default
updater retained the original function. The saved probes are
`updater-values-historical.out`, `updater-copy-historical.out` and
`copy-functions-historical.out` under `artifacts/standalone-coverage/`.

All four standalone tests pass, including **94** conformance checks. Recovered
example and fixed-seed game comparisons pass again. The increment's validation
logs use the `updater-` prefix in that artifact directory. The language-completeness
gate remains open, including generated-updater branch audits.

Final checks for this increment: **4/4** standalone sanitizer tests pass with
ASan/UBSan and `detect_leaks=0`; **23/23** reference CTests pass; `make test`,
`make cpp-quine`, `make cpp-zone1224`, historical/hybrid trivial checks and
whitespace checks pass. Existing machine backend implementation is unchanged.

## Generated updater audit — 2026-10-14

Historical probes establish that FNCOMP compositions have no automatic updater,
and partial application creates a separate updater with frozen arguments captured
at creation. Standalone now follows those rules. Changing FROZVAL or FNPART on
the original partial does not retarget its updater; changing a frozen argument
on the updater does not change the original. Explicitly assigned composition
updaters still work.

Saved evidence: `composition-updater-historical.out`,
`partial-updater-frozen-historical.out`, and
`partial-updater-saved-historical.out` in `artifacts/standalone-coverage/`.
The new tests exercise both mutation directions, FNPART replacement, absent
composition updater recovery, and explicit composition updates. Validation logs
use the `paired-` prefix. The remaining language and primitive audit gaps remain
listed in the status matrix; this increment does not close the completeness gate.

Final validation: **99** conformance checks; **4/4** standalone tests and
**4/4** ASan/UBSan tests (`detect_leaks=0`); **23/23** reference CTests;
`make test`, `make cpp-quine`, `make cpp-zone1224`, historical/hybrid trivial
checks, recovered-workload comparisons and whitespace checks all pass.
Man-or-Boy remains **-67**, with **14,983** native operations and maximum frame
depth **1,122**. No existing machine backend implementation changed.

## Update-call dispatch audit — 2026-10-14

Update calls now reject a missing/noncallable updater before invoking an ordinary
primitive or user function. Previously `9->SIN(0)` left `9, 0.0` on the stack,
and `9->DESTREF(R)` could incorrectly change R. Both now report a recoverable
error, matching historical probes. Tests additionally cover printers, identity,
constructors, list/component functions, generated destructors and user-function
bodies, checking that unsupported updates do not execute callee side effects.

Historical evidence is saved as `nonpaired-update-historical.out` and
`destructor-update-historical.out` in `artifacts/standalone-coverage/`.
Validation logs use the `update-dispatch-` prefix. A separate probe found that
historical assignment to ITEMREAD is protected; standalone identifier protection
is now explicitly recorded as an open dictionary gap, distinct from implemented
function-component protection. The probe is saved in
`numberread-callback-historical.out` (it fails at assignment, before NUMBERREAD).

Final validation: **108** conformance checks; **4/4** standalone CTests and
**4/4** ASan/UBSan CTests (`detect_leaks=0`); **23/23** reference tests;
`make test`, `make cpp-quine`, `make cpp-zone1224`, historical/hybrid trivial
checks, recovered example/game comparisons, and whitespace checks all pass.
No machine backend implementation changed in this increment.

## Protected identifiers — 2026-10-15

Resident function bindings and standard value constants now carry protection
on their identifier records. CUCHIN, CUCHOUT and ERRFUN remain writable, as
established by their class-001 dictionary properties; resident functions use
class 040. Direct assignment, top-level declaration and FORALL assignment are
checked during compilation. VALOF/MEANING updates and REFOF also reject protected
global identifiers. Function arguments, locals and captured cells may shadow
them. CANCEL detaches the protected record and permits a new writable binding;
previously compiled functions keep their original identifier.

Historical probes are saved as `protection-*-historical.out` under
`artifacts/standalone-coverage/`. They cover declaration, cancellation, VALOF,
REFOF, local arguments, mutable ERRFUN and FORALL. The status matrix now marks
ordinary resident identifier protection as implemented; full syntax-identifier
classification remains open.

The final ordinary suite passes **4/4** CTests and **116** conformance checks.
The reference suite passes **23/23**, and `make test`, `make cpp-quine`,
`make cpp-zone1224`, historical/hybrid trivial checks, recovered example/game
comparisons and whitespace checks pass. An early sanitizer executable predating
the FORALL guard failed the newly added loop case; the final-code sanitizer run
is recorded separately in `protection-sanitize-final.log`.

The rebuilt final-code ASan/UBSan suite passes **4/4** tests with
`ASAN_OPTIONS=detect_leaks=0` (final exit status 0). No existing machine backend
implementation changed in this increment.

## Malformed UTF-8 timing — 2026-10-15

Implemented the malformed UTF-8 timing item from `standalone-native-todo.md`.
Previously decoding the complete pending buffer could reject a malformed suffix
before any earlier complete commands executed. The regression test failed with
“malformed UTF-8 suppressed or replayed preceding output” on the old code.

The decoder now optionally reports the valid byte-prefix length. Source lexing
processes that prefix without treating its end as EOF, then defers the malformed
or truncated-input diagnostic until the reader reaches the failing suffix.
Session tests cover invalid lead/continuation bytes, overlong encodings,
surrogates, out-of-range scalars and truncated input at EOF. Each runs at every
two-way byte split, with collection between feeds, checking exactly-once preceding
output, one correctly located diagnostic and successful recovery on new input.

All four ordinary standalone tests pass. The Python conformance count remains
116; the added checks are in the C++ session suite. Evidence and validation logs
use `utf8-` under `artifacts/standalone-coverage/`, including the failing pre-fix
regression in `utf8-before.log`. Historical UTF-8 byte equivalence is not claimed:
Unicode is a deliberate standalone interface, not the original machine encoding.

Final validation: **4/4** standalone tests and **4/4** ASan/UBSan tests pass
(`detect_leaks=0`); **23/23** reference CTests pass. `make test`,
`make cpp-quine`, `make cpp-zone1224`, historical/hybrid trivial checks,
recovered example/game comparisons and whitespace checks pass. All command
exit statuses were checked. No machine backend implementation changed.
