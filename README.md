# re-poplan

Tools and notes for disassembling the BESM-6 POPLAN 2.1 (POP-2)
interpreter by Andrei Borisovich Khodulev.

## Requirements

- `dispak`, `besmtool`, and `disbesm6` from
  [besm6/dispak](https://github.com/besm6/dispak)
- volume `2148` installed where the BESM-6 tools can find it
- Python 3
- `expect` only for the legacy `poplan.expect` driver

The BESM-6 instruction notation used in the listings is documented in the
[instruction-set reference](https://raw.githubusercontent.com/besm6/c-compiler/refs/heads/main/docs/Besm6_Instruction_Set.md).

## Quick Start

Run the regression tests:

```sh
make test
```

Reproduce the binary image, a quine trace, an annotated listing, and the
dynamic call inventory:

```sh
make
```

Generated files are written under `build/`:

| File | Contents |
|---|---|
| `poplan.bin` | Zones `01201` through `01217` from volume `2148` |
| `trace.quine` | Instruction trace for `quine.pop2` |
| `quine.cov` | Emulator coverage map for the quine |
| `poplan.lst` | Trace-guided `disbesm6` listing using `poplan.sym` |
| `calls.md` | Direct-call counts, callers, link registers, and observed nesting |
| `dictionary.md` | Static dictionary words, tags, environments, and entry points |

Individual targets are available as `make image`, `make trace`,
`make listing`, `make calls`, and `make dictionary`.

Run the historical `zone*.pop2` sources and combine their coverage with the
quine map:

```sh
make coverage-corpus
```

The report is written to `build/coverage-corpus.md`. The zone sources are kept
as conformance inputs. Lowercase Latin KOI-7 characters and the `` `{|}~``
positions in `zone1224.pop2` have been converted to UTF-8 Cyrillic with
`tools/convert-koi7-lower.py`; uppercase POP-2 syntax and other punctuation
remain unchanged.

Build and test the address-preserving C++ evaluator/activation translation:

```sh
make cpp-test
```

Run the extracted POPLAN image on the C++ execution layer:

```sh
make cpp-run
```

No-argument `poplan` locates `poplan.bin` next to the C++ build directory or
at `build/poplan.bin`, boots the historical interpreter, and evaluates POP-2
from standard input. For example:

```sh
echo '2+2=>' | build/cpp/poplan
```

prints `** 4`. The focused Э71 loopback demonstration remains available as
`build/cpp/poplan --io-demo`.

An explicit image path is also supported:

```sh
make image cpp
build/cpp/poplan --image build/poplan.bin
```

This mode boots the historical interpreter, prints its `ПОПЛАН 2.1` greeting,
prompts with `:`, reads POP-2 source from standard input, compiles it into
generated BESM instructions through Э75, and evaluates it. A reproducible
quine check is available as:

```sh
make cpp-quine
```

The executable's GOST-10859 output conversion preserves the historical mixed
Cyrillic/Latin glyphs used by the existing regression fixtures. Input is
decoded as UTF-8; uppercase and lowercase Russian letters are mapped to the
uppercase-only GOST alphabet, including its noncontiguous `Ъ` code.

Image execution is hybrid: whenever the PC reaches the left-half entry of an
implemented `pXXXXX` routine, that semantic C++ routine runs as one machine
step and returns its continuation address. Generated routines starting at
`036000` or above are decoded from memory and execute their observed straight-
line `stx`, `xts`, `utc`, and `vtm` operations as one semantic unit, returning
at the first `uj` or `vjm`. An unfamiliar generated opcode retains ordinary
instruction fallback. `POPLAN_ROUTINE_TRACE=1` prints each such dispatch;
`POPLAN_INTERPRET_ONLY=1` disables semantic dispatch for differential trace
comparisons. `POPLAN_DISABLE_TRANSLATED_ROUTINES=03235,03261` disables selected
octal entries when isolating a semantic mismatch.

All immutable-image code reached by the quine now has semantic dispatch,
including its 98 direct `vjm` targets, computed continuations, short
trampolines, and blocking `20205` console-input operation. Generated quine code
occurs at `32535..32566` and `65556..65765`; the high block is handled by
generated-routine semantic dispatch, and the low family now has explicit
semantic entries. A fresh quine trace makes 11,437 semantic dispatches and has
zero raw instruction steps.

All code reached by `zone1224.pop2` now has semantic dispatch, including the
low generated family at `32535..32566`. A fresh trace makes 265,316 semantic
dispatches and has zero raw instruction steps.

All code reached while loading `zone1222.pop2` likewise has semantic dispatch.
The former `03474` and `17122..17127` instruction path is represented by four
call-preserving semantic entries; a fresh trace makes 66,970 semantic
dispatches and zero raw instruction steps.

The complete fixed-seed `ttt.pop2` session through moves `0 0 0` and `1 1 1`
likewise has semantic dispatch for every executed instruction path. The
scripted session makes 789,885 semantic dispatches and has zero raw steps.

Extracode `050` implements the BESM-6 floating-point square root (`000`), sine
(`001`), cosine (`002`), arctangent (`003`), arcsine (`004`), natural logarithm
(`005`), and exponential (`006`) operations and clears the remainder register.
Extracode `053` with address `010` returns local time since midnight in
1/50-second jiffies, including the current 20 ms fraction.
Dictionary primitive `POPDAT` dispatches semantically at `15667` and `15672`.
It uses the host local calendar to fill the original eight-character POP
string as `DD.MM.YY` and returns the same current-time jiffy value as its
second result.
Extracode `063` with address `004` returns elapsed image-execution time in the
same 1/50-second units.
Extracode `064` formatted output is accepted as a no-op; POPLAN's emulated
console path uses `Э71` for observable terminal I/O.
Extracode `074` with address `000` terminates image execution normally.

The historical UTF-8 Cyrillic example corpus can be checked in both hybrid
and instruction-only modes with:

```sh
make cpp-zone1224
```

## Running POPLAN

Interactive session:

```sh
tools/run-dispak.sh --bootstrap poplan.b6
```

Run a source file from standard input:

```sh
tools/run-dispak.sh --bootstrap poplan.b6 < file.pop2
```

Generate a trace:

```sh
tools/run-dispak.sh --bootstrap -t -t poplan.b6 < file.pop2 > output 2> trace
```

The wrapper gives `dispak` a private HOME under `/tmp` and links volume
`2148` from the real `$HOME/.besm6`. This keeps emulator state out of the
user's HOME and makes repeated sandboxed runs noninteractive. Override the
binary, source disk directory, or state directory with `DISPAK`,
`BESM6_DISK_DIR`, or `POPLAN_DISPAK_HOME`.

POPLAN has no terminal EOF convention of its own. Type `^` to leave an
interactive session. A redirected `dispak` process also terminates when its
host input reaches EOF.

The messages and diagnostics are in Russian UTF-8. POPLAN output mixes Latin
and Cyrillic glyphs according to the BESM-6 character encoding, so apparently
identical letters must not be normalized in regression fixtures.

## Image Extraction

`tools/extract-image.sh` performs the canonical extraction:

```sh
besmtool dump 2148 --start=01201 --length=017 --to-file=build/poplan.bin
```

The extracted image is 92,160 bytes, or 15,360 BESM-6 words. See
[ANALYSIS.md](ANALYSIS.md) for the disk-to-memory mapping and known entry
points.

## Inputs

- `quine.pop2` is the source-level quine used by the smoke test and trace.
- `tests/expected/quine.out` is its current normalized output.
- `quine2.pop2` records an older character-rendering variant.
- `quine3.pop2` is a complete historical transcript with volatile timestamps.
- `ttt.pop2` is a 4x4x4 tic-tac-toe program used as a larger compiler and
  startup probe.
- `tests/inputs/primitives.pop2` isolates arithmetic, string output, list
  access, and conditional execution.
- `zone1222.pop2`, `zone1223.pop2`, and `zone1224.pop2` are historical library
  and example sources used as compiler/runtime coverage inputs.
- `poplan.expect` is a legacy interactive driver for the copy of the game
  stored on the historical disk.

## Loader Task

`poplan.b6` is a Dubna batch-language loader:

```text
USER 419900^
DISK 42(2148)^
TELE^
BEG 75777^
E
B 75777
K 00 170 6000
C 0010 3700 0042 1200
E
FINISH
```

The instruction at `75777` invokes extracode `070` using the control word at
`76000`, reading zone `01200` from handle `42`. The zone contains the POPLAN
loader, which reads the main image and transfers to `01000`.

## Repository Map

- [ANALYSIS.md](ANALYSIS.md): memory layout, bootstrap flow, known semantics,
  and prioritized reverse-engineering work.
- [subroutines-from-trace.md](subroutines-from-trace.md): semantic routine
  catalog grounded in interactive traces.
- [docs/original-structure.md](docs/original-structure.md): tagged objects,
  compiler coverage regions, and the Man-or-Boy storage diagnosis.
- [docs/diagnostic-path.md](docs/diagnostic-path.md): traced syntax-error
  reporting and its address-preserving C++ translation.
- [docs/cpp-port-plan.md](docs/cpp-port-plan.md): address-preserving C++ port
  order, conformance gates, and baseline/fixed closure semantics.
- [poplan.sym](poplan.sym): labels and code entries consumed by `disbesm6`.
- [docs/artifacts.md](docs/artifacts.md): generated-artifact policy and
  provenance.
- [tools](tools): extraction, tracing, disassembly, normalization, and trace
  analysis.
- [tests](tests): smoke tests and tool unit tests.
