# Standalone POPLAN

A separate C++17 POPLAN interpreter using tagged signed 64-bit integers, IEEE
binary64 reals, Unicode text, stable object handles and captured mutable cells.
It builds and runs without any parent-project source, extracted image, instruction
interpreter, historical character encoding or simulated memory.

This is an **initial implementation, not a complete POPLAN port**. The language
and standard-function fixtures, corrected Man-or-Boy, quine, recovered example
library and fixed-seed game have executable coverage. See [status](docs/status.md)
for remaining work and deliberate differences.

```sh
cmake -S standalone -B build/standalone -DCMAKE_BUILD_TYPE=Debug
cmake --build build/standalone
ctest --test-dir build/standalone --output-on-failure
printf '2+2=>\n' | build/standalone/poplan-native
build/standalone/poplan-native standalone/tests/fixtures/man-or-boy.pop2
```

From a copy of this directory alone, replace `-S standalone` with `-S .`.
The parent repository also offers `make standalone-test` and
`make standalone-sanitize`. Installation uses normal CMake installation:
`cmake --install build/standalone --prefix /your/prefix`.

The command reads source files, or standard input when no files are given.
It executes complete commands before EOF and prompts when reading a terminal.
`--library-path DIR` adds a directory containing `POPLIB/NAME.pop2` files.
`--max-frames N` and `--max-operations N` set optional safety limits;
`--stats` reports native operations and maximum frame depth observed at a user call.
There are no machine-step counts or machine-control options.

Numbers have separate tags: `ISINTEGER(3)` and `ISREAL(3.0)` are both true;
`3=3.0` is false. Ordered comparisons compare numeric values without first
rounding large integers to doubles. Integer `+`, `-`, `*`, nonnegative integer
powers and division overflow raise recoverable errors. `/` produces a real;
`//` produces remainder followed by floor quotient. Real arithmetic uses the
host's IEEE binary64 operations; division by zero and nonfinite/domain results
raise errors. Subnormals and signed zero are retained. There are no emulated
word widths, accumulator modes or historical exhaustion thresholds.

Characters are Unicode scalar values. Names remain case-insensitive with six
significant characters and the existing Cyrillic/Latin lookalike aliases.
Strings retain UTF-8 text. `POPMESS([DO 'path'])` and `[DI 'path']` use ordinary
host files; `[TO]`, `[CO]`, `[LPO]`, `[TI]` and `[CI]` use console streams.
File consumers close on `TERMIN`; `[CLOSE function]` closes a file explicitly.
Historical memory/disk/device descriptors and character-code conversions fail
explicitly. They are not reinterpreted as host addresses.

`COREUSED` returns zero for an unspecified free-space budget, followed by an
estimate of bytes used by objects and cells. It does not report machine words,
process RSS or a fabricated capacity. `POPTIME` returns elapsed seconds;
`POPDATE` preserves its date-string and 20 ms time-of-day interface.

The C++ API accepts injected input/output streams and clocks. Use
`feed(source_id, bytes, eof)` and `resume()` to advance a session; results report
`need_input`, `finished` or `failed` and the number of new diagnostics. Compiler,
reader, macro and lazy-list state survives input suspension and garbage collection.
`source()` remains a convenience wrapper. Direct CHARIN reads its injected stream.

Automatic ERRFUN receives a modern message string and code 1. Rebinding it
controls diagnostic output. After a handler returns, recovery clears the stack,
restores standard source/output and preserves current variable values. Explicit
SETPOP prints its banner and performs the same reset. See the [status ledger](docs/status.md)
for unresolved syntax and compatibility details.
