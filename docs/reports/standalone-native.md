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

## Output consumer rebinding — 2026-10-15

Closed the per-character output-consumer lookup gap within the printing audit.
Previously `output_text` captured CUCHOUT once for the entire formatted text.
When a callback handled `A` and assigned CHAROUT to CUCHOUT, that same callback
still received `B` and `C`. The pre-fix regression therefore reported the last
character, 67, rather than emitting `BC` and recording the first character, 65.

Manual §8.3 specifies mutable CUCHOUT as the current character consumer. A
historical PRSTRING probe confirms that replacement takes effect between
characters. An encoding-independent `123` probe agrees on emitted `23` and the
first character code 49, after removing historical prompts and print-arrow line
breaks. This comparison does not claim identical historical text formatting.

Generated character-output operations now load the current CUCHOUT binding
before each call. The identifier is rooted by the operation graph, and callbacks
use the existing VM frames and input suspension. The direct CHAROUT path remains
available when no user callback executes. No new execution engine is involved.

Twelve additional conformance checks cover PRSTRING, nested printing, GENOUT,
invalid replacement consumers, PR, PRINT, PRREAL, PRBIN, PROCT, SP, NL and the
print arrow. Two session cases cover Unicode rebinding and an ITEMREAD callback
at every two-way byte split and in bytewise feeds, with collection while
suspended. The conformance count is now 128. Object formatting still builds its
complete text before scheduling consumers; supplier/mutation interleaving and
streaming remain open, as recorded in the TODO.

Evidence and validation logs are retained in
`artifacts/standalone-output-consumers/`, outside the build area.

Final verification: **4/4** standalone tests pass (29.47 seconds), including
**128** conformance checks; **4/4** ASan/UBSan tests pass (192.88 seconds,
`detect_leaks=0`). All **23/23** reference CTests pass, as do `make test`,
`make cpp-quine`, `make cpp-zone1224`, the historical callback probe and
historical/hybrid trivial checks. Fresh standalone example/game output agrees
with the saved historical runs (11 numeric rows and all 12 board rows, both
computer moves and normal exit). Whitespace checks pass. Each command's final
exit status was checked. The reference backends are unchanged by this increment.

## Resumable printing from historical traversal — 2026-10-15

Implemented the streaming-printing TODO using the original printer's calls and
continuations as semantic evidence. Native output had previously forced and
formatted an entire object before invoking any character consumers. The failing
pre-fix test printed `___[1 2]` when a lazy supplier emitted `_`; the original
prints the opening bracket first and interleaves supplier effects with elements.

### Historical boundaries used

| Original boundary | Established behavior | Native counterpart |
| --- | --- | --- |
| `07667..07673` | Obtain and classify the value being printed | Start a print-task frame for PR/PRINT and arrow values |
| `12630..12634` | Duplicate PRINT's argument before entering PR | Retain the supplied value on the POP value stack while the printer and suppliers run |
| `07704..07707` | Emit '[' through `07652` before list forcing/classification at `10353` | Emit the bracket before processing the first list-node task |
| `07711..07714`, `07722..07725` | Classify/force the tail, output its separator, then obtain the next head | Separate list-node classification and post-separator read tasks |
| `07714..07716` | Call `17021` for HD and `17013` for TL, saving TL before recursively printing HD | Capture the two values in source order, retaining the saved tail as a task root |
| `07720..07722` | Resume from the saved tail after head printing | Subsequent callbacks see mutations of that tail's contents, but replacing the old node's TL does not redirect traversal |
| `10005..10014` | Read each string character via the subscript call before dispatching its consumer | Read the current Unicode string at each character task |
| `07661..07663`, `10011..10012` | Reload and call the current character consumer | Existing per-character CUCHOUT load/call operations |

Historical and instruction-only probes agree on six cases: lazy-supplier markers,
head mutation on '[', tail-link replacement during head output, mutation of a
saved tail's head, mutation during the separator, and progressive string updates.
Standalone agrees after allowing its established numeric spaces/line formatting.
The saved historical instruction trace and extracted boundary records confirm
the call order; evidence lives in `artifacts/standalone-stream-print/`.

The hybrid backend disagrees with historical/instruction-only execution on the
numeric-character callback mutation probes (saved `tail_mutation-hybrid.out` and
`space_mutation-hybrid.out`). Those outputs are not used as the semantic oracle.
No hybrid fix is claimed by this standalone change.

### Native execution and tests

PR, PRINT, PRSTRING and the print arrow now schedule an explicit print-task stack
attached to a VM frame. A task emits a punctuation/text chunk, obtains one list
node or current string character, or leaves a cycle-detection scope. NeedList
keeps its task intact until the supplier finishes. User callbacks suspend in
their own frames; no already emitted bracket, character or supplier effect is
replayed. Faults and SETPOP unwind the print frame through existing recovery.
Collection traces task values and active object handles, so replacing a list's
tail and dropping its global binding while a callback waits for input cannot
collect the original saved tail. Host `Runtime::format` uses the same traversal
algorithm for synchronous snapshot formatting.

Sixteen additional conformance cases cover the historical timing differences,
the arrow, nested printing, output prefixes surviving faults, shared/cyclic
objects and operation-budget termination of an infinite lazy list. Three session
programs run at every two-way byte split and bytewise with GC, covering supplier
input, a detached tail and string mutation after callback input. An explicit
failure while suspended checks that recovery drops pending print tasks. A C++
test prints 2,000 nested lists with a small frame budget, exercising collection
without recursive host traversal. A historical/raw STACKLENGTH probe also checks
PRINT's saved value during supplier execution. The total conformance count is 144.

Canonical Man-or-Boy remains **-67**, now **14,987 native operations** with maximum
frame depth **1,122**. The extra print tasks explain the operation-count change;
the previous **14,983** and earlier **14,413 / 1,024** measurements remain intact.
These are native-operation counts, not historical machine steps.

The existing non-list reference/record/strip presentation and permissive
PRSTRING argument types remain explicit follow-up contracts in the TODO and
resident ledger. This increment establishes traversal/callback ordering, not
complete printer or language conformance. Progressive Unicode string reads
currently decode the string for each character; scaling remains an efficiency
audit, separate from the verified mutation behavior.

Final verification: all **4/4** standalone tests pass (47.85 seconds); all
**4/4** ASan/UBSan tests pass (244.68 seconds, leak detection disabled). After
the final test-only additions, **144** conformance checks pass again in ordinary
execution and under ASan/UBSan (39.69 seconds, UBSan halt-on-error enabled).
All **23/23** reference CTests pass (101.99 seconds), as do `make test`,
`make cpp-quine`, `make cpp-zone1224`, the six historical/raw timing probes,
historical/hybrid trivial checks and example/game comparisons against saved
historical output. Whitespace checks pass, and every command's final exit status
was inspected. No machine or compatibility-native implementation changed.

## Printer argument classes and DATAWORD output — 2026-10-16

Completed the remaining-printer-contracts TODO from the streaming milestone.
Historical `07777..10001` accepts the built-in character-strip class and sends
other arguments to diagnostic 14260 before reading any character. Both literal
strings and INITC objects have that class; a generated strip merely named CSTRIP
does not. Historical/raw probes confirm those distinctions and recovery.

Native PRSTRING now checks the same semantic class before scheduling output or
forcing a lazy list. The existing progressive character task handles literal
Unicode text and INITC's Unicode scalar components. PR quotes either kind;
PRSTRING omits the quotes. Invalid Unicode component values still fail explicitly
through the existing scalar conversion rules, without historical GOST conversion.
Uninitialized zero components consequently print U+0000; their byte output is not
claimed identical to historical zero-character rendering.

The original PR branch at `10025..10030` calls DATAWORD (`10463`) then PR for
non-list, non-character structures, as manual §8.4 describes. Native DATAWORD and
printing now share a class lookup. Ordinary pairs, references and ordinary strips
print PAIR, REF and STRIP; generated records/strips print their mutable class word.
Function output now uses FUNCTI, matching the interned six-character DATAWORD,
instead of the earlier hardcoded FUNCTION display. The affected function fixture
was updated to the historically established spelling.

CONSPAIR and common-list construction previously used identical host pair objects,
so the printer could not distinguish an ordinary pair with a NIL tail from a list.
A native construction marker now preserves that distinction through COPY.
CONSPAIR output is PAIR, while CONS/:: and source lists retain bracket output.
List predicates and finite-list extraction respect this marker; handles and shared
fields are unchanged. Remaining ordinary-pair accessor/comparison behavior is a
separate explicit TODO: for example historical HD rejects ordinary pairs and
EQUAL distinguishes them from common-list nodes, while SAMEDATA can accept both
as the PAIR data class. This increment does not claim that broader audit complete.

Nineteen conformance checks cover invalid PRSTRING argument classes, empty
strings/INITC, generated nominal classes, pair COPY, list predicates, references,
mutable record/strip labels, PRINT identity, function labels and character-strip
mutation. The total is **163**. Session cases exercise Unicode component mutation
after input suspension, label changes and rejection of a lazy list without
calling its supplier, with collection between feeds.

Five normalized historical/raw/native comparisons establish record, pair and
function labels, initialized character strips and pair/list predicates. Two
additional rejection comparisons establish type-error recovery. Prompt/spacing,
quote glyphs and the documented mixed Cyrillic/Latin output mappings are normalized;
modern diagnostic text remains intentionally distinct. Evidence and validation
logs are in `artifacts/standalone-printer-contracts/`, outside the build area.

Final verification: all **4/4** ordinary standalone tests pass (45.33 seconds),
including **163** conformance checks. All **4/4** ASan/UBSan tests pass
(321.22 seconds, leak detection disabled), covering the same 163 checks and
Unicode callback/suspension/collection cases. All **23/23** reference tests pass
(119.33 seconds), as do `make test`, `make cpp-quine`, `make cpp-zone1224`,
historical/raw printer comparisons, historical/hybrid trivial checks and fresh
example/game comparisons with the saved historical outputs. Whitespace checks
pass; each final command exit status was checked. The reference backends were
not changed, and earlier measurements remain intact.
