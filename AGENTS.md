This project is to disassemble or decompile the BESM-6 POPLAN (POP-2) interpreter.

The BESM-6 architecture and instruction set opcode notation is described in https://raw.githubusercontent.com/besm6/c-compiler/refs/heads/main/docs/Besm6_Instruction_Set.md

The extracode 070 (\*70) is the disk I/O system call. 071 (\*71) is the console I/O.

To run experiments: `dispak --bootstrap poplan.b6 < input_file > output`

To trace: `dispak --bootstrap -t -t poplan.b6 < input_file > output 2> trace`


