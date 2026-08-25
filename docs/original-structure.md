# Original POPLAN Structure

This document records constraints for the C++ translation. The translation
must follow confirmed POPLAN routines and object formats; it must not replace
the compiler with an independently designed POP-2 frontend.

All addresses and words are octal.

## Tagged Function Objects

`EVAL_DISPATCH` at `02750` establishes the function representation:

- `(value & 7700000000000000) == 6600000000000000` identifies a function;
- `value & 0040000000000000` selects the special path at `15765`;
- `(value >> 24) & 77777` is the captured environment address;
- `value & 77777` is the code entry address.

For example, the function displayed during the unmodified Man-or-Boy failure,
`6606563700065620`, has environment `65637` and entry `65620`. It is a
well-formed ordinary function object.

The first translated C++ routines retain their original addresses:

| Address | C++ routine | Original operation |
|---:|---|---|
| `01107` | `p01107` | Build the observed hash value, search its collision chain, and allocate a record when absent |
| `01167` | `p01167` | Select the alternate modifier-register setup and enter the shared `01122` body |
| `02750` | `p02750_dispatch` | Validate and dispatch a POP function |
| `02767` | `p02767` | Load an indirect evaluator value and redispatch it |
| `02770` | `p02770` | Validate an indirect function record through the original table and tag tests |
| `03206` | `p03206_prepare_ordinary_call` | Install an ordinary function and prepare captured slots |
| `03235` | `p03235_bind_environment` | Bind captured values and restore the caller environment |
| `03261` | `p03261_enter_function` | Enter a function through the saved descriptor |
| `03275` | `p03275_push_acc` | Decrement `r6`, then store the accumulator |
| `03277` | `p03277_pop_acc` | Load through `r6`, then increment `r6` |
| `03536` | `p03536` | Build the observed five-word frame and transfer through the continuation in `r16` |
| `03716` | `p03716` | Follow two address fields, cyclically update the selected word, and retain the computed-transfer exit |
| `03724` | `p03724` | Transform a shared table value and preserve the `03725` allocation re-entry |
| `03736` | `p03736` | Preserve compiler selection state and retain the nested `17417`, `04467`, `04214`, and generated continuations |
| `04074` | `p04074` | Preserve a two-word frame around five writes through the shared `17340` table body |
| `04322` | `p04322` | Apply the observed classification masks and preserve its nested `04467`, `16313`, and `02764` continuations |
| `04426` | `p04426` | Recognize and replace either of the two table-defined record-field markers |
| `04447` | `p04447` | Preserve two scratch values, allocate a pair through `05215`, and resume at `04455` |
| `04467` | `p04467` | Preserve the caller around `06343` and install its result through the `04471` continuation |
| `04665` | `p04665` | Preserve compiler state around repeated `03536` and `04467` entries and restore it at `04673` |
| `04675` | `p04675` | Preserve compiler state around the observed marker, table-scan, record-update, and allocation branches through `04756` |
| `04740` | `p04740` | Traverse or construct the selected two-word chain while retaining its stacked return protocol |
| `06343` | `p06343` | Build the observed compiler frame before entering `06526` |
| `06526` | `p06526` | Build the observed helper frame and call the `17045` path |
| `06623`, `06631`, `06637`, `06645` | `p06623`, `p06631`, `p06637`, `p06645` | Execute the four generated comparison templates through their shared `06650` body |
| `06650` | `p06650` | Perform the generated two-stage comparison and preserve its diagnostic continuations |
| `06712` | `p06712` | Shared numeric normalization and result-packaging path |
| `06733` | `p06733` | Checked add entry into the shared `06712` path |
| `06740` | `p06740` | Checked reverse-subtract entry into the shared `06712` path |
| `06744` | `p06744` | Checked multiply entry into the shared `06712` path |
| `06750` | `p06750` | Checked divide entry into the shared `06712` path |
| `07673` | `p07673` | Preserve a five-word compiler frame around the original classification, helper, bit-loop, and cleanup paths through `07745` |
| `11464` | `p11464` | Preserve an address and caller frame, allocate through `05430` when nonzero, and resume at `11471` to store the transformed word |
| `11500` | `p11500` | Load a word indexed by the frame word at `r17-1` |
| `15765` | `p15765_dispatch_special_function` | Expand a `664` descriptor's counted values and redispatch its nested function |
| `16254` | `p16254` | Narrow two scratch values through the original multiply/RMR-derived table offsets and return the selected address |
| `16341` | `p16341` | Preserve the observed compiler registers around record processing |
| `16477` | `p16477` | Return a populated record word or evaluate and install its missing value |
| `16742` | `p16742` | Look up the record byte through `16421`, adjust its code, and return through `r7` |
| `17013` | `p17013` | Preserve an evaluator argument around the `21464` call path |
| `17021` | `p17021` | Sibling evaluator wrapper with its original continuation offset |
| `17045` | `p17045` | Preserve two evaluator arguments around the `21464` call path |
| `17337`, `17341` | `p17337`, `p17341` | Select one of two descriptors and enter the shared table-read body |
| `17340`, `17342` | `p17340`, `p17342` | Select one of two descriptors and enter the shared table-write body |
| `17417` | `p17417` | Classify the compiler object while preserving its nested calls and two-word saved frame |
| `17762` | `p17762` | Allocate a two-word object and complete its tagged links at `17764` |
| `17774` | `p17774` | Classify and traverse the selected record chain, retaining the `20002` stamping continuation |
| `20077` | `p20077` | Select and restore a generated return through the POP value stack |
| `20110` | `p20110_transfer_arguments` | Transfer POP-stack arguments into an activation |
| `20124` | `p20124_build_activation` | Build an activation and push its actual values |
| `21464` | `p21464` | Classify the traced evaluator value and select its original continuation |

The static `NEWARR` value `6641223600000000` establishes the special branch's
layout. Environment word `12242` points at the counted vector beginning at
`12243`; count `3` causes values `12244` and `12245` to be pushed. Environment
word `12241` contains `6600000000012164`, which is then sent back through
`02750`. The C++ translation retains this indirection and stack order rather
than replacing `NEWARR` with a host primitive.

## Dictionary

The static dictionary is a sequence of four-word records:

1. packed name;
2. class word, normally tagged `650`;
3. properties or associated data;
4. value, often a tagged `660` function containing its original entry point.

`tools/extract-dictionary.py build/poplan.lst` produces the current inventory.
This table supplies public routine entry points such as `COMPIL` at `10217`
and `FNCOMP` at `10421`. Decoded name fragments are not expanded by guesswork.

## Compiler Recovery

Controlled trace deltas currently divide the compiler into these observed
regions:

- a top-level function adds compiler paths in `03461..05213`,
  `11470`, and `17070..17774`;
- a nested function additionally exercises `03702`, `03716`, `04001..04116`,
  `16254`, `17341`, `17342`, and `17472`;
- capturing a lexical value adds the small paths at `03566..03571`;
- recursive captured calls additionally exercise `04161`, `04214`, `04507`,
  `06637`, `06650`, `06712`, `06740`, and `20077`.

These are coverage facts, not final semantic names. C++ compiler functions
will be introduced only after their inputs, outputs, and callers are
established from the listing and traces.

## Man-Or-Boy Failure

The canonical translation succeeds through `k=3` and fails at `k=4`.
The immediate failure is storage exhaustion, and the displayed closure is
well formed:

1. Generated function `65620` calls activation builder `20124`.
2. The runtime constructs tagged closure values and argument records.
3. Erroneous closure rebinding causes calls to continue instead of returning.
4. The configured watchpoint fires when activation storage reaches `067000`.
5. The handler reports POPLAN error `13000` and prints the active value.

The memory boundaries are constants:

| Address | Original | Use |
|---:|---:|---|
| `17010` | `066000` | Initial `r17` and lower generated-code boundary |
| `17011` | `070000` | Initial POP value stack in `r6` |
| `17012` | `067000` | Collision watchpoint |

Moving only the watchpoint to `066600`, `066400`, or `066200` merely delays
error `13000`. At `066000`, activation storage overwrites live generated
function data and produces error `12010`. Relocating the lower boundary as
far as the static image permits and moving the POP stack to the top of memory
still exhausts before completing `k=4`.

These experiments rule out a memory-limit-only fix for the erroneous
execution. They do not show that a correct execution requires more memory.
The reduced arity-3 trace below identifies failed closure-environment
rebinding as the primary defect; storage exhaustion is its consequence. A
faithful C++ reproduction is needed to determine whether correcting that
binding operation is sufficient and localized.

## Recursive Triangular Numbers

`tests/inputs/triangular-no-local.pop2` uses the conventional recursive
definition

```text
T(0) = 0
T(n) = n + T(n - 1)
```

Each depth was tested in a fresh invocation of POPLAN, so interactive-session
allocation could not accumulate between trials. `TRI(124)` succeeds and
returns `7750`; `TRI(125)` is the first failure and reports error `13000`.

The failure is storage exhaustion, not integer overflow. In the failing trace,
programmed extracode `067` returns to the supervisor appeal path at `20150`,
and `20157` selects diagnostic `13000`. Thus this program supports 125
simultaneous calls (`TRI(124)` through `TRI(0)`) in the unmodified image.

`tests/inputs/triangular-one-local.pop2` declares one local variable while
leaving the function body unchanged:

```text
FUNCTI TRI N; VARS T;
  IF N=0 THEN 0 ELSE N+TRI(N-1) CLOSE;
END;
```

With this version, `TRI(82)` succeeds and returns `3403`; `TRI(83)` is the
first error `13000`. The capacity is therefore 83 simultaneous calls, down
from 125: one declared local removes 42 recursion levels, a 33.6 percent
reduction. The local is deliberately unused so that this comparison isolates
the activation-record cost from changes to expression evaluation.

`tests/inputs/triangular-five-fakes.pop2` instead adds five fake parameters
and passes them unchanged through every recursive call:

```text
FUNCTI TRI N FAKE1 FAKE2 FAKE3 FAKE4 FAKE5;
  IF N=0 THEN 0
  ELSE N+TRI(N-1,FAKE1,FAKE2,FAKE3,FAKE4,FAKE5)
  CLOSE;
END;
```

`TRI(34,0,0,0,0,0)` succeeds and returns `595`; an initial argument of `35`
is the first error `13000`. The six-parameter function therefore supports 35
simultaneous calls, down by 90 levels (72 percent) from the one-parameter
baseline.

`tests/inputs/triangular-nested-numeric-fakes.pop2` moves the recursive
expression into a nested function:

```text
FUNCTI TRI N FAKE1 FAKE2 FAKE3 FAKE4 FAKE5;
  FUNCTI B;
    N+TRI(N-1,FAKE1,FAKE2,FAKE3,FAKE4,FAKE5);
  END;
  IF N=0 THEN 0 ELSE B() CLOSE;
END;
```

Here `TRI(26,0,0,0,0,0)` succeeds and returns `351`; `TRI(27,...)` is the
first error `13000`, reporting the collision address `067000`. The captured
nested function reduces capacity to 27 simultaneous calls: eight fewer
(22.9 percent) than direct recursion with the same parameters, and 98 fewer
(78.4 percent) than the original one-parameter baseline.

`tests/inputs/triangular-nested-zero-fakes.pop2` also defines a top-level
function and passes it in all five fake positions:

```text
FUNCTI ZERO; 0; END;

TRI(10,ZERO,ZERO,ZERO,ZERO,ZERO)=>
```

This does not change the observed recursion boundary: `TRI(26,...)` returns
`351`, and `TRI(27,...)` fails with error `13000` at `067000`. At this
granularity, passing function objects costs the same activation space as
passing integer zeroes.

The current `tests/inputs/triangular.pop2` cyclically permutes those arguments
and invokes the first one in the base case:

```text
FUNCTI TRI N FAKE1 FAKE2 FAKE3 FAKE4 FAKE5;
  FUNCTI B;
    N+TRI(N-1,FAKE2,FAKE3,FAKE4,FAKE5,FAKE1);
  END;
  IF N=0 THEN FAKE1() ELSE B() CLOSE;
END;
```

With all five arguments bound to `ZERO`, it still returns `351` for `TRI(26)`
and first reports error `13000` for `TRI(27)`. The cyclic permutation and
additional base-case call cause no measurable change to the recursion limit.

## Dynamic Man-Or-Boy Depth

`tools/man-or-boy-depth.py` implements the canonical test with the same
function structure as POPLAN: every `A` owns a mutable `K`, nested `B` captures
it, and `B` is passed as the next invocation's first functional argument.

The `MAX_DEPTH` column counts simultaneously active POP functions of every
kind (`A`, `B`, and constant leaf functions). `MAX_A_DEPTH` counts only active
`A` invocations.

| K | Result | Max depth | Max A depth | Total calls |
|---:|---:|---:|---:|---:|
| 0 | 1 | 2 | 1 | 3 |
| 1 | 0 | 4 | 2 | 5 |
| 2 | -2 | 6 | 3 | 7 |
| 3 | 0 | 8 | 4 | 9 |
| 4 | 1 | 16 | 8 | 18 |
| 5 | 0 | 32 | 16 | 41 |
| 6 | 1 | 64 | 32 | 88 |
| 7 | -1 | 128 | 64 | 188 |
| 8 | -10 | 256 | 128 | 397 |
| 9 | -30 | 512 | 256 | 833 |
| 10 | -67 | 1024 | 512 | 1750 |

For `K >= 4`, both nesting measures double for every increment of `K`.
`K=4` has a maximum total nesting of only 16, yet the BESM implementation
already exhausts its storage there. Ordinary live call depth alone therefore
does not explain the failure; retained activation and closure environments
must account for the additional pressure.

## Four-Argument Man-Or-Boy Variant

`tests/inputs/man-or-boy-four.pop2` removes `X5` and rotates four functional
arguments:

```text
FUNCTI A K X1 X2 X3 X4;
  FUNCTI B;
    K-1->K;
    A(K,B,X1,X2,X3);
  END;
  IF K=<0 THEN X3()+X4() ELSE B() CLOSE;
END;
```

The host implementation runs this form with
`tools/man-or-boy-depth.py --arity 4`. Its first values and depths are:

| K | Expected result | Max depth | Max A depth | BESM POPLAN |
|---:|---:|---:|---:|---|
| 0 | 0 | 2 | 1 | 0 |
| 1 | -2 | 4 | 2 | -2 |
| 2 | 0 | 6 | 3 | 0 |
| 3 | 1 | 12 | 6 | error `13000` |

The `K=3` trace shows programmed extracode `067` returning to the supervisor
appeal path at `20150`, followed by diagnostic selection at `20157`. Removing
one formal therefore does not avoid the failure. It also changes the rotation
period and dynamic closure-use pattern, so this result is not a pure
activation-frame-size comparison with the five-argument test.

Continuing the same reduction gives:

| Functional arguments | Initial functions | First problematic K | Host behavior | BESM POPLAN |
|---:|---|---:|---|---|
| 5 | `1,-1,-1,1,0` | 4 | result 1, depth 16 | error `13000` |
| 4 | `1,-1,-1,1` | 3 | result 1, depth 12 | error `13000` |
| 3 | `1,-1,-1` | 2 | result 1, depth 8 | error `13000` |
| 2 | `1,-1` | 1 | does not terminate | runs until interrupted |

The three-argument source is `tests/inputs/man-or-boy-three.pop2`. It succeeds
through `K=1` and exhausts POPLAN storage at `K=2`, although the correct host
execution needs a maximum nesting of only eight.

The instrumented source `tests/inputs/man-or-boy-three-debug.pop2` prints `B`
and its captured `K` immediately after decrement. Correct `K=2` execution has
three `B` calls:

1. outer `B0`: `K` changes from 2 to 1;
2. inner `B1`: `K` changes from 1 to 0;
3. outer `B0` again: its retained `K` changes from 1 to 0, then evaluation
   terminates with result 1.

POPLAN instead prints:

```text
FUNСТI 1
FUNСТI 0
FUNСТI-1
FUNСТI-2
FUNСТI-3
...
```

The instruction trace removes the ambiguity caused by `PR` formatting every
function merely as `FUNСТI`. Every invocation dispatches descriptor
`6606562700065576` to generated entry `65576`. The load at `65577` reads the
same generated `K` slot, `65763`, with successive pre-decrement words
representing `2`, `1`, `0`, `-1`, and `-2`.

On the third call that slot should have been rebound to outer `B0`'s retained
value 1. It remains at inner `B1`'s value 0 instead. POPLAN consequently calls
the effective equivalent of the same `B` indefinitely, just as the correct
two-argument variant does structurally, until storage exhaustion produces
error `13000`. This identifies failed closure-environment rebinding as the
cause of the arity-3 failure; the error is a consequence, not the primary bug.

The two-argument source is `tests/inputs/man-or-boy-two.pop2`. For `K=1`, its
first recursive call installs the outer `B` as `X1`; the base expression
immediately calls that `B` again. Each repetition decrements the same captured
`K`, but the two-slot rotation never removes `B`, so the computation has
unbounded nesting. POPLAN was still running when interrupted after ten
seconds.
