# POPLAN Diagnostic Path

All addresses and error codes in this note are octal.

## Syntax-error probe

The focused input `tests/inputs/syntax-error.pop2` contains:

```pop2
1+*2;
```

Run it and capture the original instruction path with:

```sh
tools/run-dispak.sh --bootstrap -t -t poplan.b6 \
    < tests/inputs/syntax-error.pop2 \
    > syntax-error.out 2> syntax-error.trace
```

POPLAN reports:

```text
:****НЕОПР. ИД-Р +*

****ОШИБКА 04020
НЕТ РАЗДЕЛИТЕЛЯ
7200000000016750 [’1+*’ [      .00000000  1 +*]]

SЕТРОР
:
```

The process exits successfully after recovering to the top-level prompt.

## Original control flow

The parser detects the error at `03615`: it places code `04020` in `r16` and
jumps to `03014` with the origin address in the accumulator. The diagnostic
dispatcher then performs these observable state changes:

| Address | Effect |
|---:|---|
| `03014..03016` | Save origin `03615` at `03173` and code `04020` at `03174`; test code bit `010000` |
| `03017..03020` | For this compiler error, replace `03173` with the current source object from `16553`, `7200000000016750` |
| `03030..03031` | Copy the object to `03176` and the code to `03204` |
| `03034` | Push source object `7200000000016750` on the POP stack |
| `03035..03036` | Tag `04020` with `6400000000000000` and push `6400000000004020` |
| `03037..03040` | Dispatch function descriptor `6600000000003051` through `02750` |
| `03051..03054` | Pop the code and object, restore `r16=04020`, and tail-call formatter entry `03057` |

This is ordinary POP function dispatch, not a separate host-level exception
mechanism. Entry `03057` saves the diagnostic context and invokes the POPLAN
message and number-formatting machinery. On completion, the runtime prints
`SЕТРОР` and resumes its command loop.

## C++ reproduction

`Machine::p03014_dispatch_error()` and
`Machine::p03051_unpack_error()` reproduce the packaging and unpacking above.
The translated path now continues through the first formatting bracket:

| C++ entry | Original behavior reproduced |
|---|---|
| `p03057_begin_error_format()` | Saves the diagnostic context and dispatches tagged character `0136` through `07475` |
| `p07475_cuchin()` | Reproduces both the special `0136 -> 0012` path and the ordinary tagged-character path |
| `p21255_begin_character_input()` | Saves the binding return and enters character conversion at `21275` |
| `p21275_encode_character()` | Selects table base `21354` and enters the common arithmetic converter |
| `p21274_decode_character()` | Selects table base `21301` for the corresponding input-side conversion |
| `p21264_convert_character()` | Uses `NTR 3`, multiply/RMR arithmetic, and a shifted table word to convert one low byte |
| `p21260_forward_converted_character()` | Transfers the result to converted-character output at `25346` with link `21261` |
| `p25346_begin_character_output()` | Saves the byte and link, then calls descriptor helper `21443` |
| `p21443_advance_descriptor()` | Inserts one byte into packed output and advances or wraps descriptor `25417` |
| `p25350_continue_character_output()` | Counts bytes, returns ordinary bytes, and calls `20245` for `0377` |
| `p20245_begin_input_continue()` | Selects the `Э74`, `Э64`, or one of two translated `Э71` entries from console state |
| `p20252_query_console()` | Executes `Э71 0146` with readiness word `20336`, then enters the status decoder |
| `p20253_resume_input_continue_status()` | Decodes the result of `Э71 0146` and selects `20261`, `20715`, or `Э71 0177` |
| `p20256_transfer_console()` | Executes the control program at `20367`, transfers its GOST-byte buffer, and enters `20257` |
| `p20257_finish_input_continue()` | Marks state `20362` after `Э71 0177` and returns through `r15` |
| `p25361_resume_character_output()` | Resets descriptor and count after the `20245` continuation |
| `p21261_return_character()` | Restores the saved `03235` link from the hardware stack |
| `p03072_resume_error_format()` | Restores the diagnostic state and selects the packed sequence at `03162` |
| `p16313_begin_character_sequence()` | Constructs packed-character cursor `6400000000003162` |
| `p21431_buffer_char()` | Extracts first heading character `052` and advances the cursor to `6000000000003162` |
| `p16321_dispatch_character()` | Tags `052` as `6400000000000052` and dispatches it through `07475` |
| `p16325_continue_character_sequence()` | Decrements the sequence count and restores the four-word caller context after the final character |

The C++ regression initializes the trace-observed source object, code,
registers, and static constants, then checks the scratch words, POP arguments,
hardware-stack save areas, descriptor dispatches, and packed-text cursor.

The nested-diagnostic branch beginning in the right half of `03024` is not yet
translated. Character forwarding now continues through `25346` and its
`21443` buffering dependency into the resumable `20245` continuation and
through the `Э71` readiness and terminal-transfer operations at `20252` and
`20256`. The in-memory console device consumes the original control words and
keeps input/output in GOST byte form; it does not replace the converted buffer
with a host character lookup. The alternate boundaries at `20321`, `20250`,
`20261`, and `20715` remain pending. The later formatting blocks after `03101`
are still pending. The syntax-error probe takes the normal path documented
here.
