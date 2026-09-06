This project is to disassemble or decompile the BESM-6 POPLAN (POP-2) interpreter.

The BESM-6 architecture and instruction set opcode notation is described in https://raw.githubusercontent.com/besm6/c-compiler/refs/heads/main/docs/Besm6_Instruction_Set.md

The extracode 070 (\*70) is the disk I/O system call. 071 (\*71) is the console I/O.

To run experiments: `dispak --bootstrap poplan.b6 < input_file > output`

To trace: `dispak --bootstrap -t -t poplan.b6 < input_file > output 2> trace`

# re-POPLAN Working Instructions

This project disassembles and decompiles the BESM-6 POPLAN (POP-2)
interpreter and incrementally replaces instruction-by-instruction execution
with address-preserving semantic C++ routines.

The BESM-6 instruction set and opcode notation are documented at:

<https://raw.githubusercontent.com/besm6/c-compiler/refs/heads/main/docs/Besm6_Instruction_Set.md>

## Non-negotiable constraints

- Keep all implementation in the existing C++ source files. Do not add
  additional `.cpp` files.
- Do not hardcode or recognize complete BESM instruction words in C++.
  Translate the routine's semantics from the listing and trace.
- Preserve established octal routine and continuation addresses. When the PC
  reaches the left-half entry of a translated `pXXXXX` routine, semantic
  dispatch must use it instead of interpreting its individual instructions.
- Keep the instruction interpreter as fallback for untranslated and generated
  code and as the oracle for differential tests. The long-term goal is to make
  fallback unnecessary, not to remove it prematurely.
- Preserve the original BESM control flow, calls, computed continuations,
  register/link conventions, hardware-stack balance, ACC, RMR, ALU mode, and
  memory effects. Do not replace source-faithful arithmetic or traversal with
  an output-only shortcut.
- Calls to independent routines remain visible boundaries. In particular,
  preserve evaluator, allocator, stack, table, character, and I/O calls when a
  translated routine leaves through them.
- Internal labels may be combined or inlined only when they are not semantic
  dispatch entries and have no independent observed entry. Keep an address
  comment at the inlined code. Never erase a reachable continuation merely
  because C++ could express the containing routine as one large function.
- Do not assign stronger semantic names than the dictionary, listing, and
  traces establish.
- Do not feed `.pop2`, output, or other text files to the disassembler. The
  disassembler operates on the extracted BESM image; POP-2 sources are runtime
  input.
- Preserve unrelated user changes in a dirty worktree.

## Canonical evidence and documentation

- `build/poplan.lst` is the generated annotated listing.
- `build/trace.quine` and `build/quine.cov` are the historical quine trace and
  coverage map.
- `build/coverage-corpus.md` summarizes the quine and recovered `zone*.pop2`
  coverage.
- `subroutines-from-trace.md` is the canonical trace-backed routine inventory,
  including known continuations and observed call counts.
- `docs/cpp-port-plan.md` records the current port boundary and architecture.
- `docs/original-structure.md` records recovered structural evidence.
- `poplan.sym` contains address symbols; update it and the status documents
  when a newly understood boundary warrants it.

Regenerate artifacts through the repository targets rather than editing
generated files manually:

```sh
make image
make trace
make listing
make calls
make dictionary
make coverage-corpus
```

## Historical execution and tracing

Use historical POPLAN through `dispak` as the behavioral reference:

```sh
dispak --bootstrap poplan.b6 < input_file > output
dispak --bootstrap -t -t poplan.b6 < input_file > output 2> trace
```

Prefer `tools/run-dispak.sh` for repository experiments because it supplies a
controlled writable home directory. Obtain permission for execution outside
the sandbox when required.

For `poplan.expect`, use `dispak -l`, convert Cyrillic interaction to Latin,
answer yes to playing first, play `0 0 0` and `1 1 1`, then terminate.

Always include the trivial reference checks when validating evaluator work:

```sh
echo '2+2=>' | dispak poplan.b6
echo '2+2=>' | build/cpp/poplan
```

The normalized expected result is inline in the Makefile: `** 4`; there is no
need for `tests/expected/trivial.out`.

## Hybrid C++ execution

Build and run the extracted image with:

```sh
make cpp
build/cpp/poplan --image build/poplan.bin < input.pop2
```

With no `--image`, `build/cpp/poplan` looks for the repository's default
`build/poplan.bin`. `--io-demo` exercises the standalone console adapter.

Useful runtime controls:

- `POPLAN_ROUTINE_TRACE=1` logs semantic routine dispatches.
- `POPLAN_CPU_TRACE=1` logs every machine step.
- `POPLAN_INTERPRET_ONLY=1` disables all semantic dispatch.
- `POPLAN_DISABLE_TRANSLATED_ROUTINES=03235,03261` disables selected octal
  entries to isolate a mismatch.

Semantic dispatch owns documented left-half entries only. BESM transfers
always enter the left instruction of a word; the right instruction is reached
only by sequential execution of the left instruction. A blocking semantic I/O
routine waits and retries at its left-half routine entry.

To find remaining raw code, run with both CPU and routine tracing and subtract
steps followed by a matching `ROUTINE` record. Separate immutable image
addresses from generated heap/code addresses before proposing a static
conversion. Rank candidates by executed fallback instructions, not merely by
source length.

At commit `4ea952c`, the quine profile had 285 fallback steps: 136 in static
image regions and 149 in generated addresses. The then-remaining observed
static regions were `01000..01002`, `03461..03475`, `07761..07772`, `11514`,
`11746..11747`, `16145..16150`, `16513..16516`, `16530`, `16616..16620`,
`16745..16747`, `17120..17121`, `17131`, `20200`, `20205`, `20207`, and
`20564..20570`. Re-profile after every conversion; this baseline is expected
to become stale.

After converting that list, a fresh quine profile has zero immutable-image
fallback steps. Its 95 remaining raw steps are generated POP-2 code at
`32535..32566` and `65556..65765`; do not treat those addresses as static
subroutine candidates.

A later full `zone1224.pop2` sweep converts all traced immutable-image
fallback, including `25730..25753`. Its remaining raw execution is generated
POP-2 code; `32535..32566` is generated despite lying inside the broad address
extent occupied by the loaded image. Classify candidates by image identity and
runtime writes, not by a single numeric cutoff.

## Differential conversion workflow

For each translated entry or tightly related continuation cluster:

1. Establish the instruction-only behavior from the listing and a trace.
2. Implement the named `pXXXXX` routine in `src/machine.cpp` and declare it in
   `include/poplan/machine.hpp`.
3. Add its left-half entry to `Machine::dispatch_translated_routine()` in
   `src/machine_cpu.cpp`.
4. Stop at the next independent routine boundary. Return that octal address
   instead of absorbing the callee.
5. Add focused semantic-versus-interpreter fixtures. Compare PC and half,
   ACC, RMR, ALU mode, all index registers, hardware-stack balance, and the
   complete 32K-word memory.
6. Use `POPLAN_DISABLE_TRANSLATED_ROUTINES` when the raw oracle must interpret
   a newly translated entry while retaining other semantic routines.
7. Update `subroutines-from-trace.md` and `docs/cpp-port-plan.md` with facts
   actually established by the trace.

Do not rely on output equality alone: hidden RMR, ALU, register, stack, or
memory divergence can break a later continuation.

For game comparisons, replace volatile
`INTOF(100*POPTIM())->RANSEED` with `1->RANSEED` in the test input. A
time-derived seed is not a valid semantic/raw comparison baseline.

## Required verification

Run the applicable focused test first, then the complete suite:

```sh
ctest --test-dir build/cpp --output-on-failure
make test
make cpp-quine
make cpp-zone1224
git diff --check
```

`make cpp-zone1224` proves only that hybrid and instruction-only C++ agree. For
historical conformance, also run the advertised `zone1224.pop2` examples under
`dispak` and compare normalized output with `tools/normalize-output.py`.

Run sanitizers after semantic or machine-state changes:

```sh
cmake -S . -B build/cpp-sanitize -DCMAKE_BUILD_TYPE=Debug \
    -DPOPLAN_ENABLE_SANITIZERS=ON
cmake --build build/cpp-sanitize
ASAN_OPTIONS=detect_leaks=0 \
    ctest --test-dir build/cpp-sanitize --output-on-failure
```

LeakSanitizer conflicts with ptrace in this environment; disabling leak
detection does not disable ASan or UBSan. Never report a combined/truncated
test log as passing without checking the command's final exit status.

## BESM machine invariants

- Register zero is architecturally zero when used by effective-address
  computation. Do not repeatedly write `r0 = 0`; enforce the rule in effective
  address calculation.
- Avoid redundant `select_alu_group()` calls only when the preceding semantic
  operation already establishes exactly the same observable ALU group.
- Preserve `NTR`, RMR, normalization, rounding, sign tests, and exponent
  behavior. BESM branch conditions often depend on ALU mode, not just whether
  the accumulator is numerically zero.
- Preserve `r15` and `r14` link conventions and `r17` hardware-stack effects.
  The generated update path at `11673` is a known `r14`-linked exception.
- Keep dynamically generated instruction storage through `Э75`; do not replace
  generated instruction words with host-side instruction-word pattern tests.

## Character and console I/O

- Extracode `070` (`*70`) is disk I/O. Extracode `071` (`*71`) is console I/O.
- `Э71` is emulated semantically, including readiness queries, input-required
  suspension, packed GOST input, output transfer, and UTF-8 conversion.
- Input accepts Unicode Cyrillic and maps uppercase/lowercase Russian letters
  to the uppercase-only GOST-10859 alphabet, including the noncontiguous `Ъ`
  code. Output converts GOST text to UTF-8 while preserving historically mixed
  Cyrillic/Latin glyph behavior used by fixtures.
- `Э64` has no required observable effect here and may safely be ignored.
- `p21264_convert_character()` must retain its BESM table arithmetic: `NTR 3`,
  multiply with RMR, `YTA`, reverse subtraction, modifier addition, table
  selection through `r16`, and byte extraction. Do not replace it with a C++
  character lookup table.
- `21274` selects table base `21301`; `21275` selects `21354`.
  `21260` transfers to `25346` with link `21261`; `21261` restores the saved
  link from the `r17` hardware stack.
- Trace-validated conversions include `052 -> 031`, `012 -> 377`,
  `040 -> 017`, `060 -> 000`, `031 -> 052`, and `001 -> 061`.
- `PRSTRI` has guarded semantic dispatch at `07773`: use native packed-string
  traversal only while `01567` still names ordinary `CUCHIN_ARG`. If the
  callback is rebound, fall back to the original instruction path.
- `PRREAL` dispatches at `12674` and forms its textual representation natively,
  while preserving the packed GOST output descriptor and restoration path.
- Startup greeting generation at `20456` and session-end/farewell generation
  at `20475` are native C++, but observable output still passes through the
  semantic `20674`/`Э71` path.

## Extracodes and arithmetic

- `Э50` effective addresses are: `0` SQRT, `1` SIN, `2` COS, `3` ATAN,
  `4` ASIN, `5` LOG, and `6` EXP. Results use the BESM 48-bit biased-exponent
  representation and clear RMR.
- `Э50/000` must reproduce truncation, not host rounding. The traced mapping is
  floating `2` `4110000000000000` to sqrt `4053240474631771`. Negative square
  roots raise `E50/000 square root of negative accumulator`.
- `Э53/010` returns local time since midnight in 1/50-second jiffies, including
  the current 20 ms fraction.
- `Э63/004` returns elapsed image-execution time in 1/50-second jiffies.
- `Э74/000` terminates image execution normally.
- `Э75` stores generated instruction words when its effective address is
  nonzero.

## Current semantic-port landmarks

- All 98 direct `vjm` targets in the saved quine trace have semantic dispatch.
  Remaining fallback generally consists of computed continuations, short
  static trampolines from other workloads, blocking extracode sites, and
  generated code.
- The shared character converter is `21260..21275`; output buffering continues
  at `25346`, and packed descriptor advancement is at `21443`.
- `PRSTRI` and `PRREAL`, startup/farewell message construction, input transfer,
  output transfer, and the prominent quine/game/compiler clusters documented
  in `subroutines-from-trace.md` are already semantic.
- Large multi-entry implementations such as `03544`, `04001`, `04214`,
  `05052`, and `16351` intentionally accept an entry address while retaining
  every dispatch case.
- C++-private labels `06352`, `06363`, `06367`, `06375`, `07703`, `07726`,
  `07730`, and `07746` were safely inlined into their sole callers; their
  original address comments remain in `src/machine.cpp`.

Consult `subroutines-from-trace.md` rather than duplicating its full evolving
address catalog here. Treat historical profile counts in documentation as
measurements tied to their stated corpus and revision, not current truth.
