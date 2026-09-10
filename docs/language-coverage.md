# POP-2 Reference-Language Coverage

The reference used for this audit is R. M. Burstall and R. J. Popplestone,
*POP-2 Reference Manual*, printed pages 207-246. The source copy inspected was
`/mnt/c/Users/leob/OneDrive/Documents/POP-2-Reference-Manual.pdf`. The
reference grammar is compared with the static dictionary in
`build/poplan.bin`; the image, rather than a reconstructed C++ name table,
remains the authority for what POPLAN recognizes.

## Syntax words

The reference manual uses 21 alphabetic syntax words. POPLAN recognizes 20
of them. Its name words contain six bytes, so longer input identifiers resolve
through their first six characters.

| Reference spelling | POPLAN name | Record | Status |
|---|---|---:|---|
| `AND` | `AND` | `02314` | recognized |
| `CANCEL` | `CANCEL` | `02320` | recognized |
| `CLOSE` | `CLOSE` | `02324` | recognized |
| `COMMENT` | `COMMEN` | `02460` | recognized by six-character prefix |
| `ELSE` | `ELSE` | `02330` | recognized |
| `ELSEIF` | `ELSEIF` | `02334` | recognized |
| `END` | `END` | `02340` | recognized |
| `EXIT` | `EXIT` | `02350` | recognized |
| `FUNCTION` | `FUNCTI` | `02354` | recognized by six-character prefix |
| `GOON` | `GOON` | `02360` | recognized |
| `GOTO` | `GOTO` | `02364` | recognized |
| `IF` | `IF` | `02370` | recognized |
| `LAMBDA` | `LAMBDA` | `02374` | recognized |
| `MACRO` | `MACRO` | `02404` | recognized |
| `NONOP` | `NONOP` | `02414` | recognized |
| `OPERATION` | `OPERAT` | `02420` | recognized by six-character prefix |
| `OR` | `OR` | `02424` | recognized |
| `RETURN` | `RETURN` | `02430` | recognized |
| `ROUTINE` | — | — | not resident; the reference synonym is unsupported |
| `THEN` | `THEN` | `02444` | recognized |
| `VARS` | `VARS` | `02450` | recognized |

POPLAN also has five alphabetic syntax words outside this reference manual:

| POPLAN name | Record |
|---|---:|
| `ENDSEC` | `02344` |
| `LOOPIF` | `02400` |
| `NONMAC` | `02410` |
| `SECTIO` (`SECTION`) | `02434` |
| `SWITCH` | `02440` |

Punctuation records such as `(`, `[%`, `->`, and `=>` are grammar tokens but
are not alphabetic keywords and are therefore not included in either table.

## Standard identifiers and dialect differences

Names such as `RECORDFNS`, `NEWARRAY`, `DATALIST`, and `MACRESULTS` are
ordinary standard identifiers, not syntax words. The resident dictionary uses
the corresponding six-byte names `RECORD`, `NEWARR`, `DATALI`, and `MACRES`.
The language test uses the full reference spellings and thereby verifies the
same prefix lookup used by normal programs.

The reference manual is not an exact specification of this later BESM-6
implementation:

- It says that eight identifier characters are significant; POPLAN stores six.
- Its `ROUTINE` synonym is absent from the resident dictionary.
- Its function-restricted `VARS FUNCTION(...)` form is not accepted by this
  compiler, although `FUNCTION` definitions are accepted.
- Resident `RECORDFNS` and `STRIPFNS` take two arguments, omitting the
  storage-estimate argument in the reference manual.
- Several low-level reference arithmetic function names are not resident;
  POPLAN supplies the generic arithmetic operations instead.

These differences are recorded as findings, not silently treated as language
coverage.

## Executable coverage

`tests/inputs/language-coverage.pop2` exercises every supported alphabetic
reference syntax word, as well as literals, expressions, assignment, partial
application, list construction, records, strips, arrays, macros, and `POPVAL`.
Its normalized output must match the historical image under `dispak`, the
hybrid C++ machine, and instruction-only C++ execution.

Machine-code statements and external file/device operations are excluded:
the reference manual makes those facilities implementation-dependent, and
the emulator has separate extracode and library tests for them.
