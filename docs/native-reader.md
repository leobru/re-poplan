# Native reader, bindings and recovery

The native shell now uses `NativeReader` and `NativeSession`, rather than
collecting the entire input for `native_compile_probe`. This is the step 2
semantic milestone in `PLAN.md`. It is not full POPLAN or whole-session
allocation conformance. Native startup still requires no executable image.

## Implemented boundary

| Area | Native behavior | Source evidence |
| --- | --- | --- |
| Characters | Incremental UTF-8 input passes through the existing GOST encoder and immutable external-character table; Cyrillic and Latin lookalikes share the original packed codes | `21274`, data at `21301`; reverse display data at `21354` |
| Token classes | Alphabetic words include following digits; contiguous operator characters form one word; punctuation stays separate | `16421`, six-bit classification data at `16437..16456`, dispatch at `16351` |
| Names | First six external bytes determine identity; case conversion occurs at console encoding, before packing; full spelling is consumed | `01107..01157`, reader word accumulation |
| Numbers | Twelve decimal integer digits fit the signed 41-bit mantissa; a thirteenth digit diagnoses before adding a reader item to history. Reals retain the requested IEEE-double-first conversion | `16651..16661`, `16672..16735` |
| Declarations | `VARS` installs the top-level variable flag, preserves values on redeclaration, and warns on repeated explicit declarations | `17536..17542`, flag data `17551`; declaration checks at `25556` |
| Implicit names | First use warns and leaves an undefined binding. Implicit names have zero classification bits, distinct from explicitly declared variables | Reader/compiler warning path and raw dictionary checkpoints |
| Reads and writes | Typed load/store operations access native dictionary bindings. Chained arrows consume successive stack values | `03564..03570` emits selector `05053` with a binding-cell operand; assignment emits `05057`; `03301` and `03303` establish stack effects |
| Operators | Resident property fields determine reduction order; signs at expression start use the original unary replacements; signs after another operator use ordinary reduction | `03724..03734`, `04426..04434`, unary minus `04437..04443`, unary plus `04520..04522` |
| Statements | `;` and `=>` complete a top-level compilation. Commas and parenthesized sequences preserve multiple results. Newlines supply input without inventing statement separators | Top-level compiler/finalizer and `13454` printing path |
| Printing | `=>` prints the whole visible stack with comma separators, including empty stacks and undefined values | `13454`; historical fixtures |
| Small calls | `ERASE()` consumes a value; `SETPOP()` transfers to recovery | `03277`, `13362..13375` |
| Recovery | Supported syntax/type diagnostics use the existing ordered catalog. Recovery discards the rest of the physical input line, clears value/activation/compiler state, and preserves dictionary bindings and the eight-item diagnostic history | `03014`, `03106`, source context `16750`, `13362..13375` |

Native operations carry source/emission annotations, never execution PCs.
No instruction words are recognized or executed. The existing probe API remains
available for its primitive/compiler-storage tests; it is no longer the shell.

The integer formatter previously interpreted a 40-bit payload with bit 40 as its
sign. It now uses the full 41-bit mantissa and bit 41 as the sign, matching the
reader and the historical output for `549755813888` and `999999999999`.

## Suspension and state

`NativeReader::feed` accepts arbitrary byte chunks, including partial UTF-8
characters, names, operator words and `$` exponents. `next` distinguishes tokens,
input-required, and EOF. Its partial token state is retained. `NativeSession`
retains the compiler operation list, reduction stack, declaration/assignment
state, small-call continuation and deferred diagnostic state. It processes each
delivered token once; repeated empty input cannot replay warnings, interning,
binding changes or output. Completed statements execute before EOF.

EOF abandons an unfinished compilation without manufacturing a semicolon.
Declarations and warnings already processed by the compiler remain observable.
Unsupported facilities return an explicit development error, retaining effects
of earlier completed statements. POP diagnostics instead perform a language
recovery transfer and allow later input lines to execute.

The protected-assignment diagnostic requires one following reader item before
reporting (`04040`). The pending diagnostic survives suspension, and its context
includes that lookahead item. An overlong integer diagnoses inside the reader,
before the malformed number enters token history.

## Empty-stack quirk and limits

The POP stack is a native value vector with an explicit empty-stack cell and
underflow state, not an emulated register or hardware stack. The reference cell
at `070000` starts as real zero. Missing binary operands can consume it and put a
result below the visible stack; for example, `2* -3=>` prints an empty stack.
The cell retains its value across printing. A fixture retrieves that retained
value through a variable and compares its exact number bits with the raw backend.
Underflow beyond this one established cell reaches non-stack memory in the
reference implementation; native execution explicitly rejects that path.

Unimplemented facilities include general calls, user functions, strings/lists,
nondecimal readers, character literals, mutable syntax/operators, callbacks,
macros, runtime evaluation and non-console services. They never enter `Machine`.
Malformed real-literal branches outside the existing reader contract still
produce an explicit development failure rather than a guessed POP diagnostic.
Diagnostic suppression switches and arbitrary nested diagnostic callbacks are
not implemented.

Dictionary records and finalized operation graphs use the compatibility
allocator. The reader's historical temporary-object allocation schedule and
compiler optimization/environment scheduling are **not yet reproduced**.
`legacy_accounting_complete` remains false. Input split invariance is tested for
native allocation outcomes, but this is not equality with historical total
`COREUSED` or exhaustion thresholds. These remain explicit blockers to the
whole-runtime acceptance gate and recursion/exhaustion claims.

## Verification

`poplan-native-reader` checks all 256 forward character conversions against the
instruction-only `21274` routine. Each byte split of both fixtures, including
splits inside Cyrillic UTF-8, must preserve output, operation counts, native
allocation outcomes, interned identities, properties and binding values.
Repeated empty feeds exercise suspension without side effects. Dynamic dictionary
classification and exact binding bits are compared with an independently executed
instruction-only session, canonicalizing identities by packed name.

`tests/native-probe.py` runs both fixtures through native and the isolation guard
from an empty directory, hybrid C++, instruction-only C++, and, with
`--historical`, `tools/run-dispak.sh`. The historical output is checked in under
`tests/expected/`. There is one explicit reference-backend distinction: the
instruction-only extracted-image run omits the explanatory catalog lines in the
recovery fixture. Its diagnostic codes, values, context and recovery still match.
Native, hybrid and historical runs must match the full historical fixture.

Validation logs belong in `artifacts/native-reader/`; earlier historical machine
step counts remain unchanged. The native fixture operation counts are separate
measurements, not replacement machine-step counts.

The checked-in fixtures execute 103 native operations (`native-bindings`) and
29 (`native-recovery`). This includes execution before diagnostic transfers;
reader/compiler work and warnings are not counted as runtime operations.
Logs also record 48 fixed-seed expressions with grouped unary operands. A separate
ungrouped-sign probe reached deeper underflow into non-stack memory: native
reported its explicit limitation, while historical execution entered invalid
execution. That probe is not counted as a passing conformance case.


Milestone verification passed: 19 normal CTests (68.79 seconds), 19 ASan/UBSan
CTests (330.74 seconds, leak detection disabled), `make test`, `make cpp-quine`,
`make cpp-zone1224`, historical trivial/zone1224 comparisons, both native fixtures
through the image-independent isolation guard, and `git diff --check`.
Logs are in `artifacts/native-reader/`. Verification ran with the pre-existing
uncommitted machine shadow-state edits present; those edits are excluded from
this milestone commit.


Conditional compiler state and comparison recovery were added on 2026-10-06;
see [native-control.md](native-control.md). The reader test now also checks every
byte split and raw binding checkpoint of both control fixtures. The earlier
reader operation counts and validation measurements above remain unchanged.

Ordinary named functions, explicit call frames and dynamic binding restoration
were subsequently added; see [native-functions.md](native-functions.md). Ordinary
nested definitions and historical shared rebinding followed on 2026-10-07; see
[native-nested-functions.md](native-nested-functions.md). The full language and
exact exhaustion accounting remain outside the implemented boundary.
