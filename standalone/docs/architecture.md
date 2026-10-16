# Runtime architecture

The executable links four runtime translation units and its CLI. It has no
image, instruction decoder, machine-state object or historical allocator.

`Value` is a tagged variant containing an int64, double, object handle,
UNDEF or TERMIN. NIL is the canonical interned word at stable handle 1. Handles are monotonically assigned identifiers; moving a C++
container never changes language identity. Words are interned separately from
strings. A word points to its current identifier record; compiled operations and
activation environments retain identifier identities. CANCEL detaches that record
without changing the interned word or retargeting old code. Unreachable retired
records and cells are collected. Pairs, references, records, strips and functions are heap objects.
Generated record/strip functions share a class descriptor, so changing DATAWORD
changes the class label without conflating independently constructed classes.

The compiler emits typed operations: push/load/store, calls, closures, branches,
returns, list construction and loop control. There is no generated machine code.
Programs contain handle-valued constants and are roots while being compiled or
executed. Runtime source loading pushes an input context and a compiler
continuation, allowing a loaded source to define macros used by its later
statements. The compiler runs an explicit task stack. PROGLIST/NEXTCH are authoritative
source lists; host tokens arrive through a lazy source supplier. Cursor consumption
preserves the current pair identity, deferring a component copy when host input is
needed. COMPILE interleaves supplier callbacks with compilation/execution, while
POPVAL copies its cursor head and preserves its argument list. Token payloads
retain numeric tags and object handles rather than being serialized to source.

User calls install fresh mutable cells for parameters and locals. Each frame
remembers the previous dynamically visible cells. Nested functions capture the
actual cells from their defining invocation; sibling closures share them.
Normal returns restore dynamic bindings. SETPOP and automatic error recovery
discard frames while preserving the active cells, as required by the manual. A function defined elsewhere
continues to resolve free variables dynamically. This combination fixes the
Man-or-Boy rebinding failure without replacing POPLAN with a purely lexical
language.

The VM runs an explicit frame loop. Partial functions and callback sequences also
schedule typed operations; a cyclic partial cannot recursively overflow the C++
call stack. Parser nesting, readers and nested macro execution also retain explicit
continuations. Lazy-list forcing queues supplier/commit continuations and memoizes
the result in the shared pair. Traversals resume after forcing rather than calling
back into the VM recursively.

Collection marks interned words, active and saved cells, the value stack,
activation environments, macro bindings, program constants and transitive object
fields, source cursors, saved source contexts, deferred cursor copies and
computed callees. It sweeps unreachable objects and cells, including cycles. Collection
runs between VM operations and at the end of a source submission. It never runs
in the middle of a primitive while that primitive holds unregistered temporary
handles. Weak compilation roots keep incomplete programs alive during macro
execution without keeping completed programs alive forever.

Host streams and clock providers are supplied to Runtime. GENOUT wraps a print
function in an invocation with a dynamically rebound CUCHOUT cell. Printing
schedules Unicode scalar values through that consumer. POPMESS file functions own
host stream objects and close explicitly or when their last object becomes
unreachable. No value is interpreted as a host pointer or a device address.

REFOF returns a stable reference to an identifier's dynamically visible cell.
Repeated calls for the same current identifier return the same reference. COPY of
that reference produces an ordinary independent reference containing its current
value. CANCEL does not redirect existing references to the new identifier.

Language faults schedule ERRFUN through the same VM loop. The default handler
renders the modern message; replacement handlers receive message/code arguments.
Nested handler errors use a bounded direct diagnostic path. Source lexer failures
are deferred until the reader reaches them, preserving preceding command effects.
UTF-8 decoding retains the length of the valid byte prefix so malformed later
bytes cannot suppress earlier complete commands; that prefix is not treated as
a synthetic EOF.
The numeric callback code is currently the modern generic code 1, not an assertion
of historical catalog coverage.
