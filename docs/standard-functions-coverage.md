# POP-2 Standard-Function Coverage

The reference used for this audit is R. M. Burstall and R. J. Popplestone,
*POP-2 Reference Manual*, printed pages 214-245. The inspected copy is
`/mnt/c/Users/leob/OneDrive/Documents/POP-2-Reference-Manual.pdf`.
Recognition is checked against the static dictionary records in
`build/poplan.bin`; the executable fixture is also compared with historical
POPLAN under `dispak`.

The manual declares 85 alphabetically named standard functions or routines.
POPLAN recognizes 57 under the same spelling after its six-character name
truncation, supplies 23 through generic operations or renamed facilities, and
has no resident equivalent for five. The manual also declares 13 standard
operations: twelve retain their spelling and exponentiation is written `!` in
POPLAN.

The reference table declares `//` as divide-with-remainder. Its following
prose calls that operation `intdiv`; `INTDIV` is not treated as a second
declared identifier.

## Directly recognized names

| Manual area | Reference spelling -> POPLAN record |
|---|---|
| Items and numbers | `ISCOMPND` -> `ISCOMP` (`01730`); `ISINTEGER` -> `ISINTE` (`01740`); `ISREAL` (`01754`); `LOGAND` (`01774`); `LOGOR` (`02004`); `LOGSHIFT` -> `LOGSHI` (`02010`); `LOGNOT` (`02000`); `INTOF` (`01724`); `REALOF` (`02124`); `BOOLAND` -> `BOOLAN` (`01474`); `BOOLOR` (`01500`); `NOT` (`02054`) |
| Function application | `PARTAPPLY` -> `PARTAP` (`02064`) |
| Records and strips | `RECORDFNS` -> `RECORD` (`02130`); `DATALIST` -> `DATALI` (`01574`); `DATAWORD` -> `DATAWO` (`01600`); `COPY` (`01554`); `STRIPFNS` -> `STRIPF` (`02160`) |
| References | `CONSREF` -> `CONSRE` (`01540`); `DESTREF` -> `DESTRE` (`01614`); `CONT` (`01550`) |
| Pairs | `CONSPAIR` -> `CONSPA` (`01534`); `DESTPAIR` -> `DESTPA` (`01610`); `FRONT` (`01660`); `BACK` (`01470`); `ATOM` (`01464`) |
| Lists | `NULL` (`02060`); `CONS` (`01530`); `DEST` (`01604`); `HD` (`01674`); `TL` (`02200`); `FNTOLIST` -> `FNTOLI` (`01650`) |
| Strips and arrays | `INIT` (`01714`); `SUBSCR` (`02164`); `INITC` (`01720`); `NEWANYARRAY` -> `NEWANY` (`02030`); `NEWARRAY` -> `NEWARR` (`02034`) |
| Words | `CONSWORD` -> `CONSWO` (`01544`); `DESTWORD` -> `DESTWO` (`01620`); `CHARWORD` -> `CHARWO` (`01520`); `MEANING` -> `MEANIN` (`02024`) |
| Functions as data | `FNPROPS` -> `FNPROP` (`01644`); `UPDATER` -> `UPDATE` (`02214`); `FROZVAL` -> `FROZVA` (`01664`); `FNPART` (`01640`); `ISFUNC` (`01734`) |
| Input, output, and evaluation | `POPMESS` -> `POPMES` (`02070`); `INCHARITEM` -> `INCHAR` (`01710`); `CHARIN` (`01510`); `ITEMREAD` -> `ITEMRE` (`01764`); `CHAROUT` -> `CHAROU` (`01514`); `SP` (`02150`); `NL` (`02050`); `PRINT` (`02104`); `MACRESULTS` -> `MACRES` (`02014`); `POPVAL` (`02074`); `SETPOP` (`02140`) |

Long spellings in the left column are valid POPLAN source spellings because
only their first six characters participate in dictionary lookup.
"Directly recognized" describes the source name, not necessarily an identical
calling convention. In particular, POPLAN's `RECORDFNS` and `STRIPFNS` omit
the reference manual's storage-estimate argument; the executable fixture uses
the documented POPLAN two-argument forms.

## Operations

| Reference operation | POPLAN record | Status |
|---|---:|---|
| `<` | `01400` | same spelling |
| `>` | `01404` | same spelling |
| `=<` | `01410` | same spelling |
| `>=` | `01414` | same spelling |
| `+` | `01420` | same spelling |
| `-` | `01424` | same spelling |
| `*` | `01430` | same spelling |
| `/` | `01434` | same spelling |
| exponentiation | `01440` | POPLAN spelling is `!` |
| `//` | `01444` | same spelling |
| `=` | `01450` | same spelling |
| `::` | `01454` | same spelling |
| `<>` | `01460` | same spelling |

## POPLAN replacements

The low-level numeric names are not dictionary identifiers in POPLAN. Its
generic operations dispatch on integer or real operands, and `SIGN` combines
the two reference sign functions.

| Reference names | POPLAN facility | Record |
|---|---|---:|
| `INTADD`, `INTPLUS` | `+` | `01420` |
| `INTSUB`, `INTMINUS` | `-` | `01424` |
| `INTMULT` | `*` | `01430` |
| `INTSIGN` | `SIGN` | `02144` |
| `INTGR`, `INTLE`, `INTGREQ`, `INTLEEQ` | `>`, `<`, `>=`, `=<` | `01404`, `01400`, `01414`, `01410` |
| `REALADD`, `REALPLUS` | `+` | `01420` |
| `REALSUB`, `REALMINUS` | `-` | `01424` |
| `REALMULT` | `*` | `01430` |
| `REALDIV` | `/` | `01434` |
| `REALSIGN` | `SIGN` | `02144` |
| `REALGR`, `REALLE`, `REALGREQ`, `REALLEEQ` | `>`, `<`, `>=`, `=<` | `01404`, `01400`, `01414`, `01410` |
| `SUBSCRC` | `SUBSCC` | `02170` |
| `OUTCHARITEM` | `GENOUT` | `01670` |

`SUBSCRC` cannot merely be truncated: its first six characters collide with
the full-strip selector `SUBSCR`, so POPLAN uses the distinct spelling
`SUBSCC`. `GENOUT` serves the reference output-adapter role but takes both a
printing function and a character consumer, rather than only a consumer.

## Not resident

| Reference name | Finding |
|---|---|
| `NONUNIQUE`, `UNIQUE` | POPLAN has no mode-changing declaration routines |
| `ENDDATA` | record and strip classes cannot be explicitly removed this way |
| `DELITEM` | POPLAN relies on reachability and garbage collection |
| `NEXT` | no non-destructive `DEST` counterpart is resident |

These absences are checked by dictionary name, rather than by compiling an
intentionally failing program.

## Executable coverage

`tests/inputs/standard-functions-coverage.pop2` invokes 55 of the 57 direct
matches and exercises every replacement facility with appropriate integer,
real, structure, function, or text-item operands. It covers references,
pairs, static and dynamic lists, records, strips, arrays, words, function
properties and updaters, partial application, character/text adapters,
printing, macros, `POPVAL`, and `SETPOP`.

`CHARIN` is left as a static recognition check because calling it consumes the
compiler's live source stream. `POPMESS` is also static-only because the
reference explicitly makes its file/device contract operating-system
dependent. The executable output is required to match historical `dispak`,
hybrid C++ execution, and instruction-only C++ execution.

The executable fixture now explicitly distinguishes lists, words, and links
with `ISLIST`, `ISWORD`, and `ISLINK`, checks `SAMEDATA`, reads array bounds,
and exercises the getter side of `FNPART`. It also covers the POPLAN appendix
facilities `CODIPC`, `CODPIC`, `CODIPS`, `CODPIS`, `COREUSED`, and `FNCOMP`;
the two string-code conversions are checked by a three-character round trip.

The test also exposed a semantic-dispatch error in the shared `12043` logical
operation validator: it had absorbed the `LOGAND` continuation and therefore
implemented `LOGOR(4,1)` as zero. The translated routine now stops at the
operation-specific continuation selected through `r1`, preserving the
original shared control flow. The `12052` `LOGAND` tail, `12057` `LOGOR` tail,
and their shared `12053` return are separate semantic entries and have
full-state comparisons against instruction-only execution.

The follow-up fallback audit translated every immutable-image path newly
reached by this fixture, including `01030..01065`, executable templates in
`03315..03410`, list traversal at `07134..07276`, descriptor setup in
`10152..11052`, the `12040` wrapper, and the record selector at `16624`.
The profile fell from 1,388 raw instruction steps to zero with unchanged
normalized output. Full-state semantic-versus-interpreter tests cover
representative entries from each cluster.

The later appendix expansion opens additional paths that were not part of
that zero-fallback measurement. Its first conversion set translates the
complete observed `ISLIST`, `ISWORD`, `SAMEDATA`, and `ISLINK` regions. The
expanded fixture's fallback count falls from 362 steps at 113 resident words
to 305 steps at 96 resident words; all four converted regions have zero raw
steps, and focused tests compare complete machine state and memory with the
instruction-only path at every preserved continuation.

The next conversion translates `BOUNDSLIST`, `COREUSED`, the two adjacent
appendix evaluator wrappers, and `FNCOMP`. It retains the list and descriptor
allocation calls and both invalid-object paths rather than replacing the
operations with host containers. The expanded fixture now executes 191 raw
steps at 39 resident words, with zero fallback in `07433..07464` and
`10402..10443`.

The following conversion implements the packed-string `CODIPS`/`CODPIS` and
single-character `CODIPC`/`CODPIC` family at `15712..15762`. It preserves the
shared packed traversal and character-conversion calls instead of folding
them into a host lookup. The expanded fixture now executes five raw steps at
three resident words, all at `10604..10606`; `15712..15762` has no remaining
instruction-interpreter steps. Focused tests compare complete machine state
and memory at every translated entry and continuation.

The final follow-up converts the `FNPART` getter at `10604..10606`, preserving
the `10672` validator and `03275` POP-stack boundary. A fresh trace of the
expanded fixture has zero raw instruction steps at immutable-image addresses.
