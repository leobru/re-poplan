# Generated Artifacts

Binary images, traces, listings, coverage maps, and emulator output are
generated under `build/` and excluded from Git.

## Static Image

Source:

```sh
besmtool dump 2148 --start=01201 --length=017 --to-file=build/poplan.bin
```

Regenerate with:

```sh
make image
```

Expected size: 92,160 bytes.

## Interactive Trace

Source:

```sh
tools/run-dispak.sh --bootstrap -t -t --coverage=build/quine.cov \
    poplan.b6 < quine.pop2 > build/quine.out 2> build/trace.quine
```

Regenerate with:

```sh
make trace
```

The trace is intentionally not committed. It is large, deterministic apart
from session output, and can be reproduced from the disk image and input.

## Listing, Call, And Dictionary Inventories

`make listing` invokes `disbesm6` with `poplan.sym` and the quine trace. Trace
addresses seed code discovery, while explicit symbols retain names for
confirmed routines not reached by that input.

`make calls` parses direct `vjm` instructions from the trace. It does not infer
indirect-call destinations. Its leaf/nonleaf column reports only whether
nested `vjm` execution was observed during traced invocations.

`make dictionary` combines decoded names from the listing with raw words from
the extracted image. Using the image is required because a tagged data word
can be a valid BESM-6 instruction encoding and may be rendered as code in the
listing.

## Historical Source Coverage

`make coverage-corpus` runs `quine.pop2` and the recovered `zone*.pop2`
sources separately, writing their output and coverage maps under `build/`.
`tools/summarize-coverage.py` produces `build/coverage-corpus.md` with
per-input word and halfword counts, exclusive contributions, and union
coverage.

The zone sources remain the inputs of record. `zone1222.pop2` and
`zone1223.pop2` contain only 7-bit bytes. In `zone1224.pop2`, lowercase Latin
KOI-7 characters and the `` `{|}~`` positions were mechanically converted to
UTF-8 Cyrillic with `tools/convert-koi7-lower.py`; uppercase POP-2 syntax and
other punctuation were left unchanged. POPLAN accepts the converted UTF-8
source with the same runtime behavior.

## Reproducibility Boundary

Volume `2148` is an external input and is not copied into this repository.
Record all semantic claims against:

- the extracted-image SHA-256;
- the `dispak`, `besmtool`, and `disbesm6` versions;
- the exact POP-2 input;
- the command that produced the trace or listing.

Volatile greeting and exit times are removed only when comparing program
output. They remain intact in raw traces and output files.
