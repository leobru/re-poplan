# Coverage expansion: POPMESS and reader recovery

Run `make coverage-expansion` to rebuild the image/listing and profile the
baseline plus the probes in `tests/inputs/coverage-expansion.json`. Results are
written to [the saved report](reports/coverage-expansion.md), outside the
cleanable build tree. `--report PATH` overrides that destination. Raw artifacts
remain under `build/coverage-expansion/` (`--output` overrides this directory).
Each case has its own input,
native/raw/historical output, normalized output, trace, historical coverage map,
and JSON exit status. Historical runs use the repository wrapper with separate
writable homes. No disk-output consumer is called.

The baseline comprises quine, all recovered `zone*.pop2` files, language and
standard-function fixtures, and a fixed-seed game with two moves. Fresh raw C++
halfword coverage supplies the baseline delta; hybrid CPU/routine traces supply
the fallback ranking. Historical maps are saved separately, not conflated with
C++ instruction coverage.

`python3 tools/coverage-expansion.py --regression --historical` runs only the
supported probes, without expensive tracing. It prints its summary to stdout
unless `--report PATH` is specified, preserving the saved full profile.
CTest runs those probes against
native/raw C++; the smoke suite adds historical comparison. Full profiling has
a 180-second timeout per process because instruction tracing is substantially
slower than ordinary execution. Timeout, process failure, or unexpected output
differences fail the command. Known diagnostic differences are explicitly
reported experiments, not passing regression cases.

After a completed run, `python3 tools/coverage-expansion.py --historical
--report-only` regenerates the report from saved artifacts and verifies the
saved probe inputs. Use it only with the same image/listing and executable
revision as the saved run; it does not execute or revalidate the binary.

## Established behavior

- `[TI]` constructs a function. `[PTO]` is rejected with error `12046` in this
  image despite appearing in the manual's device table.
- MI/MO construction accepts a word in the address position. MI also accepts
  a word offset and the tested excess argument. The probes preserve these
  historical quirks rather than assuming strict numeric/arity validation.
- MI probes specify logical unit 57 (the existing POPLIB fixture) and only
  construct functions; they do not establish coverage of disk reads.
- Missing MI/MO arguments, empty/unknown/non-word commands, extra TO arguments,
  and invalid CLOSE arguments report `12046`. CLOSE of the returned TO or CI
  function is also rejected. A non-list argument reports `12020`.
- These twelve diagnostic experiments recover and execute `2+2=>`. Native C++
  agrees with historical POPLAN, but raw C++ omits the descriptive diagnostic
  line. The runner checks that exact difference; it does not strip it during
  normalization or accept arbitrary mismatches.
- Invalid NUMBERREAD input reports `05010` and recovers identically in all
  three modes. End-of-input during CARRYON, NUMBERREAD, or an unfinished
  constructor produces clean input suspension, not a missing-END diagnostic.

## Interpreting the report

The CPU trace now includes `word=`, the word fetched at the observed PC. A
matching left-half `ROUTINE` record removes that step from fallback counts.
The report distinguishes new halfword visits from fallback instruction steps,
and ranks contiguous image-matching, listing-decoded candidate regions.
Per-halfword rows retain triggering probes and observed raw successors.

Any address observed with a different word, an address outside the image, and
the known generated range `32535..32566` is excluded from static candidates.
Unclassified listing boundaries are reported separately. Matching sampled
words is **not proof of immutability**: stores between observations, including
same-value writes, are not tracked. Inspect the listing and runtime writes
before selecting a conversion boundary. Zero-filled image storage is not
automatically code, and the corpus report's `below 36000` column is only an
address band.

Both `12324` and `12404` are reached by the constructor probes. The generated
report is the authority for fresh counts; earlier port measurements remain
historical. No new native routines or device emulation are introduced here.
JUMPOUT/E71 operation 100, disk-output transfers, and external DIMON files remain
outside this batch.

## Measurement: 2026-09-17

All seven baseline workloads and twelve supported probes agreed across native,
raw, and historical execution. The twelve diagnostic experiments had exactly
the descriptive-line difference recorded above; native and historical output
agreed. The completed sweep added 529 observed halves and 267 word addresses
relative to that seven-workload baseline. Of those halves, 407 were
image-matching and listing-decoded. These are new visits, **not** 407 untranslated
instructions; many already have semantic implementations.

The aggregate image-matching hybrid fallback ranking across the 24 probes was:

| Region | Steps | Priority |
|---|---:|---|
| `12353..12376` | 1088 | POPMESS template/parameter loop; first conversion candidate |
| `12324..12336` | 210 | Argument-bearing constructor setup |
| `12404..12416` | 154 | Constructor finalization, including generated instruction storage |
| `12426..12432` | 80 | Shared constructor helper |
| `03041..03042` | 52 | Diagnostic-path continuation |
| `07773..07777` | 10 | Existing guarded PRSTRI fallback; do not remove its guard |

Use `mi-address` as a small successful constructor trace and the CLOSE/error
probes for additional branches. Preserve the original calls, generated-word
storage, and computed continuations when planning that conversion. Fixing the
raw diagnostic-text discrepancy is a separate correctness task, not part of
these coverage changes.

## Measurement: 2026-10-01 after POPMESS semantic transfers

A fresh `make coverage-expansion` completed all seven baselines and 24 probes.
The known diagnostic-description disagreements remain explicitly classified.
The prior constructor and diagnostic-restoration fallback regions above are
now semantic. The only image-matching fallback candidate in this probe batch
is the guarded `PRSTRI` path `07773..07777`: 10 steps in
`numberread-recovery`. Its callback guard remains necessary.

This batch still excludes disk-output transfers. Separate success/error
transfer fixtures now compare hybrid, instruction-only and historical output
and every written disk byte, with zero hybrid fallback. Their scope, counts,
unvisited entries and commands are recorded in [reports/popmess.md](reports/popmess.md).
