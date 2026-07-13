# Subroutines observed in `trace.quine`

Source trace: `trace.quine`, from `dispak -t -t poplan.b6 < quine.pop2`.

This is a dynamic list: "leaf" means that no nested `vjm` was observed while
that subroutine invocation was active in this trace. It is not a proof that the
routine is leaf for all inputs.

The normal link register is `r15`. The only observed target called with `r14`
is `11673`.

## Known or likely functions

These descriptions are trace-derived. 

| Address | Description | Evidence |
|---:|---|---|
| 05230 | Main initialization / cold-start routine. Saves initial registers, sets up the POPLAN runtime areas, initializes stack pointers, probes the console/batch environment, prints the greeting, then enters the POP evaluator. | First call from `01001`; saves `r1`, `r2`, `r3`, `r4`, `r15` at `05400..05404`, later sets stack base `r6=70000`, calls setup/output/input routines. |
| 20144 | Runtime/supervisor setup helper. | Called once from `05265` after constructing word `0002056650067000`; executes `*50` and `*67` calls through the `20564` table. |
| 20170 | Primary line-input routine. Checks input-ready/state word `20377`; if zero, branches to `20321:*74`. In interactive mode it prompts and reads via `*71`, then exposes packed input text through the buffer at `20400`. | Without `TELE^`, `20170: xta 207(10)` reads `20377=0` and halts at `20321`. With `TELE^`, `20377=12`, then it reads from console and later `21431` consumes text at `20400`. |
| 20245 | Secondary / continuation line-input routine. Reads or refills input once initial input state exists. | Called from `25360`; tests `20377`, `20375`, and `20362`, and in interactive trace calls `*71 177(10)` to read another line. |
| 20263 | Console/batch detection and I/O-state initialization. Probes `*71`; nonzero result selects interactive console constants, zero result selects batch/printer constants. Initializes state words around `20362..20377` and message descriptors at `25415..25417`. | With `TELE^`, `*71` result sets `20377=12`; without it, `*71` result is zero and `20377` stays zero. |
| 20456 | Numeric/time/message formatting routine used during startup. | Called from `05336`; saves `r15`/`r1`, calls `*53 10`, then repeatedly calls `25641`/`25660` to build encoded output words. |
| 20660 | Memory-size or upper-bound helper for runtime initialization. | Called from `05254`; uses constant at `0016760000033064`, computes and stores through an address in `r16`; returns a value like `0003271400000000`. |
| 20673 | Trivial return helper / no-op routine. | Called at `05335`; immediately executes `uj (15)`. |
| 20674 | Common message-output routine. Prints to console when `20377` is nonzero, otherwise to printer. | With `TELE^`, `20674` uses `*71 14(10)`, `*71 20(10)`, `*71 14(10)`; without, the same routine sees `20377=0` and uses `20705:*64 16(10)`. |
| 21251 | Character or byte extraction wrapper. Saves caller return in the `r17` frame, calls `25370`, then postprocesses through a table at `21301` and returns the extracted byte/character in `acc`. | Called from `07472`; wraps `25370` and later computes with `a*x`, `m+j`, `asn`, and `aax 0377`. |
| 21431 | Packed text-buffer cursor advance / character fetch helper. Updates a descriptor pointed to by `r16`; returns the low byte/character in `acc`. | Called with `r16=25413`, whose descriptor points at `20400`; reads `(r14)=20400`, advances descriptor, returns values such as `0100` or `0115`. |
| 21443 | Packed descriptor advance helper. Similar to `21431`, but used where only the descriptor update/count side effect is needed. | Called from `16470` and `25347`; operates on descriptor pointer in `r16`, updates `(r16)` and nearby packed-word state, returns through conditional `u1a (15)`. |
| 25370 | Input-token source wrapper. Calls `20170` when the current input buffer is empty, then sets up descriptor state at `25413..25420`. | Called from `21252`; invokes `20170` at `25372`, stores descriptor `6400000000020400` at `25413`, then calls `21431`. |
| 25641 | Octal/decimal number formatting driver. | Called from `20461`; does multiply/divide style conversion and calls `25660` three times to assemble digits/fields. |
| 25660 | Digit/field packing helper for `25641`. | Leaf routine called from `25641`; combines arithmetic result with a word in the `r17` frame and returns through `uj (15)`. |
| 2750 | POP value dispatch / evaluator trampoline. Decodes the high tag bits of a POP value in `acc`, then dispatches through the address held in the value or runtime table. | Very frequent; callers pass tagged values such as `660...`; it stores temporary words at `03272/03273`, then often jumps via `03261..03264` into the decoded target. |
| 3275 | Push accumulator to POP data stack. | Decrements `r6`, stores `acc` at `(r6)`, returns through `r15`. |
| 3277 | Pop accumulator from POP data stack. | Loads `acc` from `(r6)`, increments `r6`, returns through `r15`. |
| 3303 | Store top POP stack item into address held by `r16`. | Executes `xts (6)`, increments `r6`, then `stx (16)`. Used from `16465` with `r16` pointing into an object/frame. |
| 3413 | Numeric arithmetic helper, likely decrement/integer update. | Called 279 times from `11707`; stores an operand in the `r17` frame, uses `ntr 6`, floating/integer arithmetic, and returns a modified small integer-like value. |

## Important state words and buffers

| Address | Meaning | Evidence |
|---:|---|---|
| 20362 | Input-line available/consumed flag. | Set by `20170`/`20245` after console reads. |
| 20375 | Output/status flag checked after message output. | Read by `20674` after `*71` output sequence. |
| 20377 | Console/input availability flag. Nonzero means the console input path exists; zero makes batch line input terminate at `20321:*74`. | Set to `12` in interactive startup; remains zero in the batch trace. |
| 20400 | Packed input text buffer. | Interactive input path stores/uses descriptors pointing at `20400`; `21431` reads packed words from this buffer. |
| 25413 | Current packed-input descriptor used by `21431`. | `25370` stores `6400000000020400` here before `21431` reads from `20400`. |
| 25415..25417 | Message/output descriptor scratch area initialized by `20263`. | Startup writes prompt/greeting descriptor words here before output routines consume them. |

| Address | Calls | Link reg(s) | Observed kind | Main observed callers |
|---:|---:|---|---|---|
| 1004 | 1 | r15 | nonleaf | 01002:1 |
| 1107 | 42 | r15 | nonleaf | 16417:42 |
| 11464 | 10 | r15 | nonleaf | 05162:8, 11664:2 |
| 11541 | 279 | r15 | leaf | 11702:279 |
| 1167 | 1 | r15 | nonleaf | 06140:1 |
| 11673 | 279 | r14 | nonleaf | 11717:186, 11726:93 |
| 11717 | 186 | r15 | nonleaf | 11755:186 |
| 16005 | 1 | r15 | nonleaf | 05313:1 |
| 16254 | 5 | r15 | leaf | 17677:5 |
| 16341 | 46 | r15 | nonleaf | 20667:46 |
| 16421 | 121 | r15 | leaf | 16347:64, 16412:54, 16743:3 |
| 16457 | 195 | r15 | nonleaf | 16506:130, 16346:65 |
| 16477 | 27 | r15 | nonleaf | 16405:20, 16514:5, 16616:1, 16745:1 |
| 16505 | 96 | r15 | nonleaf | 16531:95, 16605:1 |
| 16605 | 1 | r15 | nonleaf | 16660:1 |
| 16742 | 3 | r15 | nonleaf | 16656:2, 16746:1 |
| 17013 | 45 | r15 | nonleaf | 06535:45 |
| 17045 | 46 | r15 | nonleaf | 06533:46 |
| 17070 | 5 | r15 | nonleaf | 03466:2, 25445:2, 17163:1 |
| 17242 | 33 | r15 | leaf | 03544:18, 03624:13, 04166:2 |
| 17253 | 32 | r15 | leaf | 04714:18, 04614:14 |
| 17337 | 63 | r15 | leaf | 03725:13, 03707:5, 04005:5, 04007:5, 04017:5, 04025:5, 04044:5, 04053:5 |
| 17340 | 65 | r15 | leaf | 04540:15, 04075:10, 04077:10, 04100:10, 04101:10, 04102:10 |
| 17341 | 4 | r15 | leaf | 17205:2, 17211:2 |
| 17342 | 4 | r15 | leaf | 17525:2, 17530:2 |
| 17417 | 4 | r15 | nonleaf | 03740:4 |
| 17472 | 2 | r15 | nonleaf | 25444:2 |
| 17571 | 3 | r15 | nonleaf | 17451:2, 17157:1 |
| 17602 | 5 | r15 | nonleaf | 17574:3, 17534:2 |
| 17614 | 6 | r15 | leaf | 05034:4, 25542:1, 25543:1 |
| 17624 | 1 | r15 | nonleaf | 05016:1 |
| 17762 | 11 | r15 | nonleaf | 17632:6, 17743:5 |
| 17774 | 6 | r15 | nonleaf | 17752:6 |
| 20110 | 6 | r15 | leaf | 65567:1, 65577:1, 65603:1, 65607:1, 65613:1, 65617:1 |
| 20124 | 5 | r15 | nonleaf | 65575:1, 65601:1, 65605:1, 65611:1, 65615:1 |
| 20144 | 1 | r15 | leaf | 05265:1 |
| 20170 | 5 | r15 | nonleaf | 25372:5 |
| 20245 | 5 | r15 | leaf | 25360:5 |
| 20263 | 1 | r15 | leaf | 05237:1 |
| 20456 | 1 | r15 | nonleaf | 05336:1 |
| 20660 | 1 | r15 | leaf | 05254:1 |
| 20673 | 1 | r15 | leaf | 05335:1 |
| 21107 | 1 | r15 | leaf | 16025:1 |
| 21251 | 193 | r15 | nonleaf | 07472:193 |
| 21275 | 191 | r15 | leaf | 21257:191 |
| 21431 | 192 | r15 | leaf | 25376:192 |
| 21443 | 386 | r15 | leaf | 16470:194, 25347:192 |
| 21464 | 91 | r15 | nonleaf | 17046:46, 17014:45 |
| 25346 | 191 | r15 | nonleaf | 21260:191 |
| 25356 | 1 | r15 | nonleaf | 20174:1 |
| 25370 | 193 | r15 | nonleaf | 21252:193 |
| 25427 | 1 | r15 | leaf | 32537:1 |
| 25556 | 1 | r15 | leaf | 17155:1 |
| 25641 | 1 | r15 | nonleaf | 20461:1 |
| 25660 | 3 | r15 | leaf | 25646:1, 25652:1, 25654:1 |
| 2750 | 616 | r15 | nonleaf | 10010:186, 10012:186, 16501:157, 21500:46, 16464:36, 01025:3, 07765:2 |
| 2764 | 2 | r15 | nonleaf | 07767:2 |
| 2767 | 8 | r15 | nonleaf | 16541:2, 65576:1, 65602:1, 65606:1, 65612:1, 65616:1, 65764:1 |
| 2770 | 93 | r15 | nonleaf | 16547:93 |
| 3275 | 666 | r15 | leaf | 10005:186, 10006:186, 16532:95, 16545:93, 16546:93, 20134:5, 07763:2, 07766:2 |
| 3277 | 1056 | r15 | leaf | 11674:279, 11700:279, 07475:191, 16502:157, 11730:93, 21501:45, 07774:4, 07667:2 |
| 3301 | 1 | r15 | leaf | 65557:1 |
| 3303 | 36 | r15 | leaf | 16465:35, 65560:1 |
| 3330 | 2 | r15 | leaf | 07703:2 |
| 3413 | 279 | r15 | leaf | 11707:279 |
| 3506 | 6 | r15 | leaf | 03523:3, 03525:3 |
| 3516 | 79 | r15 | leaf | 06544:45, 03533:31, 03527:3 |
| 3531 | 9 | r15 | nonleaf | 05033:4, 05150:4, 17215:1 |
| 3536 | 27 | r15 | nonleaf | 04667:14, 04601:13 |
| 3702 | 5 | r15 | nonleaf | 04006:5 |
| 3716 | 10 | r15 | leaf | 04023:5, 04047:5 |
| 3724 | 13 | r15 | nonleaf | 03630:13 |
| 3736 | 4 | r15 | nonleaf | 03547:4 |
| 4074 | 10 | r15 | nonleaf | 04001:5, 04114:5 |
| 4161 | 2 | r15 | nonleaf | 04607:2 |
| 4322 | 26 | r15 | leaf | 03614:13, 04570:9, 04163:2, 03561:1, 03602:1 |
| 4426 | 4 | r15 | leaf | 03545:4 |
| 4447 | 12 | r15 | nonleaf | 03567:4, 04172:2, 17452:2, 03607:1, 17160:1, 17162:1, 17424:1 |
| 4467 | 26 | r15 | nonleaf | 04560:9, 04002:5, 01011:4, 03741:3, 04165:2, 04606:2, 03601:1 |
| 4536 | 3 | r15 | nonleaf | 01015:3 |
| 4665 | 13 | r15 | nonleaf | 04604:13 |
| 4675 | 6 | r15 | nonleaf | 04003:5, 32555:1 |
| 5007 | 4 | r15 | nonleaf | 01024:3, 17200:1 |
| 5160 | 8 | r15 | nonleaf | 05025:4, 05027:4 |
| 5207 | 46 | r15 | leaf | 21475:46 |
| 5211 | 91 | r15 | leaf | 21472:91 |
| 5213 | 5 | r15 | nonleaf | 17605:5 |
| 5215 | 63 | r15 | nonleaf | 21507:45, 04454:17, 32563:1 |
| 5230 | 1 | r15 | nonleaf | 01001:1 |
| 5405 | 4 | r15 | leaf | 01010:4 |
| 5410 | 4 | r15 | leaf | 01023:3, 32564:1 |
| 5430 | 97 | r15 | nonleaf | 05220:68, 17763:11, 11470:5, 05021:4, 05037:4, 01137:3, 01150:1, 16021:1 |
| 5447 | 97 | r15 | leaf | 05432:97 |
| 6134 | 1 | r15 | nonleaf | 17423:1 |
| 6343 | 46 | r15 | nonleaf | 04470:41, 17071:5 |
| 6424 | 47 | r15 | leaf | 06353:42, 25423:5 |
| 6526 | 46 | r15 | nonleaf | 06345:46 |
| 7673 | 2 | r15 | nonleaf | 07671:2 |

## `r14` link-register calls

Only one subroutine target is observed with `vjm ... (14)`:

| Address | Calls | Callers | Notes |
|---:|---:|---|---|
| 11673 | 279 | 11717:186, 11726:93 | Nonleaf in this trace; it is never observed called with `r15`. |
