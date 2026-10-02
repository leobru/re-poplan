# Image-independent native runtime: development status

The requested fully native runtime is **not complete**. No implementation gate
has passed in full. `--native` currently exposes an arithmetic development probe,
not a usable POPLAN replacement. Keep the machine backend as the default.

## Contract

The new backend must preserve language semantics, evaluation order, exact numeric
bits, identity, mutation, diagnostics, recovery and the documented closure failure.
The requested double-first real-literal conversion is an explicit exception to
bitwise agreement with the historical decimal scanner (see below).
It must eventually reproduce legacy allocation, code and activation costs and
exhaustion thresholds. Native operations must never execute BESM instructions,
recognize finished instruction words, dispatch via historical PCs, load the
executable image, or fall back to `Machine`. Console, clocks, POPLIB source loading
and established POPMESS behavior are in scope; other historical devices are not.
POPLIB compatibility is described in [poplib-format.md](poplib-format.md).

The reference backend retains all address-preserving and architectural-state
requirements. The native backend uses language values and operations; provenance
addresses are evidence annotations only. Native arithmetic scratch storage is
local to each operation. There is no native architectural register bank.

## Status matrix

| Gate / area | Evidence and implementation | Verification / remaining work |
| --- | --- | --- |
| 1: resident inventory | [184 descriptor audit](native-resident-inventory.md), generated from `keyword_descriptors` in `src/poplan.cpp` | Bootstrap names checked against image; callable bindings all unbound |
| 1: syntax and compiler | `03544`, `03736`, `04001`, `04214`, `04536`, `04665`; canonical descriptions in `subroutines-from-trace.md` | Full branch, syntax-handler and lowering inventory still required |
| 1: emission sites | `05007`, `05052..05157`, especially `05124`; generated rebinding `11726..11731`, binding `11755..11757` | Need enumerate callers and establish each emitted operation's POP meaning; do not decode completed words |
| 1: object/allocation model | `01107` dictionary, `05215` pair allocation, `05430` common-list allocation, `11464` descriptor wrapper | Free-block selection/release and common-list accounting implemented; see [allocation evidence](native-allocation.md). Collection and activation/code accounting remain open |
| 2: values | `NativeValue` reserves number, word, string, pair, list, reference, function kinds and stable integer handles | Numbers, resident word metadata and logical allocator implemented; compound payload storage remains open |
| 2: dictionary | Native bootstrap retains resident packed names, class and property words | [Packed-name interning](native-dictionary.md), collision order and four-word allocation implemented. Function bindings, aliases, reader conversion and mutable syntax remain open |
| 2: arithmetic | Existing normalization/add/multiply/divide extracted into `NumericScratch` in `src/machine.cpp`; native `06712` normalization, integer retagging and `06727` range check | Bitwise primitive differentials include signed limits, zero, negative and real operands; full numeric/scanner/function coverage remains open |
| 2: real literal reader | `native_parse_real_literal` validates decimal-point / `$` syntax evidenced at `16672..16735`, parses IEEE binary64, then truncates to BESM encoding | Exact encoding, boundary, malformed-input and locale tests; deliberate conversion policy described below |
| 2: formatting | Shares existing `format_prreal`, preserving existing formatting limitations | Historical probe comparison; complete numeric printing conformance not established |
| 2: services | Input/output injected as streams | Resumable input, clocks, packed names/characters and console callbacks not ported |
| 3: development probe | `native_compile_probe` lowers numeric expressions with resident arithmetic priorities and parentheses to constant/binary/print operations | Expression fixture matches three reference modes; binding, general unary operators and general POP syntax remain unsupported |
| 3: recovery | Unsupported input returns explicit development failure, with no machine fallback | POP diagnostics and SETPOP recovery not implemented; host failures are not presented as compatible POP diagnostics |
| 4: frames/control | Not implemented | Functions, branches, loops, recursion, closures, dynamic binding, triangular thresholds and Man-or-Boy required |
| 5: dynamic language | Not implemented | Macros, runtime compiler/evaluator, composition, partial application, accessors/updaters and mutable syntax required |
| 6: host scope | Not implemented beyond development output | Resident primitives, POPLIB, POPMESS, clocks, EOF/suspension, callbacks and MI/MO quirks required |
| 7: completeness | Not passed | Quine, recovered zones, language/standard fixtures, fixed-seed game and expansion probes must pass natively, with inventory coverage |

## Development interface

```
printf '2+2=>\n' | build/cpp/poplan --native
```

Accepted probe syntax is repeated numeric expressions followed by `=>`, with
unsigned decimal integer tokens (zero included), real literals described below,
`+ - * /`, and parentheses. Multiplication/division bind more tightly than
addition/subtraction; operators at equal priority associate to the left.
Whitespace between tokens is allowed. General unary operators remain absent;
signed real literals are accepted at the beginning of an expression or inside
parentheses, so use `1+(-.5)=>` rather than `1+ -.5=>`.
The entire probe is validated before execution; unsupported source produces a
nonzero exit and no partial execution. This is a deliberately restricted test
lowering, not a replacement grammar inferred from the POP-2 manual. No compiler
allocation-cost claims follow from it. Input is currently collected until EOF;
interactive suspension is not supported.

`--native` is mutually exclusive with `--image` and `--io-demo`, and rejects the
machine-only environment controls even if they are present with empty values.
The dispatch happens before constructing `Machine`. `NativeExecutionResult`
counts POP operations, independently of historical instruction counts. The
probe uses one operation per literal, binary operator and print; parentheses
emit none. `2+3*4=>` uses six native operations. No historical profile is changed.

`poplan_native_guard` compiles the existing CPU source with guards that fail on
any machine step or decoder entry. Its native probe runs from an empty working
directory. A negative control proves the guard rejects the reference path.
The guard covers generated instruction execution because it also goes through
`Machine::step`. The regular executable retains the independent oracle.

## Required next work

Finish gate 1 before extending the probe into a language compiler: map every
syntax handler and emission caller to its POP operation, allocator cost and
recovery behavior. Establish the compatibility allocator before claiming any
recursive or compiled-program conformance. The closure failure and recursive
threshold evidence in [original-structure.md](original-structure.md) is binding;
passing small arithmetic expressions does not establish those behaviors.

Primitive comparisons canonicalize logical inputs and compare exact result bits
and failure/success with instruction-only execution. They currently do not
compare allocation events, compound sharing/cycles, language diagnostics or
recovery, because those facilities have not been implemented. Shared arithmetic
also means historical end-to-end comparison remains necessary as a separate
oracle. A full implementation still needs semantic checkpoints and the entire
acceptance corpus under all backends.

Regenerate the resident audit with:

```
python3 tools/native-inventory.py > docs/native-resident-inventory.md
```

## Verification for this development slice (2026-09-26)

- Focused native arithmetic comparisons cover 324 operand/operation combinations
  against instruction-only `06733`/`06740`/`06744`/`06750`, including sign-extended
  negative integers, signed boundaries, zero and real operands. Bootstrap clears
  memory word zero in the oracle, matching actual image startup.
- `make test`: passed, including all 8 CTests, the historical smoke fixtures and
  historical coverage-expansion regression. These corpus checks exercise the
  reference backend, except the explicitly named `poplan-native-probe` test.
- `make cpp-quine` and `make cpp-zone1224`: passed. Full zone1224 normalized
  output also matches `tools/run-dispak.sh --bootstrap poplan.b6`.
- Native `2+2=>`, `3-5=>`, `7*8=>`, `1/2=>` output matches hybrid, instruction-only
  and historical execution. Both trivial reference checks normalize to `** 4`.
- Native isolation guard and its negative control passed. Unsupported source,
  machine-only control combinations and division by zero fail explicitly.
- `git diff --check`: passed. Existing user changes were retained.

Local logs: `artifacts/native-verification/native-focused.log`, `artifacts/native-verification/native-smoke.log`,
`artifacts/native-verification/native-reference-corpus.log`, `artifacts/native-verification/native-probe-historical.normalized`,
`artifacts/native-verification/native-zone1224-historical.normalized`, and `artifacts/native-verification/native-sanitize.log`.

ASan/UBSan: all 8 CTests passed (216.68 seconds), exit status 0, with
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. The shared division
kernel now multiplies signed mantissas by two instead of left-shifting negative
signed values, avoiding C++17 undefined behavior without changing BESM results.

## Continuation status (2026-09-26)

The first slice is preserved as the verified baseline above. Continuing work is
on native allocation and dictionary foundations, before extending source syntax.
Free-block selection (`05447`), release (`03506`/`03516`), initial common extent
(`20660`), and common-list accounting (`10402`) are now implemented and pass
focused instruction-only differentials. Collection,
retry (`05430`/`05523`), compiler costs, activation costs and language integration
remain open. In particular, `COREUSED` counts only the common list; small blocks
returned to size-specific lists must not be subtracted from its used-word result.


Packed-name interning (`01107..01166`) now uses this allocator: resident words
retain their logical handles, misses allocate four words, collision chains keep
the original order, and failed allocation leaves the dictionary unchanged. The
reader and callable bindings remain unimplemented. `--native` still accepts
only the original arithmetic probe, so this foundation does not broaden the
advertised language support.

Focused checks passed for `poplan-native-allocator`, `poplan-native-dictionary`
and `poplan-native-probe`. The allocator was also followed by a successful
9-test full smoke run before dictionary work began. The final combined suite
has 10 tests. Detailed evidence and remaining boundaries are in
[native-allocation.md](native-allocation.md) and
[native-dictionary.md](native-dictionary.md).

The next source-recovery priorities are the `05523` collection/retry path and
the reader's normalization/interning interface, followed by compound payload
storage. Do not add a generic parser or infer activation accounting from the
new allocator's live ledger. The collector body beyond the `05542` control
record is mostly printed as data in the current coverage-derived listing;
its control paths must be established before implementing native collection.

Combined continuation verification: focused allocator/dictionary/probe tests
passed; `make test` passed all 10 CTests plus historical regression checks;
`make cpp-quine` and `make cpp-zone1224` passed. Fresh historical zone1224
normalized output matched hybrid and instruction-only output. Trivial historical,
hybrid and native checks all produced `** 4`. Logs are
`artifacts/native-verification/native-foundation-smoke.log`, `artifacts/native-verification/native-foundation-reference.log`,
`artifacts/native-verification/native-foundation-zone1224-historical.normalized`, and
`artifacts/native-verification/native-foundation-sanitize.log`. ASan/UBSan passed all 10 tests in
259.07 seconds with exit status 0, using `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`. Whitespace checks also passed.

## Native real literals (2026-09-27)

The native reader now accepts `3.14`, `.5`, signed reals such as `-.5`, and
optional `$` exponents (`3.14$-3`, `.5$+2`). A decimal point and at least one
fractional digit are required; `1.`, integer-exponent spelling `1$2`, C-style
`1.0e2`, hexadecimal, infinity and NaN spellings are rejected. Signed integer
literals and general unary operators remain outside this probe's grammar.

As explicitly requested, decimal formation first uses locale-independent
`std::from_chars` into an IEEE binary64 `double`, then converts that double to
BESM-6 encoding. The converter truncates excess mantissa bits toward zero,
normalizes negative powers of two, and stores the signed 41-bit mantissa and
7-bit biased exponent. Both signs of zero become BESM zero. A finite double
below the normalized BESM range becomes zero; binary64 range errors and BESM
overflow fail explicitly. The literal reader additionally rejects biased
exponents of 104 or greater, which overlap POPLAN's integer and object tags.
The standalone conversion helper supports the full BESM exponent range.

This policy can differ in low bits from historical digit-by-digit BESM decimal
arithmetic. `Machine::p16645` remains the independent reference implementation.
The test decimal `1.0000000000018189894035458` demonstrates the specified order:
binary64 rounds it to `1 + 2^-39` before BESM truncation. Native arithmetic after
reading still uses the existing BESM arithmetic operations, not host double.

Examples: `3.14=>`, `.5$2=>`, and `1.0+2=>` run with `--native`, including in an
empty directory through the execution guard. Reading all input until EOF and
compiling before execution remain probe limitations; this is not the general
resumable POPLAN reader or its diagnostic/recovery implementation.

Verification for the real-literal change: `poplan-native-reals` checks exact
encoding, double-first rounding, truncation, normalization, range limits,
malformed syntax and locale independence without an image. The extended
`poplan-native-probe` checks ordinary and guarded native runs without an image,
plus hybrid and instruction-only output comparisons. `make test` passed all
11 CTests (44.37 seconds) and the historical smoke/regression suite.
`make cpp-quine` and `make cpp-zone1224` passed; fresh historical zone1224 output
matched both reference modes. Real-literal examples matched historical printed
output, and trivial historical/hybrid checks both yielded `** 4`.

Logs: `artifacts/native-verification/native-reals-smoke.log`, `artifacts/native-verification/native-reals-reference.log`,
`artifacts/native-verification/native-reals-historical.normalized`, and
`artifacts/native-verification/native-reals-zone1224-historical.normalized`.

ASan/UBSan passed all 11 tests in 236.14 seconds with exit status 0, using
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`; see
`artifacts/native-verification/native-reals-sanitize.log`. `git diff --check` passed. Existing unrelated
worktree changes were preserved.


## Native arithmetic expressions (2026-10-01)

The restricted development compiler now supports operator chains and nested
parentheses: `2+3*4=>` prints `14`, `(2+3)*4=>` prints `20`, and `20-6-3=>`
prints `11`. It reads arithmetic priorities from the explicit resident
`keyword_descriptors`, masking the same property field as `03726` (`04377`,
octal `3600000000000000`). The source comparison complements the pending
field, adds the incoming field and one, then either restores the pending item
or emits it through `04447`. For the four resident arithmetic operators this
reduces the earlier operator when its field is less than or equal to the new
field. That includes left association at equal priority; it is not a grammar
inferred from a generic POP-2 description.

A native pending-operation stack retains each grouping boundary and emits only
constant/binary/print operations. Compilation and execution are iterative.
The arithmetic kernel and double-first real conversion policy are unchanged.
No instruction words, execution PCs, executable image, or machine fallback are
used. The full input is still validated before execution, with no partial
output on unsupported syntax.

Historical experiments also expose restrictions worth retaining explicitly:
`2*-3` interns `*-` as a word, while `2* -3` prints an empty stack rather than
ordinary arithmetic negation. This slice rejects both forms instead of
inventing unary semantics. Signed integer tokens, general unary expressions,
variables, function calls, closures, and Man-or-Boy remain unsupported.
Mutable operator definitions, compiler allocation costs, POP diagnostics and
recovery are still open gates. The grouping stress test establishes freedom
from C++ recursion, not historical allocation or nesting thresholds.

Evidence and tests:

- `tests/inputs/native-expressions.pop2` contains 66 expressions: all 16 ordered
  binary-operator pairs, with ungrouped, left-grouped and right-grouped forms,
  plus mixed real/integer arithmetic, signed real atoms, nested grouping and
  large-integer cancellation. Expected output was obtained through the
  historical wrapper.
- `tests/native-probe.py` compares native and guarded native execution in an
  empty directory, hybrid C++, instruction-only C++, and (with `--historical`)
  historical execution. The smoke suite enables that historical comparison.
- Malformed expressions, unsupported operator words and unported unary forms
  must fail explicitly without partial output. A 5000-parenthesis native-only
  probe checks the iterative compilation path.

Validation logs for this increment are kept in `artifacts/native-expressions/`.
Earlier dated measurements above describe their original implementation state.

Verification for this increment: all 16 regular CTests passed (86.76 seconds),
as did `make test` (including historical native-expression comparisons),
`make cpp-quine`, and `make cpp-zone1224`. Fresh historical zone1224 output
matched the reference backend; both trivial checks produced `** 4`.
All 16 ASan/UBSan CTests passed (357.55 seconds), exit status 0, with
`ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`.
`git diff --check` passed. Logs: `artifacts/native-expressions/ctest.log`,
`smoke.log`, `quine-zone.log`, and `sanitize-ctest.log` in the same directory.
