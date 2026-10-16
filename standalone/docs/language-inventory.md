# Standalone language inventory — 2026-10-15

This is a completeness ledger, not a coverage percentage. It includes all 184
resident dictionary spellings (six-character significance). The evidence snapshot
is the explicit resident table in the parent `src/poplan.cpp` and its generated
`docs/native-resident-inventory.md`; octal numbers below identify dictionary
records only and have no execution role in this subproject. The old inventory's
implementation statuses concern an earlier backend and are not reused here.

Manual evidence is `pop-2-opt.md`, **Description of POPLAN, Part 2** (1978):
sections 6–7 for objects/lists/functions, 8.4 for output, 8.5 for readers and source
lists, 9.1–9.3 for macros/recovery, and appendices 1–2 for additional primitives
and devices. Part 1 syntax must be recovered from compiler source or experiments;
a dictionary spelling is not sufficient evidence for inventing grammar.

“Implemented; audit open” means there is an implementation and corpus coverage,
not that every branch or updater has a focused test. Shared tests are
`tests/conformance.py`, the language/standard-functions fixtures,
`tests/runtime_test.cpp`, `tests/session_test.cpp` and `tests/interactive.py`.
The session suite tests every two-way byte split and bytewise feeds, collecting
while suspended. **Every Open, Partial, Unresolved or audit-open row prevents a
claim of complete language coverage.** Excluded rows are deliberate modern-host
boundaries, not fallback paths.

| Record | Spelling | Status | Implementation / evidence and test scope |
| --- | --- | --- | --- |
| `01400` | `<` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01404` | `>` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01410` | `=<` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01414` | `>=` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01420` | `+` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01424` | `-` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01430` | `*` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01434` | `/` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01440` | `!` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01444` | `//` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01450` | `=` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01454` | `::` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01460` | `<>` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01464` | `ATOM` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01470` | `BACK` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01474` | `BOOLAN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01500` | `BOOLOR` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01504` | `BOUNDS` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01510` | `CHARIN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01514` | `CHAROU` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01520` | `CHARWO` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01524` | `COMPIL` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01530` | `CONS` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01534` | `CONSPA` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01540` | `CONSRE` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01544` | `CONSWO` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01550` | `CONT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01554` | `COPY` | Implemented; audit open | Manual §6.1/7.3 and historical word probe; rejects interned words and functions, tests shallow pair sharing and distinct identity. Other structure branches remain open. |
| `01560` | `CUCHIN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01564` | `CUCHOU` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01570` | `DATALE` | Implemented; focused type audit | Manual §6.3; strip-only component count, Unicode strings, custom strips and invalid classes tested in `tests/conformance.py`. |
| `01574` | `DATALI` | Implemented; audit open | Manual §7.4/7.7; shared component extraction with APPDATA, Unicode characters, references and frozen arguments. Other function classes remain open. |
| `01600` | `DATAWO` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01604` | `DEST` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01610` | `DESTPA` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01614` | `DESTRE` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01620` | `DESTWO` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01624` | `ERASE` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01630` | `ERRFUN` | Partial | VM callback and nested recovery implemented; modern message/code 1, historical numbers/offending-object/suppression contract not complete. |
| `01634` | `FALSE` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `01640` | `FNPART` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01644` | `FNPROP` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01650` | `FNTOLI` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01654` | `FORALL` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `01660` | `FRONT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01664` | `FROZVA` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01670` | `GENOUT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01674` | `HD` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01700` | `IDENTF` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01704` | `IDENTP` | Partial | Built-in syntax, macros and operators; full mutable special-function protocol absent. |
| `01710` | `INCHAR` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01714` | `INIT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01720` | `INITC` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01724` | `INTOF` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01730` | `ISCOMP` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01734` | `ISFUNC` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01740` | `ISINTE` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01744` | `ISLINK` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01750` | `ISLIST` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01754` | `ISREAL` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01760` | `ISWORD` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01764` | `ITEMRE` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `01770` | `JUMPOU` | Excluded host interface | 12022 listing uses console/monitor extracodes; standalone reports unsupported historical monitor handoff. |
| `01774` | `LOGAND` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02000` | `LOGNOT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02004` | `LOGOR` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02010` | `LOGSHI` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02014` | `MACRES` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02020` | `MAPLIS` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02024` | `MEANIN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02030` | `NEWANY` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02034` | `NEWARR` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02040` | `NEXTCH` | Implemented; audit open | Authoritative remaining source chains; test source replacement and chaining. |
| `02044` | `NIL` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02050` | `NL` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02054` | `NOT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02060` | `NULL` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02064` | `PARTAP` | Implemented; audit open | Historical updater probes and `tests/conformance.py`: independent frozen partial updaters, no implicit composition updater, explicit replacement. Further branches remain open. |
| `02070` | `POPMES` | Modern replacement | Console and UTF-8 host files; historical device descriptors rejected. |
| `02074` | `POPVAL` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02100` | `PR` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02104` | `PRINT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02110` | `PRREAL` | Implemented; audit open | Decimal field width and EXP significant precision; modern IEEE rendering. |
| `02114` | `PROGLI` | Implemented; audit open | Authoritative list cursor shared by compiler/readers, with in-place cursor mutation and deferred input. |
| `02120` | `PRSTRI` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02124` | `REALOF` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02130` | `RECORD` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02134` | `SAMEDA` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02140` | `SETPOP` | Implemented; audit open | Explicit and automatic reset preserve active variable values and restore standard source/output. Nested/source recovery branches need further audit. |
| `02144` | `SIGN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02150` | `SP` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02154` | `STACKL` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02160` | `STRIPF` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02164` | `SUBSCR` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02170` | `SUBSCC` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02174` | `TERMIN` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02200` | `TL` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02204` | `TRUE` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02210` | `UNDEF` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02214` | `UPDATE` | Implemented; audit open | Manual §7.7 and historical probes: arbitrary element assignment/readback, clearing cached defaults, replacement, invocation errors and protection tested. Call dispatch rejects unpaired functions before ordinary effects; generated updater branch audit remains open. |
| `02220` | `(` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02224` | `)` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02230` | `(%` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02234` | `%)` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02240` | `.` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02244` | `,` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02250` | `;` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02254` | `[` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02260` | `]` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02264` | `[%` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02270` | `%]` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02274` | `:` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02300` | `->` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02304` | `=>` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02310` | `&` | Partial | Accepted as numeric exponent separator; other syntax uses need evidence. |
| `02314` | `AND` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02320` | `CANCEL` | Implemented; audit open | Separate identifier records preserve old compiled bindings while word interning/VALOF use the new binding. Split/GC tests and historical probe. Restricted to top-level compilation. |
| `02324` | `CLOSE` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02330` | `ELSE` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02334` | `ELSEIF` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02340` | `END` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02344` | `ENDSEC` | Open | Paired section boundary absent. |
| `02350` | `EXIT` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02354` | `FUNCTI` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02360` | `GOON` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02364` | `GOTO` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02370` | `IF` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02374` | `LAMBDA` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02400` | `LOOPIF` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02404` | `MACRO` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02410` | `NONMAC` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02414` | `NONOP` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02420` | `OPERAT` | Partial | Operator precedence declarations; full special-function syntax absent. |
| `02424` | `OR` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02430` | `RETURN` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02434` | `SECTIO` | Open | Source handler exists; standalone section binding semantics absent. |
| `02440` | `SWITCH` | Unresolved | Resident spelling alone does not establish executable syntax; binding is UNDEF. |
| `02444` | `THEN` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02450` | `VARS` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02454` | `"` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02460` | `COMMEN` | Implemented; audit open | `src/compiler.cpp` / lexer; language fixture and split compiler tests. |
| `02464` | `APPDAT` | Implemented; audit open | Manual §7.3 DATALIST/APPLIST example; partial arguments exclude callable, empty partial and Unicode/reference cases tested. Callback mutation uses a snapshot; historical implementation reads live components. |
| `02470` | `APPLIS` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02474` | `APPLY` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02500` | `ARCTAN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02504` | `CARRYO` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02510` | `COPYLI` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02514` | `COREUS` | Modern replacement | Returns unspecified budget 0 and estimated host bytes; no historical accounting. |
| `02520` | `COS` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02524` | `EQUAL` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02530` | `EXP` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02534` | `FNCOMP` | Implemented; audit open | Historical updater probes and `tests/conformance.py`: independent frozen partial updaters, no implicit composition updater, explicit replacement. Further branches remain open. |
| `02540` | `LENGTH` | Implemented; audit open | Manual §7.3; separate list traversal, NIL/nested lists, memoized supplier calls and cyclic-list rejection tested in `tests/conformance.py`; mutating suppliers still need audit. |
| `02544` | `LIBRAR` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02550` | `LISTRE` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02554` | `LOG` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02560` | `NUMBER` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02564` | `POPTIM` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02570` | `PRBIN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02574` | `PROCT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02600` | `REV` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02604` | `SIN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02610` | `SQRT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02614` | `TAN` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02620` | `VALOF` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02624` | `SYNTAX` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02630` | `REAL` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02634` | `INTEGE` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02640` | `WORD` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02644` | `STRIP` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02650` | `CSTRIP` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02654` | `PAIR` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02660` | `REF` | Implemented; audit open | `src/runtime.cpp` bootstrap; type/value conformance tests. |
| `02664` | `ClYv` | Excluded host interface | Historical operating-system control; no device/address emulation. |
| `02670` | `/=` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02674` | `REFOF` | Implemented; audit open | Historical probes and 06457/06464 listing: dynamic identifier reference; repeated identity, mutation, copying, cancellation and collection tests. |
| `02700` | `NTERM` | Implemented; audit open | Explicit initial UNDEF, matching resident bootstrap; no terminal-device number emulation. |
| `02704` | `NUMERR` | Partial | Automatic modern error stores code 1; historical catalog and suppression semantics excluded from this implementation so far. |
| `02710` | `INDEC` | Partial | Initialized; all historical interactive flag semantics not established. |
| `02714` | `POPDAT` | Implemented; audit open | `src/primitives.cpp`; standard-function/conformance corpus; individual edge contracts require audit. |
| `02720` | `CODIPC` | Excluded encoding interface | Explicit error: Unicode scalar values replace historical character-code conversion. |
| `02724` | `CODPIC` | Excluded encoding interface | Explicit error: Unicode scalar values replace historical character-code conversion. |
| `02730` | `CODIPS` | Excluded encoding interface | Explicit error: Unicode scalar values replace historical character-code conversion. |
| `02734` | `CODPIS` | Excluded encoding interface | Explicit error: Unicode scalar values replace historical character-code conversion. |

## Cross-cutting gates

- Text values: POPVAL/MACRESULTS preserve numeric tags and string handles;
  quoted-word markers and doubled string quotes have focused tests.
- Source control: PROGLIST and NEXTCH now drive the compiler and ITEMREAD;
  source-cursor identity, POPVAL preservation and GOON have focused tests.
- Dynamic lists: supplier calls, mutation, concatenation sharing, list consumers,
  callback ordering and suspended reads are tested. Printer supplier/output
  interleaving and every unusual cyclic/mutating supplier branch remain audits.
- Binding identity: corrected closures are retained; CANCEL/redeclaration now uses
  identifier records independent of interned words and current name lookup; old
  compiled bindings, references and collection have focused tests.
- Diagnostics: modern message text is intentional; ERRFUN callbacks, nested recovery and
  automatic SETPOP variable visibility are implemented. Historical numeric error
  catalog, offending-object payloads and suppression remain outside current coverage.
- Host input: source feeds resume without replay; direct CHARIN uses the injected
  blocking stream and has no separate asynchronous console channel yet.
- Verification: independent-copy build and no-image/no-Machine link guard remain
  required; corpus success does not close the per-feature audits above.

Protected identifier audit (2026-10-15): resident function bindings and standard
value constants carry protection on their identifier records. Compiler checks
cover assignment, declaration and FORALL; runtime checks cover VALOF/MEANING
updates and REFOF. Local shadowing and CANCEL/redeclaration are tested separately.
Syntax identifier classification remains open. Evidence: dictionary properties
and `artifacts/standalone-coverage/protection-*-historical.out` in the parent.
