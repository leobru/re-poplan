# POPLAN data-stack cells (register 6)

This describes the representation established by the extracted image and the
current semantic port. It is not a proposed host-language object model.
Addresses, masks, tags, and full-word examples below are **octal** unless
explicitly marked otherwise. Bit numbers and shift counts are **decimal**,
numbered from zero at the least-significant bit. A full BESM word has 48 bits
and is printed as 16 octal digits.

## Stack organization

The POP value stack is held in ordinary BESM memory. Register `006` (`r6`)
addresses its current top cell. Every cell is one complete 48-bit value:
there is no separate type byte, host pointer, or per-cell length field.

```text
lower addresses
    M[r6]       top / next value to pop
    M[r6 + 1]   preceding value
    M[r6 + 2]   preceding value
higher addresses
```

Push decrements r6 and then stores the value; pop reads the value and increments
r6. Address arithmetic wraps modulo 32768 (decimal), with mask `77777`.
Popping does not clear the cell. The reset paths `05261` and `13370` obtain the
empty-stack pointer from `M[17011]`; that image word contains `70000`.
`03305` derives stack depth from this base and r6 using the original BESM
arithmetic. The base is not an extra field in each value.

The implementation's `value_stack_*` helpers preserve this memory-backed
representation. The address-named operations are:

| Routine | Operation |
|---|---|
| `03275` | Push ACC; ACC and ALU mode are unchanged. |
| `03277` | Pop into ACC and select the logical ALU group. |
| `03301` | Load `M[r16]` into ACC and push it. |
| `03303` | Consume the top value and store it through r16, restoring incoming ACC through the hardware stack. |

Register `017` is the separate BESM hardware stack pointer. It holds saved
registers, links, and arithmetic intermediates; those cells are not necessarily
POPLAN values. Hardware-stack spills may alias value-stack cells, so neither
stack can be treated as storage invisible to other memory operations.

## A cell is a value, not necessarily a pointer

There are two principal interpretations of a word:

- Numbers are represented directly in the word.
- Tagged objects use high bits to identify a class and lower fields to carry
  addresses or other type-specific information.

Many class tests use mask `7740000000000000`, selecting bits 47..41. In source
and listings the resulting class is conveniently written as a three-octal-digit
prefix such as `640`, `644`, or `720`. These prefixes are **masked values**:
the bottom two bits of the third octal digit belong to the payload. For example,
`6437777777777775` still has class `640` under this mask.

This is not a universal six-bit tag plus a 42-bit address. Function recognition
uses the broader mask `7700000000000000`, and real-number recognition performs
arithmetic rather than a single equality test. Use the operation's actual mask.

The following classes are directly established by the current code/image:

| Masked prefix | Meaning | Payload / referenced storage | Evidence |
|---|---|---|---|
| `640` | Integer | In-word signed numeric payload; also used for character codes and Boolean results. | `03403`, constants `03453`/`03454`, PRREAL integer formatting |
| `644` | Word (interned identifier) | Low 15 bits address the word's dictionary record. Includes distinguished word values such as NIL. | `06611`, constants `06662`/`06666`, dictionary layout |
| `660` | Ordinary function | Environment and entry fields described below. | `02750`, `FunctionDescriptor::decode` |
| `664` | Special function | Same extracted fields, with the special-path flag set; evaluated through `15765`. | Function special mask |
| `710` | Packed string | Low 15 bits point to a header followed by packed characters. | `07773`, constants `10033`/`10047` |
| `714` | Pair | Low 15 bits point to two consecutive complete value words. | `CONSPA` → `07125` → `05213`, tag constant `05226` |
| `720` | Nonempty list/link representation | Low 15 bits point to the two-word object constructed by CONS. | `03330`, `10353`, `CONS` → `05215`, tag constant `05227` |
| `724` | Reference | Low 15 bits point to one allocated cell containing the referenced value. | `CONSRE` → `10277`/`10300`, tag constant `10256` |

This table is not an exhaustive assignment of every high-bit pattern. Internal
dictionary markers and generated descriptors also exist. In particular, the
dictionary class word `6500000000000000` is not itself the user-level `644`
word handle. Do not assign an additional array, record, or strip layout solely
from an apparent prefix: those facilities involve constructors, generated
accessor functions, and descriptor-specific fields.

## Integers, characters, Booleans, and real numbers

The integer tag value is `6400000000000000`. Small nonnegative integers are
formed by combining this value with the integer payload:

| Value | Cell contents |
|---|---|
| Integer 0 / Boolean false | `6400000000000000` |
| Integer 1 / Boolean true | `6400000000000001` |
| Integer 4 | `6400000000000004` |
| Integer -3 | `6437777777777775` |

Negative integer payloads are two's-complement, not sign-magnitude. The
PRREAL integer formatter extracts the low 40 bits and sign-extends bit 39;
ordinary negative examples also carry that sign in bit 40. Preserve the entire
word rather than reconstructing it from its low 15 address bits. The table
above describes established encodings, not a promise that arbitrary payload
bit patterns satisfy all integer operations' range checks.

Characters on the value stack are integer values, not a distinct character
tag. For example, a character code is pushed as the integer tag combined with
its code; interpretation/conversion of that code belongs to the character
routine, not to the stack-cell format.

Real numbers use the BESM floating representation, not IEEE binary64:

```text
bits 47..41: E, an unsigned 7-bit biased exponent
bits 40..0:  M, a signed 41-bit two's-complement mantissa
value = M * 2**(E - 104)             # all numbers in this formula decimal
```

Equivalently, the exponent bias is 64 and the mantissa's binary point is at
40 fractional bits. This is the interpretation implemented by
`MantissaExponent` and PRREAL; normalization, rounding, and arithmetic mode
still matter to execution. Examples from the formatting tests are real zero
`0000000000000000`, real -1 `4020000000000000`, and real 2.5
`4112000000000000`.

Integer zero and real zero are therefore different words. `ISREAL` at `06605`
uses the original shift/cyclic-add/sign test; do not implement it as “anything
not in the tag table.”

## Pointer-bearing values and NIL

`Word48::address()` extracts `word & 77777`, i.e. bits 14..0. For the pointer
classes above, this is a BESM **word address**, not a byte offset or host address.
The rest of the stack word must remain intact when copying a value.

The word handle points at a dictionary record whose established four words are:

1. Packed name.
2. Class word (normally `6500000000000000` in the initial dictionary).
3. Property word.
4. Associated value, often a function object.

For example, the dictionary record for NIL begins at `02044`, and its value is
`6440000000002044`. This exact word is the empty list. It is not a null pointer,
raw zero, or integer zero. The list recognizer `03330` accepts either a `720`
class value **or this exact NIL value**. List operations additionally retain
their original validation/evaluation paths (`21464`, `17013`, `17021`, `17045`);
recognizing a prefix alone is not a replacement for them.

For a CONS-created list or a pair at address `a`, `M[a]` and `M[a+1]` hold its
two full values. `05207` and `05211` read those respective fields. A list tail
is itself a value, normally another list value or NIL, not an untagged host link.
A reference at `a` instead holds its referenced value in `M[a]`.

For a string at `a`, the printed-character count is extracted as
`(M[a] >> 24) & 77777`. Its body starts at `a+1`; each body word contains six
8-bit characters, most-significant byte first. PRSTRI uses this explicit count,
not a terminating zero byte. These packed body/header words are heap storage,
not additional r6 stack cells, and should not themselves be classified as
standalone values merely because some high bits resemble a tag.

## Function cells

The established function decoder uses these fields:

| Bits (decimal) | Meaning / extraction |
|---|---|
| 47..42 | Function family: `(word & 7700000000000000) == 6600000000000000`. |
| 41 | Special-function flag: mask `0040000000000000`. |
| 38..24 | Environment address: `(word >> 24) & 77777`. |
| 14..0 | Entry address: `word & 77777`. |

Bits 40..39 and 23..15 are not extracted by this decoder; that does not license
clearing them or assigning them a universal meaning. Special descriptors need
their own evaluation path, not just a direct call to the low address.

The observed cell `6606563700065620` is an ordinary function with environment
`65637` and entry `65620`. It occupies **one** value-stack cell: its environment
and code are not two separately pushed arguments. A direct resident function
can have environment zero; generated functions can point into runtime-created
code instead of the immutable image.

## Evidence and usage constraints

- [Word48](include/poplan/word48.hpp): 48-bit truncation and address extraction.
- [Machine declarations](include/poplan/machine.hpp): function masks/fields.
- [Semantic implementation](src/machine.cpp): `value_stack_*`, `03275`/`03277`,
  classifiers and constructors named above, `MantissaExponent`, PRREAL/PRSTRI.
- [Initial dictionary](src/poplan.cpp): NIL and constructor bindings.
- [Differential tests](tests/cpp/machine_test.cpp): numeric examples, stack
  wrapping, aliasing, and complete architectural-state comparisons.
- [Original structure](docs/original-structure.md): function and dictionary
  evidence. `build/poplan.lst` and `build/poplan.bin` provide the original
  instructions and constants; regenerate them with `make image listing`.

The stack operations themselves do not type-check cells. Raw/generated code
and intermediate semantic paths can move arbitrary words through the stack.
Consequently, inspect the consuming routine before interpreting an unfamiliar
cell, and never discard bits just because a particular consumer reads only an
address. Arithmetic shadow accumulators and native stack helpers do not change
any of these representations.
