#!/usr/bin/env python3
"""Self-contained language and host-service checks; no extracted image needed."""
import pathlib
import subprocess
import sys
import tempfile

EXE = pathlib.Path(sys.argv[1]).resolve()
HERE = pathlib.Path(__file__).resolve().parent
checks = 0


def run(source, expected=None, error=None, args=(), cwd=None):
    global checks
    result = subprocess.run([str(EXE), *args], input=source, text=True,
                            capture_output=True, timeout=30, cwd=cwd)
    if error is None:
        assert result.returncode == 0, (source[:120], result.stderr)
        assert not result.stderr, result.stderr
    else:
        assert result.returncode == 1 and error in result.stderr, result
    if expected is not None:
        assert result.stdout == expected, (source[:120], result.stdout, expected)
    checks += 1
    return result.stdout


with tempfile.TemporaryDirectory(prefix="poplan-standalone-test-") as temporary:
    run('2+2=>', '** 4\n', cwd=temporary)
    run('9223372036854775807, -9223372036854775808=>',
        '** 9223372036854775807, -9223372036854775808\n')
    run('ISINTEGER(2!3),ISREAL(7/2),ISREAL(2.0),1=1.0=>', '** 1, 1, 1, 0\n')
    run('9007199254740993>9007199254740992.0,9223372036854775807<9223372036854775808.0=>', '** 1, 1\n')
    run('0.1+0.2,5$-324,-0.0=>', '** 0.30000000000000004, 5e-324, -0.0\n')
    for expression in ['9223372036854775807+1', '-9223372036854775808-1',
                       '3037000500*3037000500', '2!63']:
        run(expression + '=>\n2+2=>', '** 4\n', 'integer overflow')
    run('1/0=>\n3=>', '** 3\n', 'division by zero')
    run('SQRT(-1)=>\n3=>', '** 3\n', 'domain error')
    run('VARS ABCDEF1;17->ABCDEF2;ABCDEF1=>', '** 17\n')
    run('FUNCTION MAKE X;LAMBDA;X+1->X;X END END;MAKE(40)->F;F(),F()=>', '** 41, 42\n')
    run('VARS X;99->X;FUNCTION BAD X;1/0 END;BAD(1);\nX=>', '** 1\n', 'division by zero')
    run('VARS F;FUNCTION REC N;IF N=0 THEN 0 ELSE N+REC(N-1) CLOSE END;REC(1000)=>', '** 500500\n')
    run('FUNCTION REC;REC() END;REC();\n4=>', '** 4\n', 'frame limit', args=('--max-frames', '10'))
    run('FUNCTION LOOP;L:GOTO L END;LOOP();', '', 'operation limit', args=('--max-operations', '100'))
    run('NONOP +(%1%)->P;P->FNPART(P);P();', '', 'operation limit', args=('--max-operations','100'))
    run('FUNCTION RESULTS => X Y;2->X;3->Y END;RESULTS()=>', '** 2, 3\n')
    run('FORALL I 0.0 0.5 1.0 I CLOSE=>', '** 0.0, 0.5, 1.0\n')
    run('-1->STEP;FORALL I 3 STEP 1 I CLOSE=>', '** 3, 2, 1\n')
    run('VARS OPERATION 5 PLUS;NONOP + -> NONOP PLUS;2 PLUS 3*4=>', '** 14\n')
    run('MACRO TWICE;ITEMREAD()->X;MACRESULTS([%X,",",X%]) END;TWICE 7=>', '** 7, 7\n')
    run('POPVAL([FUNCTION DOUBLE X;X*2 END;DOUBLE(21)=>;GOON]);', '** 42\n')
    run('VARS R;RECORDFNS("POINT",[0 8])->Y->X->D->C;C(10,20)->R;21->Y(R);DATALIST(R)=>', '** [10 21]\n')
    run('RECORDFNS("POINT",[1])->X->D->C;C(2);\n4=>', '** 4\n', 'component out of range')
    run('STRIPFNS("BYTE",8)->S->I;I(1)->B;256->S(1,B);\n4=>', '** 4\n', 'component out of range')
    run('STRIPFNS("BYTE",8)->S->I;I(1)->A;I(1)->B;"OCTET"->DATAWORD(A);DATAWORD(B)=>', '** OCTET\n')
    run('STRIPFNS("BYTE",8)->S->I;STRIPFNS("BYTE",8)->T->J;I(1)->A;J(1)->B;SAMEDATA(A,B)=>', '** 0\n')
    run('NEWANYARRAY([1 2],LAMBDA X;X+40 END,INITC,SUBSCC)->A;A(1),A(2)=>;99->A(2);A(2)=>', '** 41, 42\n** 99\n')
    run('VARS N;0->N;FUNCTION MYINIT X;N+1->N;INIT(X) END;NEWANYARRAY([1 2],LAMBDA X;X END,MYINIT,SUBSCR)->A;N,A(2)=>', '** 1, 2\n')
    run('LISTREAD()->L;[[1 2] CAT]\nL=>;NUMBERREAD()->X;-9223372036854775808\nX=>', '** [[1 2] CAT]\n** -9223372036854775808\n')
    run('VARS T P;\'[2.5,3]->"ABC"\'->T;0->P;FUNCTION CH;P+1->P;IF P>DATALENGTH(T) THEN TERMIN ELSE SUBSCC(P,T) CLOSE END;INCHARITEM(CH)->R;R(),R(),R(),R(),R(),R(),R(),R(),R(),R()=>', '** [, 2.5, ,, 3, ], ->, ", ABC, ", TERMIN\n')
    run('VARS TOTAL;0->TOTAL;FUNCTION SAVE X;X+TOTAL->TOTAL END;GENOUT(PR,SAVE)->P;P(12);TOTAL=>', '** 99\n')
    run("FUNCTION TAKE C;C->FIRST;CHAROUT->CUCHOUT END;TAKE->CUCHOUT;PRSTRING('ABC');FIRST=>",
        'BC** 65\n')
    run("FUNCTION TAKE C;CHAROUT->CUCHOUT;PRSTRING('x');CHAROUT(C) END;TAKE->CUCHOUT;PRSTRING('ABC');",
        'xABC')
    run("FUNCTION TAKE C;C->FIRST;CHAROUT->CUCHOUT END;GENOUT(PRSTRING,TAKE)('ABC');FIRST=>;PRSTRING('Z');",
        'BC** 65\nZ')
    run("FUNCTION TAKE C;UNDEF->CUCHOUT END;TAKE->CUCHOUT;PRSTRING('ABC');\n4=>",
        '** 4\n', 'compound value required')
    for expression, expected in [('PR(12)', '2'), ('PRINT(12)->R', '2'),
                                 ('PRREAL(1.5,1,1)', '.5'), ('PRBIN(5,3)', '01'),
                                 ('PROCT(9,3)', '11'), ('SP(3)', '  '), ('NL(3)', '\n\n'),
                                 ('12=>', '* 12\n')]:
        run('FUNCTION TAKE C;CHAROUT->CUCHOUT END;TAKE->CUCHOUT;' + expression + ';', expected)
    run('PRBIN(5,4);NL(1);PROCT(9,3);NL(1);', '0101\n011\n')
    for value in ['17', '1.5', 'NIL', '[1 2]', '"WORD"', 'CONSREF(1)',
                  'CONSPAIR(1,NIL)', 'INIT(2)', 'SIN']:
        run('PRSTRING(' + value + ');\n4=>', '** 4\n', 'PRSTRING requires a character strip')
    run('INITC(2)->S;49->SUBSCC(1,S);50->SUBSCC(2,S);PRSTRING(S);S=>',
        "12** '12'\n")
    run("PRSTRING('');PRSTRING(INITC(0));INITC(0)=>", "** ''\n")
    run('STRIPFNS("CSTRIP",8)->S->I;PRSTRING(I(1));\n4=>',
        '** 4\n', 'PRSTRING requires a character strip')
    run('CONSPAIR(1,NIL)->P;P,COPY(P),CONSREF(7),INIT(2),CONS(1,NIL),1::NIL=>',
        '** PAIR, PAIR, REF, STRIP, [1], [1]\n')
    run('CONSPAIR(1,NIL)->P;ISLIST(P),ISLINK(P),ISLIST(CONS(1,NIL)),ISLINK(CONS(1,NIL))=>',
        '** 0, 0, 1, 1\n')
    run('RECORDFNS("POINT",[0])->X->D->C;C(7)->R;R=>;"DOT"->DATAWORD(R);R,PRINT(R)=R=>',
        '** POINT\nDOT** DOT, 1\n')
    run('STRIPFNS("BYTE",8)->S->I;I(2)->R;R=>;"OCTET"->DATAWORD(R);R=>;DATALIST(R)=>',
        '** BYTE\n** OCTET\n** [0 0]\n')
    run('CONSREF(1)->R;R->CONT(R);PR(R);', 'REF')
    run('SIN,DATAWORD(SIN)=>', '** FUNCTI, FUNCTI\n')
    run('INITC(2)->S;49->SUBSCC(1,S);50->SUBSCC(2,S);FUNCTION C X;IF X=49 THEN 57->SUBSCC(2,S) CLOSE;CHAROUT(X) END;C->CUCHOUT;PRSTRING(S);',
        '19')
    run('EQUAL([1 [2 3]],[1 [2 3]])=>', '** 1\n')
    run('CONSPAIR(1,NIL)->A;A->BACK(A);CONSPAIR(1,NIL)->B;B->BACK(B);EQUAL(A,B)=>', '** 1\n')
    run('NIL="NIL",ISWORD(NIL),DATAWORD(NIL),ISLINK([1]),ISLINK(CONSREF(1))=>', '** 1, 1, WORD, 1, 0\n')
    run('0->N;FUNCTION S;N+1->N;N END;FNTOLIST(S)->L;N=>;HD(L),N,HD(L),N=>;TL(L)->T;N=>;HD(T),N=>;9->HD(T);HD(T),N=>', '** 0\n** 1, 1, 1, 1\n** 1\n** 2, 2\n** 9, 2\n')
    run('0->N;FUNCTION S;N+1->N;IF N>2 THEN TERMIN ELSE N CLOSE END;FNTOLIST(S)->L;L=>;N,NULL(TL(TL(L))),N=>', '** [1 2]\n** 3, 1, 3\n')
    run("0->N;FUNCTION S;N+1->N;CHAROUT(95);IF N>2 THEN TERMIN ELSE N CLOSE END;PR(FNTOLIST(S));",
        '[_1_ 2_]')
    run("0->N;FUNCTION S;N+1->N;CHAROUT(95);IF N>2 THEN TERMIN ELSE N CLOSE END;FNTOLIST(S)=>",
        '** [_1_ 2_]\n')
    run('0->N;FUNCTION S;STACKLENGTH()->N;TERMIN END;PRINT(FNTOLIST(S));N=>',
        '[]** [], 1\n')
    run("[1 2]->L;FUNCTION C X;IF X=91 THEN 9->HD(L) CLOSE;CHAROUT(X) END;C->CUCHOUT;PR(L);",
        '[9 2]')
    run("[1 2]->L;FUNCTION C X;IF X=49 THEN [9]->TL(L) CLOSE;CHAROUT(X) END;C->CUCHOUT;PR(L);",
        '[1 2]')
    run("[1 2]->L;FUNCTION C X;IF X=49 THEN 9->HD(TL(L)) CLOSE;CHAROUT(X) END;C->CUCHOUT;PR(L);",
        '[1 9]')
    run("[1 2]->L;FUNCTION C X;IF X=32 THEN 9->HD(TL(L)) CLOSE;CHAROUT(X) END;C->CUCHOUT;PR(L);",
        '[1 9]')
    run("'12'->S;FUNCTION C X;IF X=49 THEN 57->SUBSCC(2,S) CLOSE;CHAROUT(X) END;C->CUCHOUT;PRSTRING(S);",
        '19')
    run("'12'->S;FUNCTION C X;IF X=39 THEN 57->SUBSCC(1,S) CLOSE;CHAROUT(X) END;C->CUCHOUT;PR(S);",
        "'92'")
    run("0->N;FUNCTION S;N+1->N;IF N=1 THEN 1 ELSE 1/0 CLOSE END;PR(FNTOLIST(S));\n4=>",
        '[1** 4\n', 'division by zero')
    run("[1]->L;FUNCTION C X;CHAROUT(X);IF X=49 THEN 1/0 CLOSE END;C->CUCHOUT;PR(L);\n4=>",
        '[1** 4\n', 'division by zero')
    run("[1]->L;L->TL(L);PR(L);", '[1 <cycle>]')
    run("[1]->L;L->HD(L);PR(L);", '[<cycle>]')
    run("[1]->L;[%L,L%]=>", '** [[1] [1]]\n')
    run("FUNCTION C X;IF X=91 THEN CHAROUT->CUCHOUT;PR([7]);C->CUCHOUT CLOSE;CHAROUT(X) END;C->CUCHOUT;PR([1]);",
        '[7][1]')
    streamed = run('FUNCTION S;1 END;PR(FNTOLIST(S));', error='operation limit',
                   args=('--max-operations', '200'))
    assert streamed.startswith('[1') and not streamed.endswith(']'), streamed
    run('0->N;FUNCTION S;N+1->N;PR(N);IF N>2 THEN TERMIN ELSE N CLOSE END;APPLIST(FNTOLIST(S),LAMBDA X;PR(X*10) END);', '1102203')
    run('0->N;FUNCTION S;N+1->N;IF N>2 THEN TERMIN ELSE N CLOSE END;EQUAL(FNTOLIST(S),[1 2])=>', '** 1\n')
    run('NEWARRAY([1 2 1 3],LAMBDA X Y;10*X+Y END)->A;[1 2 1 2]->BOUNDSLIST(A);A(2,1),BOUNDSLIST(A)=>', '** 13, [1 2 1 2]\n')
    run('[2 3]->B;[1]<>B->A;9->HD(B);A,TL(A)=B=>', '** [1 9 3], 1\n')
    run('DATALIST("CAT"),DATALIST(NONOP +(%1%))=>', '** [67 65 84], [1]\n')
    run("DATALENGTH('λёж'),DATALENGTH(INIT(0)),DATALENGTH(INITC(4)),LENGTH(NIL),LENGTH([1 [2 3]])=>",
        '** 3, 0, 4, 0, 2\n')
    run('STRIPFNS("BYTE",8)->S->I;DATALENGTH(I(3))=>', '** 3\n')
    run('0->N;FUNCTION S;N+1->N;IF N>3 THEN TERMIN ELSE N CLOSE END;FNTOLIST(S)->L;LENGTH(L),N,LENGTH(L),N=>',
        '** 3, 4, 3, 4\n')
    for value in ['NIL', '[1 2]', '"WORD"', 'CONSREF(1)', 'SIN', '1', '1.0']:
        run('DATALENGTH(' + value + ');\n4=>', '** 4\n', 'DATALENGTH requires a strip')
    run('RECORDFNS("POINT",[0])->X->D->C;DATALENGTH(C(1));\n4=>',
        '** 4\n', 'DATALENGTH requires a strip')
    run('CONSPAIR(1,NIL)->L;L->BACK(L);LENGTH(L);\n4=>',
        '** 4\n', 'finite proper list required')
    run('NONOP +(%1%)->P;APPDATA(P,IDENTFN)=>;APPDATA(NONOP +(% %),IDENTFN);STACKLENGTH()=>',
        '** 1\n** 0\n')
    run('NONOP +(%1,2%)->P;APPDATA(P,LAMBDA X;9->FROZVAL(2,P);X END)=>;DATALIST(P)=>',
        '** 1, 2\n** [1 9]\n')
    run("APPDATA(\"CAT\",IDENTFN)=>;APPDATA('λёж',IDENTFN)=>;APPDATA(CONSREF(7),IDENTFN)=>",
        '** 67, 65, 84\n** 955, 1105, 1078\n** 7\n')
    run('COPY("CAT");\n"CAT"="CAT"=>', '** 1\n', 'COPY cannot copy an interned word')
    run('[1 2]->A;COPY(A)->B;9->HD(A);8->HD(TL(A));A,B,A=B,TL(A)=TL(B)=>',
        '** [9 8], [1 8], 0, 1\n')
    run('PRREAL(2.5,4,2);PRREAL(2.5,"EXP",3);', '   2.502.50e+00')
    run('UPDATER(SIN)=UNDEF,UPDATER(CONT)=UPDATER(CONT)=>;CONSREF(1)->R;UPDATER(CONT)(9,R);CONT(R)=>', '** 1, 1\n** 9\n')
    run('FUNCTION F X;X END;17->UPDATER(F);UPDATER(F)=>;UNDEF->UPDATER(F);UPDATER(F)=UNDEF=>',
        '** 17\n** 1\n')
    run('FUNCTION F X;X END;17->UPDATER(F);9->F(1);\n4=>',
        '** 4\n', 'function has no callable updater')
    run('CONSREF(1)->R;CONT(%R%)->P;UPDATER(P)->U;UNDEF->UPDATER(P);UPDATER(P)=UNDEF=>;9->P();\nCONT(R)=>',
        '** 1\n** 1\n', 'function has no callable updater')
    run('FUNCTION F X;X END;UNDEF->UPDATER(F);LAMBDA V X;V+X->RESULT END->UPDATER(F);7->F(3);RESULT=>',
        '** 10\n')
    run('UNDEF->UPDATER(CONT);\n4=>', '** 4\n', 'protected function')
    run('COPY(NONOP +(%1%));\n4=>', '** 4\n', 'COPY cannot copy a function')
    run('CONSREF(1)->A;CONSREF(2)->B;CONT(%A%)->P;B->FROZVAL(1,P);9->P();CONT(A),CONT(B),P()=>',
        '** 9, 2, 2\n')
    run('CONSREF(1)->A;CONSREF(2)->B;CONT(%A%)->P;UPDATER(P)->U;B->FROZVAL(1,U);U(9);CONT(A),CONT(B),P()=>',
        '** 1, 9, 1\n')
    run('CONSREF(1)->A;CONT(%A%)->P;SIN->FNPART(P);9->P();CONT(A)=>', '** 9\n')
    run('IDENTFN FNCOMP CONT->F;UPDATER(F)=UNDEF=>;CONSREF(1)->R;9->F(R);\nCONT(R)=>',
        '** 1\n** 1\n', 'function has no callable updater')
    run('IDENTFN FNCOMP CONT->F;LAMBDA V R;V->CONT(R) END->UPDATER(F);CONSREF(1)->R;9->F(R);F(R)=>',
        '** 9\n')
    for expression in ['SIN(0)', 'IDENTFN(1)', "PR('must not print')",
                       'CONSREF(1)', 'LENGTH([1 2])', 'DATALIST([1 2])']:
        run('9->' + expression + ';\n4=>', '** 4\n', 'function has no callable updater')
    run('CONSREF(1)->R;9->DESTREF(R);\nCONT(R)=>',
        '** 1\n', 'function has no callable updater')
    run('RECORDFNS("POINT",[0])->X->D->C;C(1)->R;9->D(R);\nX(R)=>',
        '** 1\n', 'function has no callable updater')
    run("FUNCTION F X;PR('must not run');X END;9->F(1);\n4=>",
        '** 4\n', 'function has no callable updater')
    run("PR('must not print'),17->SIN;\nSIN(0)=>", '** 0.0\n', 'protected identifier')
    run('FORALL SIN 1 1 2 SIN CLOSE=>\nSIN(0)=>', '** 0.0\n', 'protected identifier')
    run('VARS SIN;\nSIN(0)=>', '** 0.0\n', 'declaration of protected identifier')
    run('17->VALOF("SIN");\nSIN(0)=>', '** 0.0\n', 'protected identifier')
    run('REFOF("SIN");\nSIN(0)=>', '** 0.0\n', 'REFOF of protected identifier')
    run('FUNCTION F SIN;17->SIN;SIN END;F(2),SIN(0)=>', '** 17, 0.0\n')
    run('FUNCTION F SIN;LAMBDA;SIN+1->SIN;SIN END END;F(2)->G;G(),G(),SIN(0)=>',
        '** 3, 4, 0.0\n')
    run('FUNCTION OLD;SIN(0) END;CANCEL SIN;VARS SIN;17->SIN;SIN,OLD()=>', '** 17, 0.0\n')
    run('1->FNPROPS(SIN);\n4=>', '** 4\n', 'protected function')
    run('[1 =>;GOON]->L;POPVAL(L);L=>;POPVAL(L);', '** 1\n** [1 => ; GOON]\n** 1\n')
    run('POPVAL([2 =>;GOON;99 =>]);4=>', '** 2\n** 4\n')
    run('NTERM=UNDEF=>', '** 1\n')
    run('JUMPOUT();\n4=>', '** 4\n', 'monitor handoff')
    run('SECTION;\n4=>', '** 4\n', 'unsupported standalone syntax')
    run('FUNCTION F;CANCEL X;END;\n4=>', '** 4\n', 'CANCEL is not allowed')
    file = pathlib.Path(temporary) / 'unicode.txt'
    run(f"POPMESS([DO '{file}'])->F;F(955);F(TERMIN);POPMESS([DI '{file}'])->G;G(),G()=>", '** 955, TERMIN\n')
    assert file.read_text() == 'λ'
    run('POPMESS([MI 1]);\n2+2=>', '** 4\n', 'unsupported historical')
    run('CODIPC(65);', '', 'historical character-code conversion')
    lib = pathlib.Path(temporary) / 'POPLIB'
    lib.mkdir()
    (lib / 'TEST.pop2').write_text('FUNCTION TWICE X;X*2 END;')
    run('LIBRARY("POPLIB","TEST");TWICE(21)=>', '** 42\n', args=('--library-path', temporary))
    run((HERE / 'fixtures/man-or-boy.pop2').read_text(), '** -67\n', cwd=temporary)
    for fixture in ['language-coverage', 'standard-functions', 'native-functions', 'native-nested']:
        source = (HERE / 'fixtures' / (fixture + '.pop2')).read_text()
        expected = (HERE / 'fixtures' / (fixture + '.expected')).read_text()
        run(source, expected, cwd=temporary)
    quine = run((HERE / 'fixtures/quine.pop2').read_text(), cwd=temporary)
    # The output is executable source that reproduces itself byte for byte.
    run(quine, quine, cwd=temporary)
    examples = run((HERE / 'fixtures/zone1224.pop2').read_text(), cwd=temporary)
    assert '** 74\n' in examples and '** 1.0, 1.4142135623730951' in examples
    for library in ['DEBUG', 'MEMOFN', 'EXAMPL', 'FOURS']:
        run(f'LIBRARY("POPLIB","{library}");', args=('--library-path', str(HERE.parent / 'lib')))
    game = run((HERE / 'fixtures/ttt-fixed.pop2').read_text(), cwd=temporary)
    assert 'MY MOVE003' in game and 'MY MOVE333' in game
    assert game.endswith('BACK TO POP2 THEN')
    symbols = subprocess.run(['nm', '-C', str(EXE)], text=True, capture_output=True, check=True).stdout
    assert all(forbidden not in symbols for forbidden in ['Machine::', 'Word48', 'Besm', 'NativeRuntime::'])
print(f'{checks} standalone conformance checks passed')
