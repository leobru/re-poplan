# Faithful C++ POPLAN Port

The target is a C++ reproduction of the complete BESM-6 POPLAN system. It is
not an independently designed POP-2 parser or interpreter.

## Compatibility Contract

- Preserve original POPLAN routine boundaries and use their octal entry
  addresses in C++ names.
- Preserve 48-bit tagged values, dictionary records, function descriptors,
  generated objects, stack direction, and activation/environment layout.
- Derive compiler phases and calls from the original listing and traces.
- Establish behavior against the unmodified volume `2148` image.
- Reproduce known original failures before changing their semantics.

The initial C++ baseline must therefore reproduce the arity-3 closure-binding
failure: the third dynamic `B` call continues with the inner captured `K`
instead of restoring the older closure environment. A corrected mode will be
introduced only after that baseline is observable in a focused C++ test.

## Current Status

The address-preserving machine layer now translates:

- `01004..01027` startup/evaluator initialization and its retained call
  boundaries;
- `01107..01166` traced hash construction, collision-chain lookup, allocation
  continuations, and register restoration for entry `01107`;
- `01167..01170` alternate register setup for the shared `01122` body;
- `02750..02763` function validation and dispatch;
- `02764..02766` descriptor construction and function-entry transfer;
- `02767..03005` indirect evaluator selection and validation;
- `03014..03071` normal diagnostic packaging, unpacking, and first formatter
  call;
- `03072..03100` first formatter-call restoration and heading setup;
- `03206..03234` ordinary-call setup and captured-slot preparation;
- `03235..03260` environment binding and restoration;
- `03261..03264` function entry;
- `03275`, `03277`, `03301`, and `03303` POP value-stack operations;
- `03314` shared binding return and `03374..03402` two-value selection;
- `03330..03336` traced two-stage value classification;
- `03337..03342` generated two-value POP/allocate/push bracket;
- `03413..03423` traced numeric-update arithmetic;
- `03516..03520` addressed-word replacement;
- `03536..03541` computed-continuation frame construction;
- `03544..03630` trace-confirmed computed compiler paths and their preserved
  dependency boundaries;
- `03631..03635` continuation-word update and five-word frame restoration;
- `03716..03723` addressed cyclic update, computed transfer, and ordinary
  return;
- `03724..03735` table read, transform, allocation re-entry, and table write;
- `03736..03761` compiler selection, nested classification, and saved-frame
  restoration continuations;
- `04074..04110` repeated shared-table writes and two-word frame cleanup;
- `04214..04311` generated compiler frame, selection paths, record updates,
  and frame restoration;
- `04322..04355` classification, record update, and nested dispatch
  continuations;
- `04426..04434` two-marker record-field selection and replacement;
- `04447..04455` allocator wrapper and generated-store continuation;
- `04467..04504` compiler/evaluator wrapper and record-result continuation;
- `04665..04674` compiler repeat wrapper, record-field comparison,
  `04467` re-entry, and saved-state restoration;
- `04675..04756` second compiler-chain wrapper, marker branches, shared scan,
  record update, nested allocation, and saved-frame continuations;
- `05045..05051` generated-continuation selection shared with `05040`;
- executable selector words `05052..05076`, generated-word assembly at
  `05124..05137`, and the empty-word cleanup bracket `05143..05157`;
- `05207` and `05211` indirect object-word loads;
- `05215..05225` tagged two-word allocation and initialization;
- `05230..05346` cold-start frame, initialization loops, dependency
  continuations, and saved-register restoration;
- `05430..05447` allocation wrapper, common-list allocator, and return
  continuations;
- `06343..06561` trace-confirmed compiler/evaluator entry cluster and its
  translated continuations;
- `06623..06647` four generated comparison templates, including their
  alternating `A-X`/`X-A` operations and result branches;
- `06650..06661` shared generated comparison and diagnostic continuations;
- `06712..06754` arithmetic normalization and add, subtract, multiply, and
  divide entry paths;
- `07472..07474` shared character-extractor call and binding continuation;
- `07475..07504` traced `CUCHIN` argument paths;
- `07533..07541` output-descriptor cleanup and caller-frame restoration;
- `07673..07745` compiler frame, classification branches, recursive helper
  continuations, six-pass bit loop, and five-word restoration;
- `07773` native packed-string output for dictionary primitive `PRSTRI`,
  retaining `21275` table conversion and falling back to instruction
  interpretation whenever the character callback at `01567` is rebound;
- `10232..10234` hardware-stack comparison and descriptor selection;
- `11464..11477` address-based allocation wrapper, `05430` continuation,
  transformed-word store, and three-word frame restoration;
- `11500..11502` frame-relative indexed load;
- `11524..11530` generated two-value validation and indexed-load bracket;
- `11536..11545` tagged-value precheck, frame-field match, and diagnostic
  exits;
- `11673..11745` generated quine update/binding cluster, including its unique
  `r14` linkage;
- `11755..11757` runtime-installed binding and rebinding templates;
- `12674` native fixed-point text formation for dictionary primitive
  `PRREAL`, retaining the packed-output and `07742` restoration boundaries;
- `13207..13215` generated multiply/add/subtract and masked return leaf;
- `15765..16003` special-function argument expansion and evaluator
  redispatch;
- `16005..16027` descriptor-chain scan, allocation, record initialization,
  and computed return;
- `16254..16303` trace-confirmed arithmetic table search, including its
  multiply/RMR offset path and two-value narrowing loop;
- `16313..16333` packed-character sequence entry, dispatch, and restoration;
- `16341..16350` trace-confirmed compiler dispatch frame and restoration;
- executable lookup-result selectors `16351..16370`, their shared
  continuations, and record setup through `16405`;
- `16376..16401` six-register frame restoration and environment bind;
- `16406..16416` record-word mask, comparison, and computed continuations;
- `16417..16420` hash call and POP-value forwarding continuations;
- `16421..16434` tagged-low-byte validation and packed-table lookup;
- `16457..16462` three-word record shift and continuation selection;
- `16511..16512` shifted record-word save and relocated continuation;
- `16463..16466` evaluator/store bridge and saved-state restoration;
- `16467..16476` descriptor-advance bracket and record-field update;
- `16477..16504` record-word presence check, evaluator call, and result store;
- `16505..16510` record-shift call wrapper and caller restoration;
- `16531..16552` record comparison, evaluator continuations, and `r7` loop;
- `16643..16664` trace-confirmed generated record paths and loop
  continuations;
- `16742..16744` record-byte lookup wrapper, result adjustment, and indirect
  `r7` return;
- `17013..17052` trace-confirmed evaluator wrapper paths;
- `17242..17253` shared long/short table-scan entries;
- `17254..17266` shared table read and its allocation continuation;
- `17275..17306` shared table write and its allocation continuation;
- `17337..17342` descriptor-selecting wrappers for those shared table bodies;
- `17417..17461` object/compiler classification, nested call continuations,
  generated transfer, and saved-frame restoration;
- `17624..17761` generated descriptor scan, record update loop, nested
  allocation continuations, and four-word frame restoration;
- `17762..17773` two-word object allocation, tagged-link installation, and
  shared counter update;
- `17774..20007` record-chain classification, counter stamping, link
  traversal, and ordinary counter update;
- `20077..20106` generated return selection and restoration;
- `20667` generated entry into the `16341` compiler frame;
- `20110..20123` activation argument transfer;
- `20124..20140` activation construction;
- `20144..20162` supervisor setup, extracode-register setup, and diagnostic
  continuation;
- `20170..20223` traced primary-input readiness and transfer continuations;
- `20245..20260` continuation-state selection, `Э71` readiness/output
  transfers, and post-extracode status/completion entries;
- `20263..20320` console capability probing, descriptor construction, and I/O
  state initialization;
- `20456` native startup clock and time-of-day greeting construction, with
  the original `20526` GOST descriptor and `20674` output boundary;
- `20475` native exit/session/CPU clock and farewell construction, with the
  original `20536`/`20550` GOST descriptors and `20674` output boundaries;
- `20660..20665` memory-bound arithmetic and indirect store;
- `20673` trivial register return;
- `20674..20706` console message output and status handling;
- `21107..21117` descriptor-mask and record-word update;
- `21141..21174` generated two-arm allocation/update loop and frame
  restoration;
- `21255..21257` character-input wrapper entry;
- `21251..21254` character extraction and conversion return;
- `21260..21275` table-driven character forwarding, return, decode, and
  encode entries;
- `21431..21440` packed-text character extraction and cursor advance;
- `21443..21454` packed-byte insertion and descriptor advance;
- `21464..21522` trace-confirmed evaluator classification and return paths;
- `25223..25230` stack-driven retry and unwind loop around `16151`/`16145`;
- `25346..25365` converted-character output, counting, continuation resume,
  and return entries;
- `25356` and `25370..25405` end-character and packed token-source paths;
- `25421..25426` descriptor advance wrapper around the preserved `06424`
  boundary.

The C++ tests compare the one-argument `20124` transition at generated entry
`65576` with the BESM trace, exercise a three-argument activation round trip,
and reproduce the reduced arity-3 failure. Descriptor
`6606562700065576` repeatedly dispatches to `65576` through environment
`65627`; because the record at `65632` has a zero address, the third call
continues with shared slot `65763` at `K=0` instead of restoring outer `K=1`.

The individually translated entries remain an evaluator/activation milestone,
not yet a source-level translation of the complete compiler. The primitive
runtime is entered through the syntax-diagnostic output path, including the
table-driven character converter. Entry `21260` continues through the `Э71`
operations at `20252` and `20256`, using the original indexed control words
and packed GOST buffers. The alternate `Э64`/`Э74` paths and continuations
`20261` and `20715` remain untranslated boundaries.

Separately, the `poplan` executable can now load the extracted 036000-word
image. Before fetching a BESM instruction, the machine dispatches a recognized
left-half `pXXXXX` entry to its address-preserving semantic implementation;
untranslated addresses fall back to the instruction interpreter. Its Э71
adapter supplies prompts, line input, record output, and GOST-10859 UTF-8
conversion in both directions, including Unicode Cyrillic input. Э75 stores
generated instruction words into memory, so the
historical compiler and evaluator run unchanged inside this machine layer.
`make cpp-quine` matches the existing normalized quine fixture through the
final host-EOF input request. This raw-image route is a whole-system
conformance path, not a substitute for continuing the address-preserving
routine translations. Semantic dispatch can be disabled for differential
testing with `POPLAN_INTERPRET_ONLY=1`; the long-term target is to make the
fallback unnecessary by expanding the translated-entry set.
`zone1224.pop2` now completes identically in hybrid and instruction-only
modes; that run also exercises captured-slot paths through translated `03206`.
The latest profile executes 228,943 semantic routine steps and 109,768 raw
instructions. Regions `03337..03342`, `03544..03630`, `04001..04116`,
`04214..04311`, `16643..16664`, `21141..21174`, and `25421..25426` contribute
no remaining raw instructions in that session.
The startup and session-end owners at `20456` and `20475` now construct their
complete GOST text natively. Their former `25641`/`25660` digit-building calls
are bypassed, while semantic `20674` still performs the actual `Э71` transfer.

The final quine-static pass converts `01000..01002`, `03461..03475`,
`07761..07772`, `11514`, `11746..11747`, `16145..16150`, `16513..16521`,
`16530`, `16616..16623`, `16745..16747`, `17120..17121`, `17131`, the primary
input `Э71` sites at `20200`/`20205`/`20207`, and `20564..20570`. A fresh run
records 11,406 semantic dispatches and 95 raw instruction steps. None of those
raw steps is in the immutable image; all are generated POP-2 code at
`32535..32566` or `65556..65765`.

The subsequent `zone1224.pop2` static sweep converts every remaining traced
immutable-image entry, including the hot `13454`, `04142`, `06757..07043`,
`12125`, `12246`, `15322`, and `21631..25753` clusters and their left-half
continuations. A fresh combined CPU/routine trace contains no instruction
fallback in immutable code. The remaining 100,723 instruction steps are in
dynamically generated POP-2 code; the same run makes 231,482 semantic
dispatches.

## Port Order

### 1. Tagged Machine State

Keep the current `Word48`, 32K-word core, accumulator, and BESM index
registers. Add object decoders only when their tag tests are confirmed in the
original routines.

Initial address-preserving entries:

- `02750`: function validation and dispatch;
- `03206..03264`: ordinary function call/environment selection;
- `03275`, `03277`: POP value-stack push and pop;
- `20110..20123`: activation entry and argument transfer;
- `20124..20140`: activation construction and value-stack transfer.

### 2. Evaluator and Activation Semantics

Transliterate the routines above instruction block by instruction block.
Tests must compare:

- register and memory transitions from short BESM traces;
- function descriptor, environment, and entry fields;
- activation stack and POP value-stack positions;
- nested closure rebinding at each dynamic call.

The arity-3 debug sequence is the first closure conformance gate. Correct
source semantics require decremented `K` values `1, 0, 0`; original POPLAN
produces `1, 0, -1, -2, ...`.

### 3. Primitive Runtime

Port public routines using dictionary-derived entry points and original
control flow. Arithmetic, predicates, list access, assignment, printing, and
console input are sufficient for the first source-level corpus, but their
implementation remains address matched rather than replaced with a new
runtime API.

### 4. Original Compiler

Recover and transliterate the compiler in its observed call structure.
Confirmed public anchors include:

- `10217`: dictionary fragment `COMPIL`;
- `10421`: dictionary fragment `FNCOMP`;
- `03461..05213`: source compiler paths reached by top-level functions;
- `03566..03571`: additional path for captured lexical values;
- `04001..04116`: additional nested-function compiler paths.

The C++ compiler must emit the same POPLAN object and environment formats
consumed by the translated evaluator. It must not be replaced by a separately
designed grammar or AST evaluator.

### 5. Whole-System Conformance

Run the same POP-2 inputs through the historical and C++ systems:

- primitive arithmetic, strings, lists, and conditionals;
- the quine;
- recursive triangular-number variants;
- nested closure and argument-rotation probes;
- the 4x4x4 tic-tac-toe program;
- canonical and reduced Man-or-Boy tests.

Diagnostics, output values, generated object tags, and relevant activation
transitions are comparison points. Host I/O wrapping and the historical batch
greeting are not required to match.

### 6. Closure Fix

After the faithful baseline reproduces the bug, add one isolated correction
to closure-environment selection or restoration. Keep the original behavior
available as a conformance mode.

Validation proceeds in this order:

1. arity 3, `K=2`, returns `1` with dynamic `B` sequence `B0,B1,B0`;
2. arity 4, `K=3`, returns `1`;
3. canonical arity 5, `K=4`, returns `1`;
4. canonical Man-or-Boy at `K=10` returns `-67`.

The fix is acceptable only if the existing quine, primitive, recursion, and
larger-program conformance tests remain unchanged.
