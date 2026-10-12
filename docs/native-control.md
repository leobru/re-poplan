# Native comparisons and conditional execution

This is the first part of step 3 in `PLAN.md`, not completion of that step.
Named functions, formal/local binding restoration, calls, returns and recursion
were added in the subsequent [function milestone](native-functions.md). Native execution still neither loads the executable image
nor calls the machine backend.

## Evidence and implementation

| Facility | Historical evidence | Native lowering / behavior |
| --- | --- | --- |
| `=` | Resident descriptor `01450`, `03374..03402` | Compare complete numeric representations or stable object handles; integer `1` differs from real `1.0` |
| `<`, `>`, `=<`, `>=` | Descriptors `01400..01414`, `06620..06671` | Resident precedence; operation-local BESM subtraction and sign test, without arithmetic result retagging or its range check |
| Comparison type errors | `06650..06656`, catalog `14030..14061` | Check right operand first, then left; report the offending value and recover |
| `IF`, `THEN`, `ELSEIF`, `ELSE`, `CLOSE` | Compiler `04214..04306`, emission selectors `05054`, `05056`, `05070` | Explicit nested compiler records, typed false branches, jumps and labels with operation-index targets |
| Truth test | `20073..20107` | Consume condition; only complete tagged integer zero is false. Real zero and UNDEF are true |
| Multiple statements/results | Historical `native-control.pop2` fixture | Preserve branch stack results and mutations; skipped branches have no runtime effects |
| Multiple prints in one compiled unit | Historical multiline print probe | Preserve the blank line between print calls even across source lines |

Branch, jump and label operations retain source provenance and legacy storage
costs. Labels have two-word intermediate nodes and zero final code words.
Execution uses the operation cursor; nesting does not introduce C++ recursion.
The pending conditional records, labels and expression boundaries survive input
suspension. EOF does not execute an unfinished conditional. SETPOP recovery
clears pending compiler state while preserving established bindings.

At this conditional milestone, general calls and RETURN were still absent; the
subsequent function milestone implements ordinary named calls and RETURN. Loops,
EXIT, declarations inside conditional bodies and unsupported malformed conditional
syntax still fail explicitly. No missing syntax
behavior is inferred from a generic POP grammar. The existing whole-session
allocation caveat applies: historical reader temporaries, optimizer decisions
and environment scheduling are not reproduced. Operation storage costs do not
establish total COREUSED or recursive exhaustion compatibility.

## Reference correction

The new nonnumeric comparison probes exposed a hybrid bug in `p06650`:
`06652` and `06655` were testing zero instead of the multiplicative sign
condition. The left-operand error path also omitted the `06656` reload of the
offending value. Both now follow the listing. Complete-state differentials cover
numeric values, UNDEF and a function descriptor, including PC/half, ACC, RMR,
ALU mode, every register, stack balance and all 32K memory words. No new semantic
dispatch entry or instruction-word recognition was introduced.

## Verification scope

- 845 numeric comparison cases compare exact Boolean bits and operand/temporary
  stack effects with instruction-only execution, including mixed numeric types,
  wide signed integers, real zero, tiny real values and adjacent real encodings.
- 25 complete-state `06650` differentials cover both type checks and success.
- Every byte split of both new fixtures preserves output, operation counts,
  native allocation outcomes, dictionary identities and binding mutations;
  repeated empty feeds do not replay effects.
- `tests/native-probe.py` runs the fixtures through native, the machine-execution
  guard from an image-free directory, hybrid and instruction-only modes, plus
  historical POPLAN with `--historical`. As in the reader recovery fixture, only
  the known explanatory catalog lines are omitted by instruction-only execution.
- A 300-level nested conditional runs through native and the execution guard;
  it is a host-recursion check, not a historical exhaustion assertion.

Historical output is checked in under `tests/expected/native-control*.out`.
Logs and one-off probes belong in `artifacts/native-control/`. Earlier corpus
machine-step measurements remain unchanged; native operation counts are separate.

The new fixtures execute 219 native operations (`native-control`) and 40
(`native-control-recovery`), including visited labels and operations that raise
supported diagnostics. Compiler work and skipped operations are not counted.

Validation completed on 2026-10-06: all 19 regular CTests passed (79.54 s),
as did all 19 ASan/UBSan CTests (381.02 s, `ASAN_OPTIONS=detect_leaks=0`,
`UBSAN_OPTIONS=halt_on_error=1`). The initial sanitizer run was interrupted by
a server restart; the completed rerun records exit status zero separately.
`make test`, `make cpp-quine`, `make cpp-zone1224`, historical native-fixture
comparisons, historical trivial/full-zone1224 comparisons, the image-free
execution guard, and `git diff --check` passed. The zone target alone compares
hybrid with instruction-only execution; historical conformance was checked
separately through `tools/run-dispak.sh`.


## Boolean condition syntax added — 2026-10-10

Historical AND/OR condition separators and direct NOT calls are now supported.
They preserve early arm selection and tagged-zero truth, including the unusual
mixed-chain behavior. See [Boolean evidence](native-booleans.md) for source
lowering, suspension/recovery fixtures, and the remaining direct-call boundary.
Earlier operation counts and validation measurements above remain unchanged.
