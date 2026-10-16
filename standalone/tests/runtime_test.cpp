#include "pop/runtime.hpp"
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
using pop::Value;
namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> void fault(F action) {
  try {
    action();
  } catch (const pop::Fault &) {
    return;
  }
  throw std::runtime_error("expected a language error");
}
} // namespace
int main() {
  try {
    static_assert(sizeof(std::int64_t) == 8);
    static_assert(std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53);
    fault([] { Value(std::numeric_limits<double>::infinity()); });
    const auto max = std::numeric_limits<std::int64_t>::max();
    const auto min = std::numeric_limits<std::int64_t>::min();
    check(pop::integer(pop::arithmetic("+", Value(max - 1),
                                       Value(std::int64_t(1)))) == max,
          "int64 precision");
    fault([&] { pop::arithmetic("+", Value(max), Value(std::int64_t(1))); });
    fault([&] { pop::arithmetic("-", Value(min), Value(std::int64_t(1))); });
    fault([&] { pop::arithmetic("*", Value(max), Value(std::int64_t(2))); });
    fault([&] { pop::arithmetic("/", Value(1.0), Value(-0.0)); });
    fault([&] { pop::arithmetic("!", Value(-1.0), Value(0.5)); });
    check(!pop::equal(Value(std::int64_t(1)), Value(1.0)),
          "distinct numeric tags");
    check(pop::compare(Value(max), Value(9223372036854775808.0)) < 0,
          "mixed comparison upper bound");
    check(pop::compare(Value(std::int64_t(9007199254740993LL)),
                       Value(9007199254740992.0)) > 0,
          "mixed comparison exactness");
    check(pop::real(pop::arithmetic(
              "/", Value(std::numeric_limits<double>::min()), Value(2.0))) > 0,
          "subnormals");
    std::istringstream input("\xCE\xBB");
    std::ostringstream output, errors;
    pop::Options options;
    options.elapsed = [] { return 12.5; };
    pop::Runtime runtime(input, output, errors, options);
    runtime.source("-0.0->ZERO;POPTIM()->TIME;CHARIN()->CH;");
    check(runtime.diagnostics == 0, errors.str().c_str());
    check(std::signbit(pop::real(runtime.binding("ZERO"))),
          "signed zero literal");
    check(pop::real(runtime.binding("TIME")) == 12.5, "injected clock");
    check(pop::integer(runtime.binding("CH")) == 955, "Unicode input");
    auto first = runtime.word("abcdefOne"), second = runtime.word("ABCDEFTwo");
    check(pop::equal(first, second), "six character interning");
    runtime.collect();
    const auto baseline = runtime.objects.size();
    auto cycle = runtime.pair(Value::nil(), Value::nil());
    runtime.object(cycle).fields[1] = cycle;
    runtime.push(cycle);
    runtime.collect();
    check(runtime.objects.count(cycle.handle()) == 1, "cycle root");
    runtime.pop();
    runtime.collect();
    check(runtime.objects.size() == baseline, "unreachable cycle collection");
    runtime.source("VARS F G; FUNCTION MAKE X; LAMBDA; X+1->X;X END; LAMBDA; X "
                   "END END; MAKE(40)->G->F;");
    runtime.collect();
    runtime.source("F()->A;G()->B;");
    check(runtime.diagnostics == 0, errors.str().c_str());
    check(pop::integer(runtime.binding("A")) == 41 &&
              pop::integer(runtime.binding("B")) == 41,
          "shared captured cell");
    runtime.source("99->X;FUNCTION BAD X;1/0 END;BAD(1);\nX->AFTER;");
    check(runtime.diagnostics == 1, "recoverable error");
    check(pop::integer(runtime.binding("AFTER")) == 1,
          "error reset preserves active variable values");
    runtime.source("FUNCTION FAIL X Y;X END;FAIL(1);\nX->AFTER;");
    check(runtime.diagnostics == 2 &&
              pop::integer(runtime.binding("AFTER")) == 1,
          "argument underflow leaves bindings unchanged");
    // Host handles stay stable across collection; there is no address
    // arithmetic.
    check(pop::equal(first, runtime.word("abcdef")), "stable word handle");
    runtime.source("2+", "<chunks>", false);
    runtime.source("2->FOUR;", "<chunks>", true);
    check(pop::integer(runtime.binding("FOUR")) == 4,
          "buffered source fragments");
    runtime.source("10->OLDVAR;FUNCTION KEEP;OLDVAR END;");
    auto retired = runtime.identifier("OLDVAR");
    runtime.source("CANCEL OLDVAR;20->OLDVAR;");
    runtime.collect();
    check(runtime.objects.count(retired.handle()) == 1,
          "compiled binding lost after cancellation");
    runtime.source("KEEP()->KEPT;0->KEEP;");
    runtime.collect();
    check(pop::integer(runtime.binding("KEPT")) == 10,
          "cancel retargeted a compiled binding");
    check(!runtime.objects.count(retired.handle()),
          "unreachable retired identifier retained");
    auto deep = Value(std::int64_t(7));
    for (int i = 0; i < 2000; ++i)
      deep = runtime.list({deep});
    auto rendered = runtime.format(deep);
    check(rendered == std::string(2000, '[') + "7" + std::string(2000, ']'),
          "deep acyclic lists must not be reported as cycles");
    runtime.bind("DEEP", deep);
    const auto before_print = output.str();
    const auto before_errors = runtime.diagnostics;
    runtime.options.max_frames = 8;
    runtime.source("PR(DEEP);");
    runtime.options.max_frames = 0;
    check(output.str() == before_print + rendered &&
              runtime.diagnostics == before_errors,
          "streaming deep objects uses tasks rather than recursive call frames");
    std::cout << "runtime invariants passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
