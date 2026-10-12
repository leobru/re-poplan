# Native Boolean condition syntax

Native `IF`/`ELSEIF` conditions now accept historical `AND` and `OR` separators,
and direct `NOT(...)` calls are implemented. All execution uses typed POP
operations; no BESM instruction execution or reference-backend fallback is added.

## Historical behavior and lowering

| Facility | Source evidence | Native behavior |
| --- | --- | --- |
| `AND` in an IF condition | `04231..04236`, selector constant `04415`, emission `05054`, branch routine `20073` | Consume the term; false jumps directly to this condition's false arm, otherwise evaluate the next term |
| `OR` in an IF condition | `04240`, selector constant `04416`, emission `05055`, branch routine `20077` | Consume the term; true jumps directly to this condition's THEN arm, otherwise evaluate the next term |
| THEN / ELSEIF / ELSE / CLOSE | Existing `04214..04306` compiler family | Patch each condition's early false/true branches independently, including nested IFs and ELSEIF chains |
| `NOT(...)` | Resident `02054`, body `03371..03373`, return path `03314` | Pop one value and return tagged TRUE only if it is exactly tagged integer zero; otherwise return tagged FALSE |
| Invalid AND/OR placement | Historical recovery fixture | Report 04700 with source/token context and resume on the next line |

`AND` and `OR` are condition separators, not ordinary infix Boolean expressions.
Evaluation proceeds left-to-right until an AND term is false, an OR term is true,
or the last term selects an arm. In particular:

```
IF FALSE AND TRUE OR TRUE THEN 1 ELSE 2 CLOSE=>
IF TRUE OR FALSE AND FALSE THEN 1 ELSE 2 CLOSE=>
```

These print 2 and 1, respectively. The remaining terms are skipped. Parentheses may group a numeric expression
inside a term; they do not create a general AND/OR expression. `TRUE AND FALSE=>`
and `IF (TRUE OR FALSE) THEN ...` produce historical error 04700.

Only the complete tagged integer-zero word is false. Real zero, UNDEF, and
ordinary function objects are true. Every tested term consumes one value;
additional results remain on the POP stack. NOT likewise consumes just one
value: `NOT(12,0)=>` prints `12, 1`.

The compiler stores separate early-false and early-true branch lists for each
conditional. Targets are POP operation indices, not historical execution PCs.
True branches have `05055` allocation provenance. NOT uses the ordinary call
emission family and a dedicated native operation. Pending NOT argument groups
and conditional fixups persist across input suspension.

## Coverage and boundary

`native-booleans.pop2` tests tagged truth, mixed chains, repeated separators,
short-circuit side effects, skipped division errors, nested IFs, ELSEIFs, nested
NOT calls, ordinary functions/returns, and extra/multiple stack results.
`native-booleans-recovery.pop2` checks invalid separator placement, evaluated
arithmetic errors, SETPOP inside a condition, and subsequent execution.

Both fixtures run under native, guarded native, hybrid, instruction-only, and
historical execution. Every byte split checks output, native operation counts,
allocation scheduling, dictionary identity, bindings and classes. Final raw
binding checkpoints remain part of the reader test. As in earlier recovery
fixtures, instruction-only comparison omits its missing catalog explanation
lines; native and historical diagnostics agree including those explanations.

Bare NOT as a function value, aliases to it, and mutable resident bindings remain
unsupported and fail explicitly. This milestone does not add general Boolean
infix syntax, loops, EXIT, GOTO, or LAMBDA. Full primitive scratch accounting,
legacy exhaustion rendering/recovery, and complete language coverage remain open.

Validation logs and experiments are in `artifacts/native-booleans/`. Earlier
native operation and historical machine-step measurements are unchanged.

The new fixtures execute 252 (`native-booleans`) and 15
(`native-booleans-recovery`) native operations. Reader/compiler work is not
counted; executed branches and labels are counted, including early arm selection.

Verification passed: all 20 regular CTests (84.95 s), all 20 ASan/UBSan CTests
(450.66 s, leak detection disabled), smoke, quine, zone1224, historical native
fixture comparisons, and separate historical trivial/full-zone comparisons.
Guarded native execution and every-byte input suspension checks passed.
`git diff --check` passed. Logs and command exit statuses are retained in the
artifact directory above.
