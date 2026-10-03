# Native allocation evidence and implemented boundary

`NativeAllocator` in `include/poplan/native.hpp` / `src/poplan.cpp` implements
logical free-block selection and release. It does not execute instruction words,
load the image, hold emulated registers, or call `Machine`. Arena bounds are
explicit inputs. `NativeRuntime` supplies the recovered startup bounds and owns
object payloads, roots, and collection/retry.

## Evidence map

All addresses below are octal. `build/poplan.lst` and the corresponding `pXXXXX`
methods in `src/machine.cpp` establish these decisions.

| Boundary | Source decision | Native representation / status |
| --- | --- | --- |
| `20660` | Common head comes from word `00017`; extent is word `17010` minus that head | Constructor takes `first` and exclusive `limit`; one initial extent |
| `05430` | Zero-size request returns immediately; nonzero saves size/link and calls `05447` | `zero_size` is separate from allocation failure; native retry uses explicit roots, not a hardware frame |
| `05447..05454` | Compare size with data `06102 = 021`; try exact-size head `05502 + size` only below cutoff | Ordered bucket for each size `1..020`; newest returned block is reused first |
| `05463..05467` | Start at common head `05502`, skip blocks smaller than request | Ordered common extents, first sufficient block; no best-fit search |
| `05470..05474` | Subtract request from block's upper-half size; result address is block base plus remainder | Split from high end, leave shortened extent in the same list position |
| `05475..05500` | Exact match splices next pointer into predecessor; preserve predecessor's size | Erase exactly the selected extent, retaining all other list order and sizes |
| `05455..05461` | Write data `02213 = 06500000000000000` into every allocated word | Oracle verifies initialization; native reference/pair payloads default to unbound |
| `05462` | Return zero block address on failed search | `collection_required`; never silently grow beyond the logical arena |
| `05433..05435` | On failure call `05523`, then retry the same size | `NativeRuntime` collects once and retries; constructor arguments remain roots |
| `05440..05441` | Failed retry prints the required-block message and transfers to `20715` | Native result is `exhausted`; POP diagnostics/recovery remain open |
| `03506..03515` | Zero size returns; size at least `021` prepends extent to common head | Common release is LIFO, without coalescing adjacent blocks |
| `03516..03520` | Replace the selected size-list head and retain its previous head in returned block | Small release is LIFO; logical links represented by ordered blocks |
| `10402..10414` | Sum only common extents; return tagged used and free counts | `core_usage()` returns those two numerical counts, excluding small buckets from free |

`COREUSED` is **not** host live-allocation size. In particular, freeing a
2-word block returns it to its size-specific chain but does not reduce the
reported used count. Those words remain reusable for another 2-word request.
Collection rebuilds free gaps into the common list and clears the size buckets,
so it can change the reported used count without changing reachable payloads.

Logical addresses and sizes are checked, and nonzero release requires a live
allocation with the exact address and size. Invalid ownership is an internal
implementation error. Host allocation failures remain host failures; they are
not disguised as POP diagnostics. A failed search and a zero request leave the
logical state untouched. The allocator's live ledger is ownership metadata,
not an alternative definition of historical `COREUSED`.

## Differential coverage

`ctest --test-dir build/cpp -R poplan-native-allocator --output-on-failure`
executes the existing `tests/cpp/machine_test.cpp` with `--native-allocator`.
It uses the extracted image only in the independent, instruction-only oracle.
The native allocator receives explicit bounds and ordinary sizes/addresses.

Each allocation/release compares every common and size-specific chain, logical
result address, extent and `COREUSED` projection. Successful raw allocation also
checks every initialized payload word. The test asserts zero translated-routine
execution by the oracle, and conservation of all logical words. It does not
compare machine registers to native state.

Focused cases include zero requests, zero releases, newest-first small reuse,
common exact removal, high-end splitting after a too-small predecessor, both
sides of the `020`/`021` cutoff, and failed allocation. A deterministic sequence
of 240 mixed operations then exercises fragmentation, release and reuse, with
the same checkpoints after every operation. Invalid native ownership/request
width is rejected without mutation, and an empty arena cannot allocate via host
storage.

## Runtime and collection (2026-10-02)

`NativeRuntime` owns the dictionary, allocator, value stack, retained programs,
frame-root records and object payloads. References cost one logical word; pairs
cost two. Object handles increase monotonically independently of logical
addresses; reclamation never makes a stale handle refer to a different object.
There are no native activation semantics yet: frame-root records are a storage
interface for the later explicit call-frame implementation.

At the first input boundary, instruction-only startup has arena
`033064..065777`, one two-word reservation at `065776`, and common extent
`033064` of length `032712`. The default runtime reserves this extent without
pretending its historical compiler payload is a native function. The explicit
bounds constructor used by primitive tests omits the startup reservation.

| Collector boundary | Recovered decision / native treatment |
| --- | --- |
| `05523..05542` | Save caller state and enter supervisor collector; native collection requires no emulated supervisor or hardware frame |
| `05543..05553` | Initialize mark bitmap and clear free-list heads |
| `05554..05621` | Resident references, dictionary chains and compiler tables are roots; native dictionary records/bindings and retained programs are roots |
| `05622..05634` | Hardware and POP stack roots; native frame/value roots and pending constructor arguments replace these representations |
| `05722..06017` | Traverse tagged object graphs; native iterative traversal currently implements references and pairs, including cycles and sharing |
| `06020..06046` | Test and mark logical extents; native collection marks complete owned blocks |
| `05635..05701` | Ascending, nonmoving sweep rebuilds common list and coalesces gaps; size bins become empty |
| `05654..05672` | Sweep stops before the bitmap word containing the exclusive upper bound; a free run is linked only when a marked word terminates it |

The last rule discards an unterminated free tail. It is deliberately preserved
and represented as `unavailable`, which counts as used in `COREUSED`. It is not
host-live storage. Default arena limits and differential fixtures end on bitmap
boundaries; arbitrary partial-bitmap limits have not been differentially proven.

`poplan-native-storage` compares startup and 16 cyclic/shared object graphs with
instruction-only collection, checking every rebuilt free extent, bucket clearing,
subsequent allocation address and accounting. Roots exercise values, programs,
frames and bindings. Separate tests execute the complete raw
`05430 -> 05523 -> 05447` wrapper for both successful retry and exhaustion and
compare native outcomes at the return/diagnostic boundary. Native-only checks
cover temporary argument roots, released programs, dynamic words, reclamation,
and runtime-owned numeric execution. The existing allocator differential remains
responsible for exact allocation/release and raw `COREUSED` checks.

## Allocation-cost inventory

These are costs at source boundaries, not a claim that native operations each
have the same cost. Addresses and sizes in this table are octal unless expressed
as a formula.

| Source | Logical cost established | Native status |
| --- | --- | --- |
| `01137` / `01150` dictionary miss | Four words | Implemented and differential-tested |
| `10277` CONSREF | One word | Native reference storage implemented |
| `05215` / `05217` pair | Two words | Native pair storage implemented |
| `05021` function/compiler descriptor | Five words | Recovered from allocation request; constructor not ported |
| `05035..05040` generated code extent | Length in compiler sizing word `03645` | Aggregate cost depends on original sizing pass; not assigned to probe operations |
| `05124..05125` emission | Writes into the already allocated extent | No separate allocation per emitted word |
| `11464..11474` descriptor wrapper | Zero for zero input address; otherwise input low address plus one | Recovered allocation formula; environments not yet lowered |
| `20110..20123` argument transfer | No stack growth for zero; one word for one argument; n+1 retained words for n greater than one, plus three temporary saved words | Source stack movement recovered; not a complete activation cost |
| `20124..20140`, `11726..11731`, `11755..11757` | Activation rebuilding and captured binding costs depend on descriptors and calling context | Unresolved aggregate costs; block recursive exhaustion claims |

Probe operations carry primitive provenance and emitter-family annotation
`05052`; this is not a resolved per-operation emission site. `legacy_words` is
explicitly unknown, never zero by default. `NativeExecutionResult` reports
`legacy_accounting_complete = false`. No finished instruction words are decoded
or recognized to infer these operations.

Remaining work is the complete compiler sizing/lowering map, environment and
activation accounting, other object layouts, collection depth diagnostics,
SETPOP recovery, and recursive/closure exhaustion thresholds. The runtime now
owns numeric execution, but the probe does not yet charge historical compiled
program or activation costs. These limitations prevent claiming full allocation
or native language conformance.
