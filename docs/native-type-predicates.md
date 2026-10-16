# Native type predicates — 2026-10-11

Direct `ISINTE(...)`, `ISREAL(...)` and `ISFUNC(...)` calls now compile to typed
native operations. They consume one value and push tagged TRUE or FALSE, leaving
earlier stack results intact. Native execution does not load an image, execute
BESM instructions or use reference-backend fallback.

## Source and implementation

| Predicate | Historical evidence | Native decision |
| --- | --- | --- |
| ISINTE | Resident `01740`, `03403..03406`, constants `03453..03454` | Numeric value whose class under mask `7740000000000000` equals integer tag `6400000000000000` |
| ISREAL | Resident `01754`, `06605..06610`, constant `06665` | Numeric word below the tagged-integer boundary; tagged integers are false |
| ISFUNC | Resident `01734`, `06600..06604`, constants `06662..06664` | Native function kind; the reference accepts both ordinary and special callable classes |
| Compilation | Existing ordinary-call emission family `05064` | Resumable argument groups emit `is_integer`, `is_real` or `is_function`; provenance is not an execution address |

`ISREAL(1)` is FALSE and `ISREAL(1.0)` is TRUE. Both Boolean constants satisfy
ISINTE. UNDEF satisfies none of these predicates. Ordinary function objects,
including escaped nested functions and aliases to user functions, satisfy ISFUNC;
calling a function first tests its returned value instead. Long spellings retain
six-character significance: ISINTEGER and ISFUNCTION select ISINTE and ISFUNC.

## Coverage and remaining boundary

The reader test adapts 126 predicate cases into native and instruction-only
representations and compares exact Boolean bits, argument consumption,
preservation of earlier native stack results, and raw scratch-stack balance.
Inputs include zero, signed integer limits, small and boundary real encodings,
UNDEF, both raw callable tags, and native pair/reference objects. Mapping the raw
special-function tag to the native function kind tests classification only;
creation and invocation of special functions are not implemented by this change.

`native-type-predicates.pop2` covers user functions and aliases, dynamic bindings,
escaped nested functions, conditional arguments, multiple results, short circuit
conditions, nested numeric calls and long predicate spellings. The recovery
fixture checks arithmetic/type errors within arguments, SETPOP and subsequent
execution. Both fixtures run under native, guarded image-free native, hybrid,
instruction-only and historical POPLAN. As in earlier fixtures, instruction-only
recovery comparison omits its missing catalog explanation lines.

Every byte split and repeated empty feed checks output, operation counts,
allocation outcomes and dictionary state. The fixtures execute 210 and 15 native
operations respectively. Existing measurements remain unchanged.

First-class values or aliases for these resident predicates remain explicitly
unsupported. General word/list/string construction, other type predicates,
special-function execution, complete legacy allocation accounting and exhaustion
recovery remain open. This milestone does not complete an implementation gate.

Logs and historical outputs are retained in `artifacts/native-type-predicates/`.

Verification completed: all 20 regular CTests passed (100.47 s), and all 20
ASan/UBSan CTests passed (636.00 s, leak detection disabled and UBSan
halt-on-error). Focused reader/probe tests, `make test` smoke, `make cpp-quine`,
`make cpp-zone1224`, historical native-fixture comparisons, separate historical
trivial/full-zone1224 comparisons, and `git diff --check` passed. Exit statuses
and logs are retained in the artifact directory above.
