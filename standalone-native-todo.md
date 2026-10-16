# Standalone native POPLAN: remaining work

Status: 2026-10-16. Applies to `standalone/poplan-native`, relative to documented
POPLAN language behavior and the BESM-6 reference implementation. It does **not**
list missing features in historical POPLAN or untranslated hybrid-backend code.

Sources: [status matrix](standalone/docs/status.md),
[184-resident ledger](standalone/docs/language-inventory.md),
[architecture](standalone/docs/architecture.md), and
[verification history](docs/reports/standalone-native.md).

“Missing” means an implementation is absent. “Unresolved” means the contract
still needs evidence. “Audit” means implemented behavior has not been verified
exhaustively; it does not mean the whole feature is missing. This list covers all
currently recorded gaps, not a guarantee that no undiscovered defects remain.

## 1. Missing or unresolved language behavior

- [ ] **SECTION / ENDSEC:** recover and implement section binding semantics,
  scope entry/exit, nesting and recovery behavior. Both currently fail explicitly
  as unsupported syntax. Establish the grammar from the original compiler or
  Part 1 of the manual before implementing it.
- [ ] **SWITCH:** determine whether and how the resident spelling is executable
  language syntax. Its original binding is UNDEF; its presence in the dictionary
  alone is insufficient evidence. Standalone currently rejects it explicitly.
- [ ] **Mutable syntax and special functions:** finish the contracts represented
  by IDENTPROPS (`IDENTP`) and OPERATION (`OPERAT`). Built-in syntax recognition,
  macros and operator precedence declarations work; the full protocol remains
  unresolved. Distinguish language-visible facilities from BESM-only function
  representations before deciding what must be ported.
- [ ] **Syntax identifier classification:** establish and implement remaining
  syntax-word properties, protection and declaration/cancellation rules, including
  interactions with IDENTPROPS, macros and operator declarations. Ordinary resident
  function/value protection is already implemented; do not list it as missing.
- [ ] **Other uses of `&`:** exponent-separator syntax works. Establish whether
  additional language uses are required, then implement and test those established
  by evidence; do not invent uses from generic POP-2 grammars.
- [ ] **INDEC:** it is initialized, but the historical interactive flag's complete
  language-visible semantics and relevant standalone behavior are not established.

Evidence limitation: the locally available `pop-2-opt.md` is **Part 2**. Part 1
syntax needs additional documentation or recovery from compiler listings/probes.

## 2. Implemented behavior needing further semantic audits

- [ ] **Every resident branch:** close each “Implemented; audit open” row in the
  ledger with source evidence and focused tests. The complete spelling list is
  included below so generic corpus coverage cannot hide an open entry.
- [ ] **Primitive contracts:** audit argument types, bounds, argument order,
  multiple results, stack effects, aliasing, mutation, rejected updates and error
  recovery for every supported primitive. Include protected-function component
  updates and interactions with identifier protection.
- [ ] **Function components and copying:** finish DATALIST/APPDATA component
  contracts for other function classes and COPY behavior for remaining structures.
  Words, strings, references and partial frozen components have focused coverage;
  COPY rejects words and functions. Do not assume all object fields are exposed
  language components.
- [ ] **Generated functions/updaters:** finish the branch audit for records,
  strips, arrays, partial application, composition and explicitly assigned updaters,
  including custom storage callbacks, invalid arguments, mutation and recovery.
  Independent partial updater arguments, absence of implicit composition updaters,
  arbitrary UPDATER values and rejection of unpaired update calls already work.
- [ ] **Lazy and mutable lists:** cover unusual cyclic/mutating/reentrant supplier
  cases, invalid list shapes, predicate behavior and traversal ordering. LENGTH's
  normal/lazy/cyclic cases are tested, but mutating suppliers remain an audit.
  Check that retries never repeat supplier or callback effects and preserve sharing.
- [ ] **Source/compiler combinations:** extend edge coverage for quote markers,
  macros, NONMAC, mutable operators, POPVAL/MACRESULTS, COMPILE/CARRYON and source
  replacement/chaining, especially combinations with suspension, cancellation,
  recovery and collection. These features are implemented, not wholesale omissions.
- [x] **Output consumer rebinding (completed 2026-10-15):** each output character
  reloads CUCHOUT, so a consumer can redirect subsequent characters, including
  during nested printing and GENOUT. Tests cover all output primitives, invalid
  replacement consumers, Unicode, byte-split input suspension and collection.
  Manual §8.3 and historical PRSTRING probes establish the per-character lookup.
- [x] **Streaming printing and callbacks (completed 2026-10-15):** explicit print
  tasks interleave list forcing, character reads and output callbacks. Historical
  `07704..07727` and `10005..10014`, instruction-only execution and historical
  probes establish bracket-before-forcing, saved-tail traversal, live tail-content
  mutation and progressive string reads. Tests include input suspension, GC of
  detached saved tails, nested printing, partial output on failure, cycles, deep
  objects and operation limits for infinite lazy lists.
- [x] **Printer argument types and structure labels (completed 2026-10-16):**
  PRSTRING accepts literal strings and built-in INITC character strips, rejecting
  other values before output or list forcing. PR/PRINT/=> print ordinary pairs,
  references and non-character strips/records through shared DATAWORD lookup;
  function labels use FUNCTI. COPY preserves the plain-pair/list-node distinction.
  Historical `07773..10001`, `10025..10030`, manual §8.4 and historical/raw
  probes establish the cases. Character strips retain progressive Unicode
  reads, callback suspension and GC coverage.
- [ ] **Ordinary pairs versus common lists:** finish the access/comparison branch
  audit now that construction kind is retained for printing and list predicates.
  Historical HD rejects an ordinary CONSPAIR object and EQUAL distinguishes it
  from a common-list node; native HD/TL/NULL and EQUAL still share broader paths.
  SAMEDATA can be true for the two kinds because both have DATAWORD PAIR.
- [x] **Malformed UTF-8 timing (completed 2026-10-15):** complete commands before
  malformed bytes now execute before the deferred reader error. The lexer retains
  the valid UTF-8 prefix without inventing an EOF at its boundary. Session tests
  cover invalid lead/continuation bytes, overlong encodings, surrogates, out-of-range
  scalars and truncated EOF, at every two-way byte split with GC. They assert
  exactly-once prior output, a single diagnostic at the correct location and
  successful subsequent input. See the verification report's UTF-8 timing entry.
- [ ] **Resumption, unwinding and GC:** exercise all newly covered branches under
  byte-split feeds and collection, including nested source contexts, callbacks,
  saved/captured bindings, error handlers and object cycles. Existing tests cover
  many such paths, not every resident combination.

## 3. Diagnostic compatibility: clarify the remaining boundary

Modern diagnostic wording is an accepted standalone choice. Replacing it with
historical text is **not** required by the current contract. ERRFUN callbacks,
nested-handler recovery and SETPOP variable visibility are implemented.

The ledger still marks ERRFUN and NUMERR partial because these historical details
are not reproduced:

- [ ] Resolve which historical numeric error distinctions, if any, remain part of
  the desired standalone language interface. Automatic ERRFUN currently receives
  a message string and generic integer **1**, also stored in NUMERR.
- [ ] Resolve offending-object callback arguments and diagnostic suppression/flag
  behavior separately from message wording. Implement required behavior or record
  an explicit modern replacement; do not leave it ambiguously “partial”.
- [ ] Audit remaining nested diagnostic, reader/lexer error, EOF and recovery
  branches against the chosen modern contract.

Explicit ERRFUN accepts its supplied element/number. Host operation-budget
exhaustion bypasses ERRFUN because no further VM operations may run; that is an
intentional host-limit behavior, not missing historical instruction execution.

## 4. Standalone runtime enhancements, not missing BESM language features

- [ ] **Asynchronous character input:** CHARIN uses an injected blocking stream.
  There is no independent asynchronous console-character feed. If retained as a
  runtime objective, add a resumable channel without replaying callbacks; source
  `feed()` suspension already works.
- [ ] **Incremental-reader efficiency:** incomplete lexical tokens are rescanned
  when bytes arrive, and some finite-list traversals restart after forcing another
  lazy node. Effects are memoized. Audit scaling and replace repeated work with
  explicit cursor state where necessary; this is not evidence of missing syntax.
  Progressive string printing also decodes the current string for each character
  to observe Unicode mutations; audit its scaling without caching stale contents.

## 5. Known differences and deliberate exclusions

These are **not automatic implementation TODOs** under the standalone contract:

| Area | Current standalone decision |
| --- | --- |
| Numbers and identity | Tagged int64 / IEEE binary64, native stable handles; no BESM number encoding or logical-address reproduction. |
| Closures | Corrected shared captured cells; do not reproduce the original Man-or-Boy closure bug. |
| Memory / COREUSED | Host storage and collection; COREUSED reports unspecified free budget 0 and estimated host bytes. No historical allocation/exhaustion accounting, process-RSS guarantee or exact capacity. |
| APPDATA mutation ordering | Uses the manual's DATALIST/APPLIST snapshot behavior. Historical execution reads components live between callbacks. Preserve this explicit difference unless the contract is changed. |
| POPMESS | Console and UTF-8 host files supported. Historical memory/disk/device descriptors remain unsupported and must fail explicitly. |
| POPLIB | Plain UTF-8 source libraries; no binary container, disk-zone loader or DIMON filesystem compatibility. |
| Character conversions | Unicode replaces CODIPC/CODPIC/CODIPS/CODPIS historical encodings; those interfaces fail explicitly. |
| Monitor / devices | JUMPOUT (`JUMPOU`) and the resident spelling `ClYv` are excluded historical host interfaces. No terminal-number/device emulation for NTERM. |
| Machine execution | No BESM instructions, generated machine code, historical PCs, image loading or fallback to Machine. |

Keep these differences explicit in tests and documentation. A demand for exact
BESM compatibility would be a scope change, not evidence that standalone should
silently emulate the machine.

## 6. Completion and verification gates

- [ ] For each required feature, record its contract, source/listing or historical
  evidence, implementation, focused tests and any deliberate deviation.
- [ ] Replace generic audit-open statuses with concrete branch coverage; resolve
  every Open, Unresolved and Partial row, or classify it as an intentional exclusion
  with a clear reason. Do not equate fixture success with language completeness.
- [ ] Keep ordinary/historical comparisons distinct from deliberate modern behavior.
  Test values, stack effects, identity/sharing, mutation, callback order and recovery,
  not output text alone.
- [ ] After implementation milestones run standalone tests and ASan/UBSan with
  leak detection disabled, reference CTest, Make smoke suites, historical workloads
  through `tools/run-dispak.sh`, and whitespace checks. Inspect final exit statuses.
- [ ] Maintain independent-copy build/install checks and no-image/no-Machine guards.
  Revalidate suspension and GC for new continuations.
- [ ] Keep reports under `docs/reports/`, experimental evidence under `artifacts/`
  or `/tmp`, and preserve earlier measurements. Keep native-operation counts
  separate from historical machine-step counts.

Current verified baseline: **163 conformance checks**, **4/4 standalone tests**,
**4/4 standalone sanitizer tests**, **23/23 reference tests**, and Make/historical
workload checks pass. These results establish the tested baseline, not completeness.

## 7. Exact resident-ledger snapshot

The ledger has 184 rows: 166 implemented-but-audit-open, six partial, two open,
one unresolved, one with a focused type audit, two modern replacements, and six
excluded interfaces. Counts describe documentation status, not code coverage.

### Implemented; audit open (166)

`<`, `>`, `=<`, `>=`, `+`, `-`, `*`, `/`,
`!`, `//`, `=`, `::`, `<>`, `ATOM`, `BACK`, `BOOLAN`,
`BOOLOR`, `BOUNDS`, `CHARIN`, `CHAROU`, `CHARWO`, `COMPIL`, `CONS`, `CONSPA`,
`CONSRE`, `CONSWO`, `CONT`, `COPY`, `CUCHIN`, `CUCHOU`, `DATALI`, `DATAWO`,
`DEST`, `DESTPA`, `DESTRE`, `DESTWO`, `ERASE`, `FALSE`, `FNPART`, `FNPROP`,
`FNTOLI`, `FORALL`, `FRONT`, `FROZVA`, `GENOUT`, `HD`, `IDENTF`, `INCHAR`,
`INIT`, `INITC`, `INTOF`, `ISCOMP`, `ISFUNC`, `ISINTE`, `ISLINK`, `ISLIST`,
`ISREAL`, `ISWORD`, `ITEMRE`, `LOGAND`, `LOGNOT`, `LOGOR`, `LOGSHI`, `MACRES`,
`MAPLIS`, `MEANIN`, `NEWANY`, `NEWARR`, `NEXTCH`, `NIL`, `NL`, `NOT`,
`NULL`, `PARTAP`, `POPVAL`, `PR`, `PRINT`, `PRREAL`, `PROGLI`, `PRSTRI`,
`REALOF`, `RECORD`, `SAMEDA`, `SETPOP`, `SIGN`, `SP`, `STACKL`, `STRIPF`,
`SUBSCR`, `SUBSCC`, `TERMIN`, `TL`, `TRUE`, `UNDEF`, `UPDATE`, `(`,
`)`, `(%`, `%)`, `.`, `,`, `;`, `[`, `]`,
`[%`, `%]`, `:`, `->`, `=>`, `AND`, `CANCEL`, `CLOSE`,
`ELSE`, `ELSEIF`, `END`, `EXIT`, `FUNCTI`, `GOON`, `GOTO`, `IF`,
`LAMBDA`, `LOOPIF`, `MACRO`, `NONMAC`, `NONOP`, `OR`, `RETURN`, `THEN`,
`VARS`, `"`, `COMMEN`, `APPDAT`, `APPLIS`, `APPLY`, `ARCTAN`, `CARRYO`,
`COPYLI`, `COS`, `EQUAL`, `EXP`, `FNCOMP`, `LENGTH`, `LIBRAR`, `LISTRE`,
`LOG`, `NUMBER`, `POPTIM`, `PRBIN`, `PROCT`, `REV`, `SIN`, `SQRT`,
`TAN`, `VALOF`, `SYNTAX`, `REAL`, `INTEGE`, `WORD`, `STRIP`, `CSTRIP`,
`PAIR`, `REF`, `/=`, `REFOF`, `NTERM`, `POPDAT`.

### Implemented; focused type audit (1)

`DATALE`.

### Partial (6)

`ERRFUN`, `IDENTP`, `&`, `OPERAT`, `NUMERR`, `INDEC`.

### Excluded host interface (2)

`JUMPOU`, `ClYv`.

### Modern replacement (2)

`POPMES`, `COREUS`.

### Open (2)

`ENDSEC`, `SECTIO`.

### Unresolved (1)

`SWITCH`.

### Excluded encoding interface (4)

`CODIPC`, `CODPIC`, `CODIPS`, `CODPIS`.

`DATALE` (DATALENGTH) has a focused type audit; that status alone is not a
claim of exhaustive language completeness. Six-character spellings above match
the resident ledger. Update this snapshot when that ledger changes.
