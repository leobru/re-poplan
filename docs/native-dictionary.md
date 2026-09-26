# Native packed-name interning

`NativeDictionary` adds the identity/allocation part of `01107..01166` to the
image-independent foundation. It accepts the reader's **already packed** name;
it does not implement source tokenization, case conversion or UTF-8 parsing.
Resident primitive bindings remain unbound on the native side.

## Source decisions retained

| Evidence | Native behavior |
| --- | --- |
| `01107..01121`, mask at `01173` | Form successive masked shifts of the six-byte name and mask the sum with `0377` |
| `01122..01136`, table `01200..01377` | Select one of 256 collision chains; even buckets correspond to the original word's upper half, odd buckets to its lower half |
| `01141..01147` | Compare complete packed names and follow the low-address link in property word `+2`, preserving order |
| Resident descriptor properties | Reconstruct original bucket heads from the linked records; do not sort names or descriptor addresses |
| `01137`, `01150` | Allocate exactly four words only on a miss; report collection-required when the allocator cannot satisfy the request |
| `01140` | Install a new head in an empty bucket |
| `01151` | Append to the last record of an existing collision chain; retain the other property bits |
| `01154..01157` | New record: packed name, unbound class/value fields, property word `03000000000000000` |
| `01160..01166` | Return the same stable logical word identity for repeated lookups |

The hash is not a simple weighted sum of characters: after each eight-bit left
shift, the right shift decreases from 40 to 35, so later samples include high
bits from the next byte. That detail affects collision placement and lookup
order. Native code retains the arithmetic on the packed name; it neither reads
BESM scratch words nor recognizes executable instructions.

The public API exposes word records read-only. Names and collision links cannot
be independently changed through it. Word identities are numeric handles,
independent of host vector storage. Dynamic arena overlap with resident records
is rejected as an implementation configuration error. Native binding, alias,
property-update and mutable-syntax operations are still unimplemented.

## Verification boundary

`poplan-native-dictionary` uses `tests/cpp/machine_test.cpp --native-dictionary`
with the normal image-path argument before the option. It compares against
instruction-only execution of `01107`, with the established bootstrap descriptor
constants and an explicit logical arena.

Checks cover all 184 resident names; all 256 bucket heads; every word's name,
class and property/link fields; dynamic collision hits and misses; empty and
non-ASCII packed inputs at this low-level API; repeated lookup without allocation;
stable identity through host vector reallocations; and exact four-word accounting.
At arena exhaustion, the raw oracle stops on the collection boundary `05523` and
the native interner returns `collection_required` without inserting a word.
This does not claim language-level EOF/error recovery or collector conformance.

Full six-character source-name collision behavior still requires porting the
reader; this API operates on exactly six packed bytes already. It cannot itself
establish where the reader ends a token, converts characters or discards suffixes.

Validation: the focused dictionary test, combined 10-test smoke suite and
ASan/UBSan suite all passed; see [native-runtime.md](native-runtime.md) for logs
and the exact continuation verification scope.
