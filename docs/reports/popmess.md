# POPMESS semantic port status — 2026-10-01

This report covers the hybrid backend. It does not extend `--native` or
claim complete coverage of historical POPMESS devices.

| Scope | Implementation and evidence | Status |
| --- | --- | --- |
| Constructors and CLOSE | Live command table and descriptor fields; `12304..12517`; constructor replay fixture | Implemented, permissive arguments retained |
| MI/MO character transfers | `21647..22024`, buffer families `16030..16101` and `20573..20635` | Round trip, offset, zone crossing, closing character and descriptor reuse verified |
| MIП/MOП conversion | Original `21274`/`21275` conversion calls | Historical output and disk bytes agree |
| TI/CI/TO/LPO console | Existing console routines, shared POPMESS construction | Character input/output and EOF suspension verified |
| Diagnostics | Invalid character, closed input/output, `22216`, restoration continuations | Historical recovery agrees; raw catalog description limitation retained |
| E70 host service | Explicit flat read-only/read-write mounts, complete-zone reads and bounded persistent writes | Host and CLI negative tests, independent backend disk copies |
| Computed CLOSE continuation | `12463..12464` live descriptor modifier | Synthetic full-state differential includes address wrapping |
| DIMON files, other peripherals | No host implementation | Unsupported |
| MIR/MOR record-oriented devices | Listing seeds only; instruction fallback retained | Semantic port incomplete |
| Unvisited device branches | Dispatch entries alone do not establish conformance | Further branch coverage remains |

## Reproducible checks

```sh
make cpp
ctest --test-dir build/cpp -R 'popmess|disk-host' --output-on-failure
python3 tests/popmess-devices.py --historical --output artifacts/popmess/conformance
make coverage-expansion
```

`tests/cpp/machine_test.cpp` replays each reached translated entry against
instruction execution, comparing PC/half, ACC, RMR, ALU mode, every register,
all 32768 memory words, console output and independently cloned disk files.
The two transfer replays reach 97 of their 106 selected entries. They do not
reach `10020`, `10023`, `10025`, `10026`, `10027`, `20633`, `20635`, `21772`,
or `22006`; these remain explicit targets for additional branch probes.
The two transfer fixtures compare all 18432 bytes of their three-zone images
against instruction-only C++ and historical DISPAK. They check persisted reads
in a new process, protected images, missing units, invalid mounts, bounds and
unsupported seeks. Failed complete-zone reads do not partially publish memory.

| Fixture | Hybrid semantic dispatches | Remaining raw instruction steps |
| --- | ---: | ---: |
| `popmess-devices.pop2` | 50462 | 0 |
| `popmess-errors.pop2` | 24564 | 0 |

Counts are from the 2026-10-01 working tree, not historical measurements or
native operation counts. Zero fallback is asserted by the test script. The
error comparison permits only the three established missing raw description
lines; historical and hybrid output must match exactly after normalization.
The existing narrow raw system-catalog request on unmounted `42/01200` remains
a non-transfer exception. Other unmounted device transfers fail explicitly.

The broader coverage report is [coverage-expansion.md](coverage-expansion.md).
Saved reports live outside `build/`; optional run logs and disk snapshots live
under `artifacts/popmess/`. Old profile counts in other documents remain
measurements of their stated revisions and corpora.

The refreshed constructor/reader coverage batch has only 10 image-matching
fallback steps, at guarded `PRSTRI` `07773..07777` in `numberread-recovery`.
All seven baselines and 24 probes completed with their established verdicts.

## Verification results

- Full regular CTest: 16/16 passed, including the final computed-CLOSE test.
- `make test`, `make cpp-quine`, `make cpp-zone1224`: passed.
- Historical trivial result: `** 4`; historical zone1224 and fixed-seed game
  comparisons passed in the coverage sweep.
- Full ASan/UBSan CTest: 16/16 passed with `ASAN_OPTIONS=detect_leaks=0`
  and `UBSAN_OPTIONS=halt_on_error=1`.
- POPMESS historical comparisons: passed, including EOF suppliers, recovery,
  exact disk bytes and zero-fallback assertions.
- `git diff --check`: passed.

Logs are saved in `artifacts/popmess/`; the full sanitizer run took 338.31
seconds. The additional computed-CLOSE test was introduced after that build
and passed the final focused sanitizer rebuild/run: 5/5 tests, 98.52 seconds.
