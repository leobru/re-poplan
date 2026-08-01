# Faithful C++ POPLAN Port

The target is a C++ reproduction of the complete BESM-6 POPLAN system. It is
not an independently designed POP-2 parser or interpreter.

## Compatibility Contract

- Preserve original POPLAN routine boundaries and use their octal entry
  addresses in C++ names.
- Preserve 48-bit tagged values, dictionary records, function descriptors,
  generated objects, stack direction, and activation/environment layout.
- Derive compiler phases and calls from the original listing and traces.
- Establish behavior against the unmodified volume `2148` image.
- Reproduce known original failures before changing their semantics.

The initial C++ baseline must therefore reproduce the arity-3 closure-binding
failure: the third dynamic `B` call continues with the inner captured `K`
instead of restoring the older closure environment. A corrected mode will be
introduced only after that baseline is observable in a focused C++ test.

## Current Status

The address-preserving machine layer now translates:

- `02750..02763` function validation and dispatch;
- `03014..03071` normal diagnostic packaging, unpacking, and first formatter
  call;
- `03072..03100` first formatter-call restoration and heading setup;
- `03206..03234` ordinary-call setup and captured-slot preparation;
- `03235..03260` environment binding and restoration;
- `03261..03264` function entry;
- `03275`, `03277` POP value-stack operations;
- `07475..07504` traced `CUCHIN` argument paths;
- `16313..16333` packed-character sequence entry, dispatch, and restoration;
- `20110..20123` activation argument transfer;
- `20124..20140` activation construction;
- `21255..21257` character-input wrapper entry;
- `21431..21440` packed-text character extraction and cursor advance.

The C++ tests compare the one-argument `20124` transition at generated entry
`65576` with the BESM trace, exercise a three-argument activation round trip,
and reproduce the reduced arity-3 failure. Descriptor
`6606562700065576` repeatedly dispatches to `65576` through environment
`65627`; because the record at `65632` has a zero address, the third call
continues with shared slot `65763` at `K=0` instead of restoring outer `K=1`.

This is an evaluator/activation milestone, not yet a source-level C++ POPLAN.
The primitive runtime is now entered through the syntax-diagnostic output
path, but its character conversion, buffering, and console calls remain
incomplete. The original compiler, remaining runtime, I/O, and whole-system
driver must still be translated before the closure fix can be validated end
to end.

## Port Order

### 1. Tagged Machine State

Keep the current `Word48`, 32K-word core, accumulator, and BESM index
registers. Add object decoders only when their tag tests are confirmed in the
original routines.

Initial address-preserving entries:

- `02750`: function validation and dispatch;
- `03206..03264`: ordinary function call/environment selection;
- `03275`, `03277`: POP value-stack push and pop;
- `20110..20123`: activation entry and argument transfer;
- `20124..20140`: activation construction and value-stack transfer.

### 2. Evaluator and Activation Semantics

Transliterate the routines above instruction block by instruction block.
Tests must compare:

- register and memory transitions from short BESM traces;
- function descriptor, environment, and entry fields;
- activation stack and POP value-stack positions;
- nested closure rebinding at each dynamic call.

The arity-3 debug sequence is the first closure conformance gate. Correct
source semantics require decremented `K` values `1, 0, 0`; original POPLAN
produces `1, 0, -1, -2, ...`.

### 3. Primitive Runtime

Port public routines using dictionary-derived entry points and original
control flow. Arithmetic, predicates, list access, assignment, printing, and
console input are sufficient for the first source-level corpus, but their
implementation remains address matched rather than replaced with a new
runtime API.

### 4. Original Compiler

Recover and transliterate the compiler in its observed call structure.
Confirmed public anchors include:

- `10217`: dictionary fragment `COMPIL`;
- `10421`: dictionary fragment `FNCOMP`;
- `03461..05213`: source compiler paths reached by top-level functions;
- `03566..03571`: additional path for captured lexical values;
- `04001..04116`: additional nested-function compiler paths.

The C++ compiler must emit the same POPLAN object and environment formats
consumed by the translated evaluator. It must not be replaced by a separately
designed grammar or AST evaluator.

### 5. Whole-System Conformance

Run the same POP-2 inputs through the historical and C++ systems:

- primitive arithmetic, strings, lists, and conditionals;
- the quine;
- recursive triangular-number variants;
- nested closure and argument-rotation probes;
- the 4x4x4 tic-tac-toe program;
- canonical and reduced Man-or-Boy tests.

Diagnostics, output values, generated object tags, and relevant activation
transitions are comparison points. Host I/O wrapping and the historical batch
greeting are not required to match.

### 6. Closure Fix

After the faithful baseline reproduces the bug, add one isolated correction
to closure-environment selection or restoration. Keep the original behavior
available as a conformance mode.

Validation proceeds in this order:

1. arity 3, `K=2`, returns `1` with dynamic `B` sequence `B0,B1,B0`;
2. arity 4, `K=3`, returns `1`;
3. canonical arity 5, `K=4`, returns `1`;
4. canonical Man-or-Boy at `K=10` returns `-67`.

The fix is acceptable only if the existing quine, primitive, recursion, and
larger-program conformance tests remain unchanged.
