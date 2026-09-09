# POPLIB Compatibility Source Library

The surviving POPLAN image contains a loader for a missing executable overlay
on logical disk `057`. Its raw zone-zero and zone-six checks are partially
known, but they do not establish the historical DIMON catalog layout. The C++
evaluator therefore recognizes a small, explicit source-library directory.
It is deliberately marked as emulator-native and is not claimed to reproduce
the historical filesystem.

`poplib.bin` is kept in flat form: every unsigned 48-bit BESM word is six bytes,
big endian. A logical zone contains `02000` words, or 6144 bytes. The C++
evaluator reads this file directly. Before starting dispak,
`tools/run-dispak.sh` imports it into physical volume `2157` with:

```sh
besmtool write 2157 --start=0 --from-file=poplib.bin
```

| Zone-zero word | Meaning |
| ---: | --- |
| `0` | POPLAN header `1303100000000000` with directory word count in its low 11 bits |
| `1` | six bytes `RPN57\0` |
| `2` | format version, currently `2` |
| `3` | record count |
| `4...` | consecutive four-word records |

Each record contains the six-byte GOST user identifier, the six-byte GOST file
identifier, the starting zone, and the exact source byte count. Identifiers
shorter than six letters are padded with GOST space (`017`); longer source
identifiers are truncated to the same first six characters that POPLAN passes
to `LIBRARY`. Source is encoded into the character values returned by POPLAN's
input decoder; in particular, line end is `012`. Payloads occupy consecutive
zones, and zero padding after the recorded byte count is ignored.

Zone 0 holds the directory, zones 1 through 5 are unused, and zone 6 holds a
small loader-compatible overlay for raw execution. Source files are packed
from zone 7 upward. The current image is 73728 bytes and contains:

| Requested file | Packed key | Zones | Encoded bytes |
| --- | --- | --- | ---: |
| `FOURS` | `FOURS` | `0007..0010` | 9172 |
| `DEBUG` | `DEBUG` | `0011` | 1168 |
| `MEMOFNS` | `MEMOFN` | `0012` | 2115 |
| `EXAMPL` | `EXAMPL` | `0013` | 1727 |

```sh
tools/make-poplib.py poplib.bin \
    --entry POPLIB FOURS zone1220.bin 9172 \
    --entry POPLIB DEBUG zone1222.pop2 - \
    --entry POPLIB MEMOFNS zone1223.pop2 - \
    --entry POPLIB EXAMPL zone1224.pop2 -
```

Multiple `--entry USER FILE PATH LENGTH` options may be supplied. `LENGTH=-`
stores the complete file.

Display every compatibility catalog found in the flat image with:

```sh
tools/show-poplib-catalog.py poplib.bin
```

Semantic dispatch occurs at `14662`, only after the original POPLAN code has
evaluated the LIBRARY argument and decoded both identifiers. On a match, the
payload is split into lines, converted back to terminal GOST through the
original `21275` table arithmetic, queued ahead of terminal input, and exposed
as the existing `CHARIN`
supplier. The original `14677..14704` epilogue pushes that function result and
restores the saved registers. A non-native or unmatched image continues via
the original `14662` `NTR 3; VJM 14723(16)` path. Under raw dispak execution,
the zone-6 overlay recognizes the same directory records, reads source zones
through extracode `070`, and returns a supplier function to the resident
loader. This is a compatibility format, not a claim about historical DIMON
media layout.
