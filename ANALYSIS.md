# POPLAN Disassembly Status

All addresses in this document are octal BESM-6 word addresses.

## Static Image

The reproducible static image is extracted with:

```sh
besmtool dump 2148 --start=01201 --length=017 --to-file=build/poplan.bin
```

The result contains `017` octal zones, or 15 zones. Each zone contains `02000`
octal 48-bit words, so the image occupies addresses `00000..35777`.

For a disk location `zone.offset`, its memory address in the extracted image
is:

```text
(zone - 01201) * 02000 + offset
```

Examples:

| Disk location | Memory address | Meaning |
|---|---:|---|
| `1201.1000` | `01000` | Runtime entry |
| `1202.0750` | `02750` | POP value dispatch |
| `1203.1230` | `05230` | Cold start |
| `1211.0170` | `20170` | Primary input |
| `1213.1370` | `25370` | Token source |

For the volume present during this analysis, the extracted image has SHA-256
`98307635094cc68ed6e4a6e733de3fd5c32d363dfeb2b6ee179ed131f5d88ef9`.
The extraction script validates the byte count and prints the current hash;
the hash is provenance, not a substitute for regenerating the image.

## Bootstrap

The bootstrap path is outside the extracted `00000..35777` image:

1. `75777` saves the initial accumulator and executes `*70 76000`.
2. The control word `0010370000421200` reads zone `01200` from disk handle
   `42` into `76000`.
3. `76000` establishes its local base in `r10`.
4. `76011:*70 26(10)` performs the first main-image read.
5. `76014..76017` advances both the memory page and disk zone in the control
   word and repeats the read.
6. The observed startup reads through zone `01216`.
7. Control transfers to `01000`, which calls `05230`.

This sequence is directly visible at the beginning of any `-t -t` trace.

## Runtime Landmarks

The current labels are maintained in `poplan.sym`.

| Address | Label | Established behavior |
|---:|---|---|
| `01000` | `ENTRY` | Transfers into cold start and then the evaluator |
| `02750` | `EVAL_DISPATCH` | Dispatches tagged POP values |
| `03014` | `ERROR_DISPATCH` | Packages diagnostic context as POP arguments |
| `03051` | `ERROR_UNPACK` | Restores diagnostic code and source object |
| `03057` | `ERROR_FORMAT` | Saves context and drives diagnostic formatting |
| `03275` | `PUSH_ACC` | Pushes the accumulator on the POP data stack |
| `03277` | `POP_ACC` | Pops the POP data stack into the accumulator |
| `03303` | `STORE_STACK_TOP` | Stores the top POP item through `r16` |
| `03413` | `NUMERIC_UPDATE` | Numeric helper used by generated quine code |
| `05230` | `COLD_START` | Initializes runtime state and enters POPLAN |
| `07475` | `CUCHIN_ARG` | Consumes and normalizes a tagged character argument |
| `16313` | `CHAR_SEQUENCE` | Iterates a packed character sequence through `21431` |
| `20170` | `INPUT_PRIMARY` | Reads or exposes the current console line |
| `20263` | `IO_INIT` | Initializes console and message state |
| `20674` | `MESSAGE_OUTPUT` | Common interactive message output |
| `21255` | `CHAR_INPUT` | Saves a character parameter and enters conversion |
| `21264` | `CHAR_CONVERT` | Converts through one of the tables selected at `21274` or `21275` |
| `21274` | `CHAR_DECODE` | Selects the input-side conversion table at `21301` |
| `21275` | `CHAR_ENCODE` | Selects the output-side conversion table at `21354` |
| `21431` | `BUFFER_CHAR` | Advances a packed-text descriptor and returns a character |
| `25370` | `TOKEN_SOURCE` | Refills and initializes the current input descriptor |
| `25641` | `FORMAT_NUMBER` | Startup number/time formatting driver |

Detailed evidence is in `subroutines-from-trace.md`.

## Dynamic Coverage

The quine trace contains:

- 136,165 instruction lines;
- 1,600 unique word addresses overall;
- 1,555 unique static-image addresses;
- 10.1% word-address coverage of the 15,360-word static image;
- 98 direct `vjm` targets.

Only 20 direct targets currently have useful semantic descriptions. Leaf
status in generated call inventories means that no nested `vjm` was observed
while an invocation was active. It is dynamic evidence, not a static proof.

The most frequently called unnamed targets are:

| Address | Calls | Observed kind |
|---:|---:|---|
| `11541` | 279 | leaf |
| `11673` | 279 | nonleaf, linked through `r14` |
| `16457` | 195 | nonleaf |
| `25346` | 191 | nonleaf |
| `11717` | 186 | nonleaf |
| `16421` | 121 | leaf |
| `16505` | 96 | nonleaf |
| `05430` | 97 | nonleaf |
| `05447` | 97 | leaf |

## Next Reverse-Engineering Targets

1. Identify `11541`, `11673`, and `11717`, which dominate execution of the
   generated quine loop.
2. Continue the converted-character path through `25346` and its `21443`
   buffering dependency.
3. Separate executable code, tagged POP objects, jump tables, strings, and
   numeric constants in the static listing.
4. Extend the focused POP-2 probes beyond the current arithmetic, list,
   string, and branch coverage to identifiers, function compilation, and
   diagnostics.
5. Promote confirmed routine names into `poplan.sym`; keep hypotheses in the
   semantic notes until a trace or static path establishes them.
