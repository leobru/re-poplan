# Native allocation evidence and implemented boundary

`NativeAllocator` in `include/poplan/native.hpp` / `src/poplan.cpp` implements
logical free-block selection and release. It does not execute instruction words,
load the image, hold emulated registers, or call `Machine`. Arena bounds are
explicit inputs; they are not yet the native runtime's bootstrap policy.

## Evidence map

All addresses below are octal. `build/poplan.lst` and the corresponding `pXXXXX`
methods in `src/machine.cpp` establish these decisions.

| Boundary | Source decision | Native representation / status |
| --- | --- | --- |
| `20660` | Common head comes from word `00017`; extent is word `17010` minus that head | Constructor takes `first` and exclusive `limit`; one initial extent |
| `05430` | Zero-size request returns immediately; nonzero saves size/link and calls `05447` | `zero_size` is separate from allocation failure; wrapper frame is not ported |
| `05447..05454` | Compare size with data `06102 = 021`; try exact-size head `05502 + size` only below cutoff | Ordered bucket for each size `1..020`; newest returned block is reused first |
| `05463..05467` | Start at common head `05502`, skip blocks smaller than request | Ordered common extents, first sufficient block; no best-fit search |
| `05470..05474` | Subtract request from block's upper-half size; result address is block base plus remainder | Split from high end, leave shortened extent in the same list position |
| `05475..05500` | Exact match splices next pointer into predecessor; preserve predecessor's size | Erase exactly the selected extent, retaining all other list order and sizes |
| `05455..05461` | Write data `02213 = 06500000000000000` into every allocated word | Oracle verifies initialization; native metadata allocator does not yet own payload storage |
| `05462` | Return zero block address on failed search | `collection_required`; never silently grow beyond the logical arena |
| `05433..05435` | On failure call `05523`, then retry the same size | **Not implemented**; a failed attempt is not yet a final language out-of-memory diagnostic |
| `05440..05441` | Failed retry prints the required-block message and transfers to `20715` | **Not implemented** |
| `03506..03515` | Zero size returns; size at least `021` prepends extent to common head | Common release is LIFO, without coalescing adjacent blocks |
| `03516..03520` | Replace the selected size-list head and retain its previous head in returned block | Small release is LIFO; logical links represented by ordered blocks |
| `10402..10414` | Sum only common extents; return tagged used and free counts | `core_usage()` returns those two numerical counts, excluding small buckets from free |

`COREUSED` is **not** host live-allocation size. In particular, freeing a
2-word block returns it to its size-specific chain but does not reduce the
reported used count. Those words remain reusable for another 2-word request.
Collecting/coalescing free storage may change this picture; the collector is
outside this unit's boundary.

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

## Remaining allocation work

- Recover collector `05523` and the full free-space reconstruction/coalescing
  paths, including recovery diagnostics and retries.
- Determine native startup's exact arena and all resident/dynamic reservations.
- Attach object payload storage and initialization, with stable object handles,
  aliasing, mutations and cycles.
- Account for dictionary records, compiler operation allocation, generated
  accessors, closure environments, activation frames and retained structures.
- Reproduce recursive thresholds and documented closure failures before claiming
  native exhaustion compatibility.
- Connect this foundation to language execution. The arithmetic probe currently
  uses no compatibility allocator and makes no allocation-conformance claim.

Validation: the focused allocator test, combined 10-test smoke suite and
ASan/UBSan suite all passed; see [native-runtime.md](native-runtime.md) for logs
and the exact continuation verification scope.
