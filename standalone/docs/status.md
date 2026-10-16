# Standalone implementation status

Date: 2026-10-15. The implementation lives entirely in this directory. Existing
machine and compatibility-native code remains independent. This work supersedes
the legacy representation/accounting requirements only for this new executable.

## Implemented and exercised

| Area | Implementation | Verification |
|---|---|---|
| Values and numbers | Explicit tags; int64 checks; binary64 arithmetic, formatting, exact mixed comparisons | C++ runtime tests and numeric conformance cases |
| Dictionary | Interned words, separate identifier records, six-character significance, aliases, mutable cells; CANCEL preserves old compiled bindings | Collision, redeclaration, dynamic restoration, REFOF and retired-identifier collection tests |
| Functions | Explicit VM frames, local/result parameters, recursion, dynamic helper lookup, shared captured cells | Function/nested fixtures, depth 1000, canonical Man-or-Boy K=10 = -67 |
| Storage | Stable handles; tracing collector marks programs, active/saved/captured cells and object graphs; cyclic collection | Rooted and unreachable cycle tests; collection during Man-or-Boy |
| Compiler | Explicit resumable tasks; literals, assignments, computed calls, conditionals, loops, labels, returns, partials, operators and macros | Language fixture, every-byte split tests with GC, example library |
| Generated functions | Partial application/composition, updaters, records, strips, arrays including supplied storage callbacks | Standard-function fixture, nominal class/width tests, custom initializer test |
| Readers | Resumable ITEMREAD/LISTREAD/NUMBERREAD; macros, NONMAC, quoted-word markers, doubled string quotes, Unicode character literals | Signed bounds, nested lists, supplier timing and split-input tests |
| Runtime source | Authoritative PROGLIST/NEXTCH; typed POPVAL/MACRESULTS, interleaved COMPILE, generic CARRYON, source loading | Source replacement/chaining/cursor mutation; POPVAL identity/preservation; supplier ordering; libraries |
| Output | Iterative object formatting, decimal/EXP PRREAL fields, Unicode consumers, GENOUT | Deep acyclic/cyclic objects, quine, output redirection and UTF-8 files |
| Lists | Lazy memoized FNTOLIST, list predicates, updaters, traversals, callback ordering; concatenation shares its second operand | Infinite supplier prefix, deferred effects, mutation/sharing, equality and suspended ITEMREAD supplier |
| Recovery | ERRFUN callbacks, nested-handler guard, active-variable preservation on SETPOP/errors | Custom handler, nested failure, captured cells and lexer error after completed-command effects |
| Host services | Streams, clocks, console/file POPMESS, explicit unsupported-device errors | C++ injected-service test and CLI service cases |
| Workloads | Quine, language/standard functions, Man-or-Boy, recovered examples, fixed-seed tic-tac-toe | Self-contained conformance suite |
| Separation | Separate CMake link graph; no parent includes or image reads | Empty-working-directory runs, forbidden-symbol guard, independent-copy build |

`tests/fixtures` carries local copies so the standalone project does not depend
on its parent even for testing. Golden outputs use modern number/text formatting.
The standard-function fixture omits historical code-conversion calls and checks
`COREUSED`'s documented zero free-space budget. These changes are intentional;
matching these fixtures alone does not establish full language coverage.

The `lib/POPLIB` files are plain UTF-8 copies of recovered `zone1222.pop2`
(DEBUG), `zone1223.pop2` (MEMOFN), and `zone1224.pop2` (EXAMPL), plus `ttt.pop2` (FOURS). No binary library
container, disk-zone loader or historical filesystem is included.

## Remaining work — completeness gate is open

The [184-resident inventory](language-inventory.md) distinguishes implemented
paths, unresolved contracts and intentional modern-host exclusions. This is not
an assertion that each implemented primitive has complete branch coverage.

1. **Syntax and special functions.** SECTION/ENDSEC and SWITCH remain explicit
   unsupported-syntax failures. Full mutable syntax and special-function
   protocols need source-backed semantics and focused tests. The locally
   available manual is Part 2; do not infer Part 1 grammar from generic POP-2.
2. **Diagnostic details.** Modern automatic ERRFUN calls receive the message
   string and integer code **1**, also stored in NUMERR. Callback replacement
   and nested recovery work, but historical error numbers, offending-object
   arguments and suppression flags are not reproduced. Explicit ERRFUN accepts
   its supplied element/number. Host operation-budget exhaustion bypasses the
   callback because further VM operations are prohibited.
3. **Input and output audits.** Source feeds suspend without replay, including
   macros and lazy suppliers. CHARIN still uses an injected blocking stream;
   there is no separate asynchronous console-character feed. Formatting uses
   explicit traversal, but accumulates output text before scheduling consumers;
   fine-grained printer/supplier interleaving remains an audit. Incomplete lexical
   tokens are scanned again when more bytes arrive, without language effects.
4. **Protected identifiers and primitive branch coverage.** Resident functions
   and standard value constants now have protected identifier records. Direct
   assignment, declarations, loop assignment, VALOF updates and REFOF enforce
   protection, while local shadowing and CANCEL/redeclaration work. Full syntax
   identifier classification remains part of the syntax audit. Verify every inventoried updater, protected
   function, cyclic/mutating supplier and invalid argument branch. Current
   conformance includes decimal/EXP formatting, BOUNDSLIST updates, stable
   UPDATER identity, REFOF, word/partial introspection and character literals,
   and separate LENGTH list traversal / DATALENGTH strip component counts with
   invalid-type recovery. Corpus success alone does not close those audits.
5. **Host accounting.** COREUSED remains an approximate host-storage estimate.
   Historical memory budgets, machine code, encoding conversions and monitor
   handoff are intentionally excluded, with explicit errors for unsupported
   operations.

Modern behavior follows the manual where historical bugs disagree: ISLINK is a
nonempty-list predicate, NIL is an interned word, and error reset preserves the
failed invocation's visible variable values. The standard fixture's reference
ISLINK result was changed accordingly; existing backend fixtures are unchanged.

Historical probes saved under the parent `artifacts/standalone-coverage/`
establish POPVAL list preservation, REFOF dynamic visibility/copy/identity,
CANCEL's separation of interned word identity from compiled bindings, and error
variable visibility. These files are evidence, not runtime dependencies.

The component audit unifies DATALIST and APPDATA for words, strings, references
and partial functions; the underlying callable is not a frozen component.
COPY rejects interned words instead of creating noncanonical duplicates.
APPDATA retains the snapshot semantics of the manual §7.3 example
(`DATALIST` followed by `APPLIST`). A historical mutation probe instead observes
later component changes during callbacks. This is an explicit standalone
behavior difference, not historical conformance; other function classes still
need component-layout audits.

UPDATER accepts any element as documented in §7.7, including UNDEF. Explicit
assignment suppresses automatic updater generation; invoking a nonfunction
updater produces a recoverable error. Protected functions still reject updates.
COPY also rejects functions, matching probes for partial, user and standard
functions; it no longer creates copies with cached updaters bound to an original.

Generated updater audit: FNCOMP compositions have no automatic updater, but
accept an explicit one. Partial application captures a separate partial updater
at creation, with independent frozen arguments and underlying function; later
FROZVAL/FNPART changes do not retarget that updater. Historical probes and
conformance tests cover those mutation boundaries and explicit composition updates.

Update dispatch now checks for an updater before entering any ordinary function
or primitive. Unsupported updates (including SIN, PR, constructors and
destructors) recover without executing the callee or leaving ordinary results.
Historical SIN/DESTREF probes and focused conformance cases establish this rule.

Protected identifier evidence (2026-10-15): original dictionary resident function
properties are 040, while CUCHIN, CUCHOUT and ERRFUN are mutable class 001.
Protection follows the identifier's global cell; local parameter/captured cells
remain writable. Compilation rejects protected assignments before executing the
statement. CANCEL detaches the old identifier, so newly declared bindings are
writable while previously compiled code retains its original binding.

Malformed UTF-8 timing (2026-10-15): valid source prefixes are tokenized before
reporting malformed or truncated suffixes. Complete earlier commands execute
once; the reader error is deferred to its source position. Session tests cover
six malformed/truncated encodings at every byte split, with GC and recovery.
