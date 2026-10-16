#include "pop/runtime.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace {
void check(bool ok, const std::string &message) {
  if (!ok)
    throw std::runtime_error(message);
}
struct Session {
  std::istringstream input;
  std::ostringstream output, errors;
  pop::Runtime runtime{input, output, errors};
};
void splits(const std::string &source, const std::string &expected) {
  for (std::size_t split = 0; split <= source.size(); ++split) {
    Session s;
    auto first = s.runtime.feed("split", source.substr(0, split), false);
    check(first.state == pop::Runtime::SessionState::need_input,
          "prefix completed before EOF");
    s.runtime.collect();
    auto result = s.runtime.feed("split", source.substr(split), true);
    check(result.state == pop::Runtime::SessionState::finished,
          "split " + std::to_string(split) + ": " + s.errors.str());
    check(s.output.str() == expected,
          "split " + std::to_string(split) + " output: " + s.output.str());
  }
  Session bytewise;
  for (auto c : source) {
    auto result = bytewise.runtime.feed("bytes", std::string(1, c));
    check(result.state == pop::Runtime::SessionState::need_input,
          "bytewise failed: " + bytewise.errors.str());
    bytewise.runtime.collect();
  }
  check(bytewise.runtime.feed("bytes", "", true).state ==
            pop::Runtime::SessionState::finished,
        bytewise.errors.str());
  check(bytewise.output.str() == expected,
        "bytewise output mismatch: " + bytewise.output.str());
}
} // namespace
int main() {
  try {
    Session immediate;
    immediate.runtime.feed("live", "2+2=>");
    check(immediate.output.str() == "** 4\n", "complete command waits for EOF");
    immediate.runtime.feed("live", "FUNCTION F X;X+", false);
    check(immediate.output.str() == "** 4\n", "incomplete function ran");
    immediate.runtime.feed("live", "1 END;F(8)=>", true);
    check(immediate.output.str() == "** 4\n** 9\n", immediate.errors.str());
    splits("2+3*4=>\n", "** 14\n");
    splits("FUNCTION F X;IF X THEN X+1 ELSE 8 CLOSE END;F(3),F(0)=>",
           "** 4, 8\n");
    splits("'λёж'=>\n[1 [2 3] CAT]=>\n", "** 'λёж'\n** [1 [2 3] CAT]\n");
    splits("VARS N;0->N;MACRO "
           "M;N+1->N;PR('M');ITEMREAD()->X;MACRESULTS([%X,\",\",X%]) END;M 7=> "
           "N=>",
           "'M'** 7, 7\n** 1\n");
    splits("FUNCTION F X;VARS Y;X->Y;LOOPIF Y>0 THEN Y-1->Y CLOSE;Y END;F(3)=>",
           "** 0\n");
    splits("FUNCTION F X;LAMBDA;X+1->X;X END END;F(1)->G;G(),G()=>",
           "** 2, 3\n");
    splits("POPVAL([2+3 =>;GOON]);4=>", "** 5\n** 4\n");
    splits("LISTREAD()->L; [[1 2] CAT]\nL=>", "** [[1 2] CAT]\n");
    splits("NUMBERREAD()->N; -9223372036854775808\nN=>",
           "** -9223372036854775808\n");
    splits("MACRO M; MACRESULTS([17]) END;ITEMREAD()->X; M\nX=>", "** 17\n");
    splits("'abc'->S;POPVAL([%S,\"->\",\"T\",\";\",\"GOON\"%]);S=T=>",
           "** 1\n");
    splits("(LAMBDA X;X+1 END)(4)=>", "** 5\n");
    splits("FUNCTION F X;LAMBDA Y;X+Y END END;F(3)(4)=>", "** 7\n");
    splits("FUNCTION TAKE C;C->FIRST;CHAROUT->CUCHOUT END;TAKE->CUCHOUT;"
           "PRSTRING('λёж');FIRST=>", "ёж** 955\n");
    splits("FUNCTION TAKE C;CHAROUT(C);ITEMREAD()->X;CHAROUT->CUCHOUT END;"
           "TAKE->CUCHOUT;PRSTRING('λё');17\nX=>", "λё** 17\n");
    splits("FUNCTION S;CHAROUT(95);ITEMREAD()->V;IF V=\"DONE\" THEN TERMIN "
           "ELSE V CLOSE END;PR(FNTOLIST(S));17 DONE\n", "[_17_]");
    splits("[1 2]->L;FUNCTION C X;IF X=49 THEN NIL->TL(L);UNDEF->L;"
           "ITEMREAD()->Y CLOSE;CHAROUT(X) END;C->CUCHOUT;PR(L);17\n"
           "CHAROUT->CUCHOUT;Y=>", "[1 2]** 17\n");
    splits("'12'->S;FUNCTION C X;IF X=49 THEN ITEMREAD()->Y;Y->SUBSCC(2,S) "
           "CLOSE;CHAROUT(X) END;C->CUCHOUT;PRSTRING(S);57\n", "19");
    splits("INITC(2)->S;955->SUBSCC(1,S);1078->SUBSCC(2,S);FUNCTION C X;"
           "IF X=955 THEN ITEMREAD()->Y;Y->SUBSCC(2,S) CLOSE;CHAROUT(X) END;"
           "C->CUCHOUT;PRSTRING(S);1105\n", "λё");
    splits("RECORDFNS(\"POINT\",[0])->X->D->C;C(7)->R;PR(R);"
           "\"DOT\"->DATAWORD(R);PR(R);", "POINTDOT");
    Session printer_type;
    printer_type.runtime.feed("printer-type", "0->N;FUNCTION S;N+1->N;"
                              "ITEMREAD() END;PRSTRING(FNTOLIST(S));");
    printer_type.runtime.collect();
    printer_type.runtime.feed("printer-type", "N=>", true);
    check(printer_type.runtime.diagnostics == 1 &&
              printer_type.output.str() == "** 0\n" &&
              printer_type.errors.str().find("PRSTRING requires a character strip") !=
                  std::string::npos,
          "PRSTRING type rejection forced a supplier or lost recovery");
    Session print_failure;
    print_failure.runtime.feed("printer", "[1]->L;FUNCTION C X;CHAROUT(X);"
                               "IF X=49 THEN ITEMREAD();1/0 CLOSE END;"
                               "C->CUCHOUT;PR(L);");
    check(print_failure.output.str() == "[1",
          "printer did not suspend after its emitted prefix");
    print_failure.runtime.collect();
    print_failure.runtime.feed("printer", "17\n4=>", true);
    check(print_failure.runtime.diagnostics == 1 &&
              print_failure.output.str() == "[1** 4\n",
          "printer failure replayed output or retained pending tasks");
    Session unicode;
    unicode.runtime.feed("utf", std::string("2+2=> ") + char(0xce));
    check(unicode.output.str() == "** 4\n",
          "UTF-8 suffix delayed complete command");
    unicode.runtime.feed("utf", std::string(1, char(0xbb)) + "->L;", true);
    splits("ITEMREAD(),ITEMREAD(),ITEMREAD()->C->B->A;\"ABC\"\nA,B,C=>",
           "** \", ABC, \"\n");
    splits("99->X;FUNCTION RESET X;SETPOP() END;RESET(1);X=>",
           "SETPOP\n** 1\n");
    splits("'A''B'->S;DATALENGTH(S),S=>", "** 3, 'A'B'\n");
    splits("FNTOLIST(ITEMREAD)->L;HD(L)->X;17\nX,HD(L)=>", "** 17, 17\n");
    splits("MACRO M;PR('bad') END;ITEMREAD()->X;NONMAC M\nX=>", "** M\n");
    Session replacement;
    check(replacement.runtime.source("[2 + 3 =>]->PROGLIST;99=>"),
          replacement.errors.str());
    check(replacement.output.str() == "** 5\n", "PROGLIST assignment ignored");
    Session chain;
    check(chain.runtime.source("[[7 =>]]->NEXTCH;[2 =>]->PROGLIST;99=>"),
          chain.errors.str());
    check(chain.output.str() == "** 2\n** 7\n", "NEXTCH ignored");
    splits("FUNCTION READ2;ITEMREAD(),ITEMREAD() END;READ2()=> 11 12\n",
           "** 11, 12\n");
    Session cursor;
    auto list = cursor.runtime.list(
        {pop::Value(std::int64_t(11)), pop::Value(std::int64_t(12))});
    cursor.runtime.bind("PROGLIST", list);
    cursor.runtime.schedule(cursor.runtime.binding("ITEMREAD"), {});
    check(cursor.runtime.run(), cursor.errors.str());
    check(pop::integer(cursor.runtime.pop()) == 11,
          "ITEMREAD returned wrong item");
    cursor.runtime.collect();
    check(pop::equal(list, cursor.runtime.binding("PROGLIST")) &&
              cursor.runtime.format(list) == "[12]",
          "source list identity or mutation lost");
    splits("10->X;FUNCTION OLD;X END;\"X\"->W;CANCEL "
           "X;20->X;OLD(),X,VALOF(W),W=\"X\"=>",
           "** 10, 20, 20, 1\n");
    splits("10->X;FUNCTION RD;X END;FUNCTION F X;POPVAL([CANCEL X;VARS "
           "X;20->X;GOON]);RD() END;F(7)=> RD(),X=>",
           "** 7\n** 10, 20\n");
    splits("_A,_λ,_ ,__=>", "** 65, 955, 32, 95\n");
    splits("17->X;REFOF(\"X\")->R;FUNCTION F X;CONT(R) "
           "END;F(99)=>;18->CONT(R);X=>;COPY(R)->C;19->X;CONT(C),CONT(R),REFOF("
           "\"X\")=R=>",
           "** 99\n** 18\n** 18, 19, 1\n");
    splits("17->X;REFOF(\"X\")->R;CANCEL "
           "X;20->X;CONT(R),X=>;21->CONT(R);DATALIST(R),X=>",
           "** 17, 20\n** [21], 20\n");
    Session recovery;
    recovery.runtime.source(
        "FUNCTION EH MESSAGE CODE;CODE->SEEN END;EH->ERRFUN;99->X;"
        "FUNCTION BAD X;1/0 END;BAD(7);\nX,SEEN=>");
    check(recovery.runtime.diagnostics == 1 && recovery.errors.str().empty(),
          "custom ERRFUN was not authoritative");
    check(recovery.output.str() == "** 7, 1\n",
          "error reset restored failed invocation bindings");
    Session nested_error;
    nested_error.runtime.source("FUNCTION EH M C;1/0 END;EH->ERRFUN;1/0;\n4=>");
    check(nested_error.runtime.diagnostics == 2 &&
              nested_error.errors.str().find("error during ERRFUN") !=
                  std::string::npos,
          "nested diagnostic did not terminate recovery");
    check(nested_error.output.str() == "** 4\n",
          "nested diagnostic lost following source");
    Session lex_error;
    lex_error.runtime.source("PR('before');'unfinished", "lex-error");
    check(lex_error.output.str() == "'before'" &&
              lex_error.runtime.diagnostics == 1,
          "lexer error prevented earlier complete command effects");
    check(lex_error.errors.str().find("lex-error:") != std::string::npos,
          "lexer diagnostic lost source location");
    // A malformed suffix must not suppress or replay preceding commands.
    for (const std::string bad : {std::string("\xff"), std::string("\xe2\x41\x80"),
                                  std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
                                  std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82")}) {
      const std::string source = "PR('λ');\n" + bad;
      for (std::size_t cut = 0; cut <= source.size(); ++cut) {
        Session malformed;
        malformed.runtime.feed("bad-utf8", source.substr(0, cut), false);
        malformed.runtime.collect();
        malformed.runtime.feed("bad-utf8", source.substr(cut), true);
        check(malformed.output.str() == "'λ'",
              "malformed UTF-8 suppressed or replayed preceding output");
        check(malformed.runtime.diagnostics == 1,
              "malformed UTF-8 did not report exactly once");
        check(malformed.errors.str().find("bad-utf8:2:1:") != std::string::npos,
              "malformed UTF-8 diagnostic location");
        malformed.runtime.source("4=>", "after-utf8");
        check(malformed.output.str() == "'λ'** 4\n",
              "session did not recover after malformed UTF-8");
      }
    }
    Session supplier;
    supplier.runtime.bind("TEXT", supplier.runtime.string("PR('A');"));
    check(supplier.runtime.source(
              "0->POS;FUNCTION CH;POS+1->POS;IF POS>DATALENGTH(TEXT) THEN "
              "PR('Z');TERMIN ELSE SUBSCR(POS,TEXT) CLOSE END;COMPILE(CH);"),
          supplier.errors.str());
    check(supplier.output.str() == "'A''Z'",
          "COMPILE reads ahead of effects: " + supplier.output.str());
    Session carry;
    carry.runtime.bind("TEXT", carry.runtime.string("discard this END 4=>"));
    check(carry.runtime.source(
              "0->POS;FUNCTION CH;POS+1->POS;IF POS>DATALENGTH(TEXT) THEN "
              "TERMIN ELSE SUBSCR(POS,TEXT) CLOSE END;CARRYON(CH);"),
          carry.errors.str());
    check(carry.output.str() == "** 4\n",
          "generic CARRYON did not resume source");
    std::cout << "resumable compiler split tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
