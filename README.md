# re-poplan

Disassembling the BESM-6 ["POPLAN" system](https://en.wikipedia.org/wiki/POP-2#History) with AI agents.

The author of the system was [Andrei Borisovich Khodulev](https://www.keldysh.ru/pages/cgraph/people/khodulev.htm) (1953-1999)

To run POPLAN:

 - install https://github.com/besm6/dispak

 - for a fully interactive session: dispak --bootstrap poplan.b6
   (to exit, type ^)

 - to enter a POP-2 source, then to switch to interactive mode:
         cat file.pop2 /dev/tty | dispak --bootstrap poplan.b6
   (to exit, type ^)

 - to run a POP-2 program non-interactively:
         echo '^' | cat file.pop2 - | dispak --bootstrap poplan.b6

The messages and diagnostics are in Russian (UTF-8); the non-abbreviated ones could be Google-translated.

A brief primer on the batch language:

USER 419900^    - user acct number
TELE^           - interactive session
DISK 42(1)^     - volume 1 attached to handle 42 (octal) read-only
BEG 75777^      - execution entry point in 48-bit words (octal), out of the 15-bit address space
E               - end of resource section
B 75777         - start address for the data section
K 00 170 6000   - instruction format (K) I/O system call, parameters @76000
C 0010 3700 0042 1200 - read (1) into page 37 (starting @76000) from handle 42, block 1200
E               - end of data section
FINISH          - end of batch

The syscall returns control @76000 where the binary read from the disk will be.

All line breaks are either explicit (^) or not needed, thus the whole batch can be compressed into a "koan".

