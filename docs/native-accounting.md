# Native compiler and activation storage accounting

Step 1 of `PLAN.md` is complete at the storage/primitive boundary. This document
records the cost model implemented in `include/poplan/native.hpp` and
`src/poplan.cpp`, and the boundaries against which it is tested. It does not
claim that the native reader, functions or recursion have been implemented.
All source addresses are octal; formulas and counts are decimal.

## Compiler storage phases

`NativeCompilation` describes a single compilation unit after reader decisions
and compiler rewrites. It contains typed POP emission operations and two
explicit environment chains. It contains no machine instructions. Literal
operands optionally carry exact `Word48` bits.

| Source boundary | Cost and lifetime | Native implementation |
| --- | --- | --- |
| `06134`, `01167`, `01137` | Two-word interned literal record on a full-word miss; no allocation on a hit | `intern_literal`, including distinct integer/real encodings |
| `05554..05564` | Every literal-table record is a collection root | Runtime literal pool retains its logical blocks |
| `04447..04455` | Two words for each intermediate operation | `native_emission_cost().node_words` |
| `17602..17606` | Two words for each compiler environment-chain item | Temporary blocks in the compilation record |
| `17762`, `17774`, `20002` | Rewritten nodes cost two words; final sizing advances once per non-label operation, excluding labels | Typed emission size table; label has zero code words |
| `05021` | Five-word function descriptor | `NativeCompiledStorage::descriptor` |
| `05025`, `05027`, `05160`, `11464` | Copy each environment chain: zero allocation if empty, otherwise n+1 words including its header | `bindings` and `captures` blocks |
| `05033`, `03531` | Release capture-chain nodes in forward list order before code allocation | Historical size-two bucket receives the nodes in the same order |
| `05035..05040` | One code extent, length from `03645` | Sum of emission contributions plus one terminator word |
| `05124..05125` | Write into existing code extent | No additional allocation per emitted operation |
| `05143` | Final return word | One-word sizing seed, even for an empty function body |
| `05147`, `03531` | Release operation nodes in forward order after code allocation | Same small-bucket order as raw finalizer |
| `17560` compiler table | Binding-chain nodes remain rooted after this finalizer | Retained until `finish_compilation_context`; dropping this context makes nodes collectible, without an invented immediate free |

The existing program-retention API now uses this storage path. Literal records
are interned immediately before their operation node is allocated. A retained
numeric graph therefore consumes historical logical storage, and exhaustion
returns an explicit program error. For `2+2=>`, the compiler-storage contribution
is 20 words: one two-word literal record, four two-word intermediate nodes, a
five-word descriptor and five code words. The intermediate nodes return to the
size-two bucket; `COREUSED` still counts them as used until collection.

This is a compiler-storage checkpoint, **not** an assertion about total heap
usage after the same source passes through the historical reader. Reader scratch
objects, statement scheduling and optimization decisions must be supplied by
the later reader/compiler port. `storage_accounting_complete` reports successful
storage charging for the retained graph. `legacy_accounting_complete` remains
false until the whole session accounts for those additional paths. No unknown
compiler word costs are substituted with zero.

## Emission inventory

The native enum records POP-level emission intent. These entries currently
support storage sizing. The native session also executes binding operations,
comparisons, conditional branches, jumps and labels; see [control evidence](native-control.md). Every listed operation uses a two-word intermediate node.

| Source selector | Native emission | Final code words |
| --- | --- | --- |
| `05052` | evaluate | 1 |
| `05053` | literal | 1 |
| `05054` | branch_zero | 1 |
| `05055` | branch_nonzero | 1 |
| `05056` | jump | 1 |
| `05057` | store_binding | 1 |
| `05060` | return_value | 1 |
| `05061` | evaluate_stack | 1 |
| `05062` | load_capture | 1 |
| `05063` | embedded_value | 1 |
| `05064` | call_binding | 1 |
| `05065` | update_binding | 1 |
| `05066` | transfer_arguments | 1 |
| `05067` | rebuild_arguments | 1 |
| `05070` | label | 0 |
| `05071` | continuation | 1 |
| `05072` | load_binding | 1 |
| `05073` | capture_function | 1 |
| `05076` | jump_capture | 1 |

`05074..05075` are data, not emission selectors. Numeric literal operations now
carry site `05053`; arithmetic and printing calls carry `05064`, each contributing
one code word. Primitive provenance is kept separately. There is no recognition
or decoding of completed BESM instruction words in this implementation.

## Environment and activation costs

The earlier plan cited `11726..11757` for closure rebinding. That cluster actually
implements packed record access/update. Ordinary function binding/restoration is
`03206..03260`; its two-word-per-slot saves, reverse entry order and forward
restoration order determine frame costs. This corrects the source attribution,
without changing the historical closure failure required by later steps.

`native_binding_frame_cost` takes argument/result flags in binding-table order:

- Entry starts with two saved words (previous function and return link).
- Visit slots in reverse order. Each retains two more words. An argument slot
  additionally borrows three temporary words at that point.
- Restoration visits slots forward. A result slot borrows three words before its
  two saved words are removed. The final two-word header is then removed.

Thus retained storage is `2 + 2*n`, but peak storage depends on flag placement.
Entry and exit peaks are computed separately, including the retained words.
They are tested against raw instruction-by-instruction stack high-water marks.
The environment-free `03261` case retains the same two-word header.

`native_argument_frame_cost` covers `20110` and ordinary rebuilding at `20124`:

| Argument count | Retained words | Entry peak | Ordinary rebuild peak |
| --- | --- | --- | --- |
| 0 | 0 | 0 | No transferred area to rebuild |
| 1 | 1 | 1 | 5 |
| n > 1 | n | n+4 | n+4 |

The previous n+1 retained estimate was wrong: `20110` reserves n+1 words but its
restoration pops four words after only three saves, leaving **n**. `20124` saves
four transient words and consumes the n-word area on ordinary return. Its
separate zero-link branch drops one word and pushes the accumulator directly;
it does not use the ordinary rebuild scratch path. Language selection of these
paths belongs to the function port.

`enter_frame` and `leave_frame` maintain a bounded logical area corresponding to
`066001..066777`, before the `067000` watchpoint. Entry checks entry peak; exit
checks exit peak at restoration, so a later failure is not moved prematurely to
call entry. Failed entry/exit leaves the frame ledger unchanged. `unwind_frames`
drops charges and roots without borrowing restoration scratch. Frames contain
native roots and costs; no emulated registers or hardware-stack words exist.
These primitives do not yet execute POP calls, restore bindings or claim the
triangular/Man-or-Boy thresholds. Those remain steps 3–5.

## Verification scope

`poplan-native-accounting` uses only the instruction interpreter as its oracle:

- All 19 emission cases, empty bodies, repeated emission sequences, empty and
  nonempty environment chains are adapted to the original compiler's IR.
- The raw `17774/20002` sizing walk supplies the finalizer's actual `03645` value.
  Native sizes are compared against it, not supplied as the oracle's answer.
- Raw `05007` finalization and native storage construction agree on descriptor,
  code and both environment addresses, every common/small free-list chain and
  raw `10402` `COREUSED` results.
- Literal interning checks exact logical addresses, repeated values, zero and
  different numeric encodings. Literal records remain collection roots.
- Binding frames cover 0–8 slots with varying argument/result flags; argument
  transfer covers 0–8 arguments, and ordinary rebuilding covers 1–8. Raw retained
  sizes, entry/exit peaks and released words match the native costs.
- Native lifecycle probes cover compilation roots, shared captured objects,
  release, partial-construction exhaustion, actual program charging, bounded
  frame entry, late exit failure and explicit unwind.

`poplan-native-storage` additionally compares 32 graph patterns, including
non-aligned arena upper limits. This closes the earlier unverified partial-bitmap
limit case. Other language object kinds, collection-depth diagnostics, reader
scratch allocation order, actual POP call selection and SETPOP behavior remain
outside this storage milestone.

Validation logs are kept in `artifacts/native-accounting/`; full-suite outcomes
are recorded in `native-runtime.md`. Historical profile measurements are unchanged.


## Runtime integration update — 2026-10-10

The top-level execution context now contributes three retained words. For the
established two-load comparison path, the compiler annotates ordinary argument
rebuilding and charges its access extent. `20132` accesses the word at the peak
stack pointer, so addressability needs n+5 words although the pointer peak is
n+4. The original argument-frame costs above remain unchanged. See
[recursion evidence](native-recursion.md) for boundary tests and the remaining
compiler scheduling/diagnostic limits.
