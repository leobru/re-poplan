#include "pop/runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
namespace pop {
void Runtime::register_primitives() {
  std::istringstream names(
      "+ - * / ! // = /= < > =< >= :: <> ATOM BACK BOOLAND BOOLOR BOUNDSLIST "
      "CHARIN CHAROUT CHARWORD COMPIL CONS CONSPAIR CONSREF CONSWORD CONT COPY "
      "CUCHIN CUCHOUT DATALENGTH DATALIST DATAWORD DEST DESTPAIR DESTREF "
      "DESTWORD ERASE ERRFUN FNPART FNPROPS FNTOLIST FRONT FROZVAL GENOUT HD "
      "IDENTFN IDENTPROPS INCHARITEM INIT INITC INTOF ISCOMPND ISFUNC "
      "ISINTEGER ISLINK ISLIST ISREAL ISWORD ITEMREAD LOGAND LOGNOT LOGOR "
      "LOGSHIFT MACRESULTS MAPLIST MEANING NEWANYARRAY NEWARRAY NL NOT NULL "
      "PARTAPPLY POPMESS POPVAL PR PRREAL PRSTRING PRINT REALOF RECORDFNS REV "
      "SETPOP SIGN SP STACKLENGTH STRIPFNS SUBSCR SUBSCC TL UPDATER APPDATA "
      "APPLIST APPLY ARCTAN CARRYON COPYLIST COREUSED COS EQUAL EXP FNCOMP "
      "LENGTH LIBRARY LISTREAD LOG NUMBERREAD POPTIME PRBIN PROCT SIN SQRT TAN "
      "VALOF REFOF POPDATE CODIPC CODPIC CODIPS CODPIS SAMEDATA JUMPOUT");
  for (std::string name; names >> name;) {
    auto key = canonical(name);
    auto f = primitive(key);
    object(f).protected_function = true;
    bind(key, f);
  }
}
void Runtime::invoke(const std::string &name, Object *fn, bool update) {
  auto boolean = [&](bool b) { push(Value(std::int64_t(b))); };
  auto size = [&](Value v) -> std::size_t {
    auto n = integer(v);
    if (n < 0 ||
        static_cast<std::uint64_t>(n) > std::numeric_limits<std::size_t>::max())
      throw Fault("invalid size");
    return std::size_t(n);
  };
  auto get = [&](Value v, Object::Kind k) -> Object & {
    if (v.object() && object(v).source_forward.object())
      empty_list(v);
    auto &o = object(v);
    if (o.kind != k)
      throw Fault("wrong argument type for " + name);
    return o;
  };
  auto generated = [&](const std::string &n, std::vector<Value> fields = {},
                       std::size_t index = 0) {
    auto v = primitive(n);
    auto &o = object(v);
    o.fields = std::move(fields);
    o.index = index;
    return v;
  };
  auto pushop = [](Program &p, Value v) {
    p.code.push_back({Operation::Kind::push, v, {}, {}, {}});
  };
  auto callop = [&](Program &p, Value f) {
    pushop(p, f);
    p.code.push_back({Operation::Kind::call_value, {}, {}, {}, {}});
  };
  auto queue = [&](std::shared_ptr<Program> p) {
    frames.push_back({std::move(p), 0, {}, {}, {}, {}, {}});
  };
  auto component = [&](Value value, std::size_t width) {
    if (width) {
      auto n = integer(value);
      if (n < 0 ||
          (width < 64 && std::uint64_t(n) > ((std::uint64_t(1) << width) - 1)))
        throw Fault("component out of range");
    }
    return value;
  };
  if (fn && fn->function_kind == Object::FunctionKind::partial) {
    if (update)
      throw Fault("function has no callable updater");
    auto p = std::make_shared<Program>();
    for (auto v : fn->fields)
      pushop(*p, v);
    p->code.push_back(
        {Operation::Kind::call_value, {}, {}, update ? 1u : 0u, {}});
    queue(p);
    return;
  }
  if (fn && fn->function_kind == Object::FunctionKind::composition) {
    if (update)
      throw Fault("function has no callable updater");
    auto p = std::make_shared<Program>();
    callop(*p, fn->fields[0]);
    callop(*p, fn->fields[1]);
    queue(p);
    return;
  }
  if (fn && fn->function_kind == Object::FunctionKind::array) {
    std::size_t offset = 0, stride = 1;
    for (std::size_t j = fn->bounds.size(); j > 0; j -= 2) {
      auto x = integer(pop()), lo = fn->bounds[j - 2], hi = fn->bounds[j - 1];
      if (x < lo || x > hi)
        throw Fault("array index out of bounds");
      offset += std::size_t(x - lo) * stride;
      stride *= std::size_t(hi - lo + 1);
    }
    push(Value(std::int64_t(offset + 1)));
    push(fn->fields[0]);
    call(fn->fields[1], update);
    return;
  }
  if (name == "@REC") {
    std::vector<Value> values(fn->index);
    for (std::size_t i = values.size(); i-- > 0;)
      values[i] =
          component(pop(), std::size_t(object(fn->fields[0]).bounds.at(i)));
    auto v = make(Object::Kind::record);
    object(v).type = fn->fields[0];
    object(v).fields = std::move(values);
    push(v);
    return;
  }
  if (name == "@DEST") {
    auto v = pop();
    auto &o = get(v, Object::Kind::record);
    if (!equal(o.type, fn->fields[0]))
      throw Fault("record class mismatch");
    for (auto x : o.fields)
      push(x);
    return;
  }
  if (name == "@FIELD") {
    auto v = pop();
    auto &o = get(v, Object::Kind::record);
    if (!equal(o.type, fn->fields[0]))
      throw Fault("record class mismatch");
    if (update)
      o.fields.at(fn->index) =
          component(pop(), std::size_t(object(o.type).bounds.at(fn->index)));
    else
      push(o.fields.at(fn->index));
    return;
  }
  if (name == "@SINIT") {
    auto n = size(pop());
    auto v = make(Object::Kind::strip);
    auto &o = object(v);
    o.fields.assign(n, Value(std::int64_t(0)));
    o.type = fn->fields[0];
    o.width = fn->index;
    push(v);
    return;
  }
  if (name == "@SGET" || name == "SUBSCR" || name == "SUBSCC") {
    auto v = pop();
    auto i = integer(pop());
    auto &o = object(v);
    if (i < 1)
      throw Fault("subscript out of bounds");
    if (o.kind == Object::Kind::string) {
      auto chars = decode(o.text);
      if (std::uint64_t(i) > chars.size())
        throw Fault("subscript out of bounds");
      if (update) {
        auto c = integer(pop());
        if (c < 0 || c > 0x10ffff)
          throw Fault("invalid character");
        chars[i - 1] = char32_t(c);
        o.text.clear();
        for (auto ch : chars)
          o.text += encode(ch);
      } else
        push(Value(std::int64_t(chars[i - 1])));
      return;
    }
    if (o.kind != Object::Kind::strip || std::uint64_t(i) > o.fields.size())
      throw Fault("strip subscript out of bounds");
    if (name == "@SGET" && !equal(o.type, fn->fields[0]))
      throw Fault("strip class mismatch");
    if (update) {
      auto value = pop();
      if (o.width) {
        auto n = integer(value);
        if (n < 0 || (o.width < 64 &&
                      std::uint64_t(n) > ((std::uint64_t(1) << o.width) - 1)))
          throw Fault("strip component out of range");
      }
      o.fields[i - 1] = value;
    } else
      push(o.fields[i - 1]);
    return;
  }
  if (name == "@ASET") {
    push(Value(std::int64_t(fn->index + 1)));
    push(object(fn->fields[0]).fields[0]);
    call(object(fn->fields[0]).fields[1], true);
    return;
  }
  if (name == "@ARRIN") {
    auto array = fn->fields[0];
    object(array).fields[0] = pop();
    auto bounds = object(array).bounds;
    auto total = fn->index;
    auto p = std::make_shared<Program>();
    for (std::size_t offset = 0; offset < total; ++offset) {
      auto remaining = offset;
      std::vector<std::int64_t> indices(bounds.size() / 2);
      for (std::size_t j = indices.size(); j-- > 0;) {
        auto span = std::size_t(bounds[2 * j + 1] - bounds[2 * j] + 1);
        indices[j] = bounds[2 * j] + std::int64_t(remaining % span);
        remaining /= span;
      }
      for (auto n : indices)
        pushop(*p, Value(n));
      callop(*p, fn->fields[1]);
      callop(*p, generated("@ASET", {array}, offset));
    }
    pushop(*p, array);
    queue(p);
    return;
  }
  if (name == "@EOF") {
    push(Value::termin());
    return;
  }
  if (name == "@PHYS") {
    physical_token = read_physical_token();
    // The magnitude of INT64_MIN is a lexical token, not an int64 value.
    // Leave conversion to the signed reader/compiler in this one case.
    if (physical_token.kind == Token::Kind::number &&
        physical_token.text == "9223372036854775808")
      push({});
    else
      push(token_value(physical_token));
    return;
  }
  if (name == "@FORCD") {
    auto value = pop();
    auto &node = object(fn->fields[0]);
    node.forcing = false;
    if (std::holds_alternative<Terminator>(value.data))
      node.fields = {Value(std::int64_t(0)), fn->fields[1]};
    else {
      node.fields = {value, pair(Value(std::int64_t(1)), fn->fields[1])};
      if (object(fn->fields[1]).text == "@PHYS") {
        node.source_token = physical_token;
        if (!std::holds_alternative<Undefined>(value.data))
          node.source_token->value = value;
      }
    }
    return;
  }
  if (name == "@UPDAT") {
    auto &base = get(fn->fields[0], Object::Kind::function);
    invoke(base.text, &base, true);
    return;
  }
  if (name == "@NEG") {
    auto v = pop();
    push(v.real() ? Value(-real(v))
                  : arithmetic("-", Value(std::int64_t(0)), v));
    return;
  }
  if (name == "+" || name == "-" || name == "*" || name == "/" || name == "!") {
    auto b = pop(), a = pop();
    push(arithmetic(name, a, b));
    return;
  }
  if (name == "//") {
    auto b = integer(pop()), a = integer(pop());
    if (!b)
      throw Fault("division by zero");
    if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
      throw Fault("integer overflow");
    auto q = a / b, r = a % b;
    if (r && ((r < 0) != (b < 0))) {
      --q;
      r += b;
    }
    push(Value(r));
    push(Value(q));
    return;
  }
  if (name == "=" || name == "/=") {
    auto b = pop(), a = pop();
    boolean(equal(a, b) == (name == "="));
    return;
  }
  if (name == "<" || name == ">" || name == "=<" || name == ">=") {
    auto b = pop(), a = pop();
    int c = compare(a, b);
    boolean(name == "<"    ? c < 0
            : name == ">"  ? c > 0
            : name == "=<" ? c <= 0
                           : c >= 0);
    return;
  }
  if (name == "ISINTE" || name == "ISREAL" || name == "ISFUNC" ||
      name == "ISWORD" || name == "ISLINK" || name == "ISLIST" ||
      name == "ISCOMP" || name == "ATOM") {
    auto v = pop();
    bool b = false;
    if (name == "ISINTE")
      b = v.integer();
    else if (name == "ISREAL")
      b = v.real();
    else if (name == "ISCOMP")
      b = v.object();
    else if (name == "ATOM")
      b = !v.object() || object(v).kind != Object::Kind::pair;
    else if (name == "ISLIST" || name == "ISLINK") {
      bool empty = empty_list(v);
      b = empty || (v.object() && object(v).kind == Object::Kind::pair &&
                    !object(v).plain_pair &&
                    (object(v).fields[1].nil_value() ||
                     (object(v).fields[1].object() &&
                      object(object(v).fields[1]).kind == Object::Kind::pair)));
      if (name == "ISLINK")
        b = b && !empty;
    } else if (v.object())
      b = object(v).kind ==
          (name == "ISFUNC" ? Object::Kind::function : Object::Kind::word);
    boolean(b);
    return;
  }
  if (name == "NOT") {
    boolean(!pop().truth());
    return;
  }
  if (name == "BOOLAN" || name == "BOOLOR") {
    auto b = pop().truth(), a = pop().truth();
    boolean(name == "BOOLAN" ? a && b : a || b);
    return;
  }
  if (name == "INTOF") {
    auto v = pop();
    if (!v.real())
      throw Fault("INTOF requires real");
    double n = std::floor(real(v));
    if (n < -9223372036854775808.0 || n >= 9223372036854775808.0)
      throw Fault("integer conversion overflow");
    push(Value(std::int64_t(n)));
    return;
  }
  if (name == "REALOF") {
    auto v = pop();
    integer(v);
    push(Value(real(v)));
    return;
  }
  if (name == "SIGN") {
    auto v = pop();
    push(Value(std::int64_t(compare(v, Value(std::int64_t(0))))));
    return;
  }
  if (name == "SIN" || name == "COS" || name == "TAN" || name == "ARCTAN" ||
      name == "SQRT" || name == "EXP" || name == "LOG") {
    auto x = real(pop());
    auto n = name == "SIN"      ? std::sin(x)
             : name == "COS"    ? std::cos(x)
             : name == "TAN"    ? std::tan(x)
             : name == "ARCTAN" ? std::atan(x)
             : name == "SQRT"   ? std::sqrt(x)
             : name == "EXP"    ? std::exp(x)
                                : std::log(x);
    if (!std::isfinite(n))
      throw Fault("real domain error or overflow");
    push(Value(n));
    return;
  }
  if (name == "LOGAND" || name == "LOGOR" || name == "LOGNOT" ||
      name == "LOGSHI") {
    std::uint64_t bits = 0;
    if (name == "LOGNOT")
      bits = ~std::uint64_t(integer(pop()));
    else {
      auto y = integer(pop());
      auto x = std::uint64_t(integer(pop()));
      if (name == "LOGAND")
        bits = x & std::uint64_t(y);
      else if (name == "LOGOR")
        bits = x | std::uint64_t(y);
      else
        bits = y >= 64 || y <= -64 ? 0
               : y < 0             ? x >> unsigned(-y)
                                   : x << unsigned(y);
    }
    std::int64_t n;
    std::memcpy(&n, &bits, sizeof n);
    push(Value(n));
    return;
  }
  if (name == "ERASE") {
    pop();
    return;
  }
  if (name == "APPLY") {
    schedule(pop(), {}, update);
    return;
  }
  if (name == "SETPOP") {
    resetting = true;
    return;
  }
  if (name == "IDENTF")
    return;
  if (name == "STACKL") {
    push(Value(std::int64_t(stack.size())));
    return;
  }
  if (name == "REFOF") {
    auto value = pop();
    get(value, Object::Kind::word);
    auto symbol = identifier(value);
    if (protected_binding(symbol))
      throw Fault("REFOF of protected identifier");
    auto &w = object(symbol);
    cell(symbol);
    if (!w.binding_reference.object()) {
      w.binding_reference = make(Object::Kind::reference);
      object(w.binding_reference).referent = symbol;
    }
    push(w.binding_reference);
    return;
  }
  if (name == "CONSRE") {
    auto value = pop();
    auto v = make(Object::Kind::reference);
    object(v).fields = {value};
    push(v);
    return;
  }
  if (name == "CONT" || name == "DESTRE") {
    auto v = pop();
    auto &o = get(v, Object::Kind::reference);
    if (o.referent.object()) {
      if (update)
        assign(o.referent, pop());
      else
        push(binding(o.referent));
    } else {
      if (update)
        o.fields[0] = pop();
      else
        push(o.fields[0]);
    }
    return;
  }
  if (name == "CONS" || name == "CONSPA" || name == "::") {
    auto b = pop(), a = pop();
    auto v = pair(a, b);
    object(v).plain_pair = name == "CONSPA";
    push(v);
    return;
  }
  if (name == "FRONT" || name == "BACK" || name == "HD" || name == "TL") {
    auto v = pop();
    if ((name == "HD" || name == "TL") && empty_list(v))
      throw Fault("nonempty list required");
    auto &o = get(v, Object::Kind::pair);
    auto i = name == "FRONT" || name == "HD" ? 0 : 1;
    if (update)
      o.fields[i] = pop();
    else
      push(o.fields[i]);
    return;
  }
  if (name == "DEST" || name == "DESTPA") {
    auto v = pop();
    if (name == "DEST" && empty_list(v))
      throw Fault("nonempty list required");
    auto &o = get(v, Object::Kind::pair);
    push(o.fields[0]);
    push(o.fields[1]);
    return;
  }
  if (name == "NULL") {
    boolean(empty_list(pop()));
    return;
  }
  if (name == "<>") {
    auto b = pop(), a = pop();
    auto values = items(a);
    // Section 7.3: only the first operand is copied; the second is shared.
    for (auto it = values.rbegin(); it != values.rend(); ++it)
      b = pair(*it, b);
    push(b);
    return;
  }
  if (name == "REV" || name == "COPYLI") {
    auto values = items(pop());
    if (name == "REV")
      std::reverse(values.begin(), values.end());
    push(list(values));
    return;
  }
  if (name == "DATALI") {
    push(list(data_components(pop())));
    return;
  }
  if (name == "DATAWO") {
    auto v = pop();
    if (update) {
      auto type = pop();
      auto &o = object(v);
      if (o.kind != Object::Kind::record && o.kind != Object::Kind::strip)
        throw Fault("cannot change standard class");
      if (!o.type.object() || object(o.type).kind == Object::Kind::word)
        throw Fault("cannot change standard class");
      object(o.type).type = type;
    } else
      push(data_word(v));
    return;
  }
  if (name == "COPY") {
    auto v = pop();
    if (v.object() && object(v).source_forward.object())
      empty_list(v);
    auto &o = object(v);
    if (o.kind == Object::Kind::word)
      throw Fault("COPY cannot copy an interned word");
    if (o.kind == Object::Kind::function)
      throw Fault("COPY cannot copy a function");
    auto c = make(o.kind);
    object(c) = o;
    if (o.kind == Object::Kind::reference && o.referent.object()) {
      object(c).fields = data_fields(v);
      object(c).referent = {};
    }
    push(c);
    return;
  }
  if (name == "LENGTH") {
    // Manual section 7.3: count list elements, forcing lazy tails.
    push(Value(std::int64_t(items(pop()).size())));
    return;
  }
  if (name == "DATALE") {
    // Manual section 6.3: strip components, not list elements or record fields.
    auto v = pop();
    if (!v.object())
      throw Fault("DATALENGTH requires a strip");
    const auto &o = object(v);
    if (o.kind != Object::Kind::string && o.kind != Object::Kind::strip)
      throw Fault("DATALENGTH requires a strip");
    auto n = o.kind == Object::Kind::string ? decode(o.text).size()
                                            : o.fields.size();
    push(Value(std::int64_t(n)));
    return;
  }
  if (name == "RECORD") {
    auto widths = items(pop());
    auto type = pop();
    get(type, Object::Kind::word);
    for (auto v : widths) {
      auto w = integer(v);
      if (w < 0 || w > 64)
        throw Fault("invalid record component width");
    }
    auto descriptor = make(Object::Kind::reference);
    object(descriptor).type = type;
    for (auto v : widths)
      object(descriptor).bounds.push_back(integer(v));
    type = descriptor;
    push(generated("@REC", {type}, widths.size()));
    push(generated("@DEST", {type}));
    for (std::size_t i = 0; i < widths.size(); ++i)
      push(generated("@FIELD", {type}, i));
    return;
  }
  if (name == "STRIPF") {
    auto width = integer(pop());
    auto type = pop();
    get(type, Object::Kind::word);
    if (width < 0 || width > 64)
      throw Fault("invalid strip width");
    auto descriptor = make(Object::Kind::reference);
    object(descriptor).type = type;
    type = descriptor;
    push(generated("@SINIT", {type}, std::size_t(width)));
    push(generated("@SGET", {type}));
    return;
  }
  if (name == "INIT" || name == "INITC") {
    auto n = size(pop());
    auto v = make(Object::Kind::strip);
    object(v).fields.assign(n, Value(std::int64_t(0)));
    object(v).type = word(name == "INIT" ? "STRIP" : "CSTRIP");
    object(v).width = name == "INITC" ? 21 : 0;
    push(v);
    return;
  }
  if (name == "NEWARR" || name == "NEWANY") {
    auto sub = binding("SUBSCR"), init = binding("INIT");
    if (name == "NEWANY") {
      sub = pop();
      init = pop();
      get(sub, Object::Kind::function);
      get(init, Object::Kind::function);
    }
    auto initializer = pop();
    get(initializer, Object::Kind::function);
    auto values = items(pop());
    if (values.empty() || values.size() % 2)
      throw Fault("array bounds require pairs");
    std::vector<std::int64_t> bounds;
    std::size_t total = 1;
    for (std::size_t i = 0; i < values.size(); i += 2) {
      auto lo = integer(values[i]), hi = integer(values[i + 1]);
      if (hi < lo)
        throw Fault("invalid array bounds");
      auto span = integer(arithmetic("+", arithmetic("-", Value(hi), Value(lo)),
                                     Value(std::int64_t(1))));
      if (std::uint64_t(span) > std::numeric_limits<std::size_t>::max() / total)
        throw Fault("array too large");
      total *= std::size_t(span);
      bounds.push_back(lo);
      bounds.push_back(hi);
    }
    auto array = make(Object::Kind::function);
    object(array).function_kind = Object::FunctionKind::array;
    object(array).fields = {{}, sub};
    object(array).bounds = bounds;
    object(array).width = total;
    auto p = std::make_shared<Program>();
    pushop(*p, Value(std::int64_t(total)));
    callop(*p, init);
    callop(*p, generated("@ARRIN", {array, initializer}, total));
    queue(p);
    return;
  }
  if (name == "BOUNDS") {
    auto &o = get(pop(), Object::Kind::function);
    if (o.function_kind != Object::FunctionKind::array)
      throw Fault("array required");
    if (update) {
      auto values = items(pop());
      if (values.empty() || values.size() % 2)
        throw Fault("array bounds require pairs");
      std::vector<std::int64_t> bounds;
      std::size_t total = 1;
      for (std::size_t i = 0; i < values.size(); i += 2) {
        auto lo = integer(values[i]), hi = integer(values[i + 1]);
        if (hi < lo)
          throw Fault("invalid array bounds");
        auto span =
            integer(arithmetic("+", arithmetic("-", Value(hi), Value(lo)),
                               Value(std::int64_t(1))));
        if (std::uint64_t(span) > o.width / total)
          throw Fault("array bounds exceed storage");
        total *= std::size_t(span);
        bounds.push_back(lo);
        bounds.push_back(hi);
      }
      o.bounds = std::move(bounds);
    } else {
      std::vector<Value> vs;
      for (auto n : o.bounds)
        vs.push_back(Value(n));
      push(list(vs));
    }
    return;
  }
  if (name == "PARTAP") {
    auto frozen = items(pop());
    auto f = pop();
    get(f, Object::Kind::function);
    push(partial(f, frozen));
    return;
  }
  if (name == "FNCOMP") {
    auto b = pop(), a = pop();
    get(a, Object::Kind::function);
    get(b, Object::Kind::function);
    auto v = make(Object::Kind::function);
    object(v).function_kind = Object::FunctionKind::composition;
    object(v).fields = {a, b};
    push(v);
    return;
  }
  if (name == "FNPART" || name == "FROZVA" || name == "FNPROP" ||
      name == "UPDATE") {
    auto v = pop();
    auto &o = get(v, Object::Kind::function);
    if (update && o.protected_function)
      throw Fault("protected function");
    if (name == "FNPROP") {
      if (update)
        o.property = pop();
      else
        push(o.property);
      return;
    }
    if (name == "UPDATE") {
      if (update) {
        o.updater = pop();
        o.updater_assigned = true;
      } else {
        push(updater(v));
      }
      return;
    }
    if (o.function_kind != Object::FunctionKind::partial)
      throw Fault("partial application required");
    auto index = name == "FNPART" ? o.fields.size() - 1 : size(pop()) - 1;
    if (index >= o.fields.size() - (name == "FROZVA" ? 1 : 0))
      throw Fault("frozen argument index out of bounds");
    if (update) {
      auto value = pop();
      if (name == "FNPART")
        get(value, Object::Kind::function);
      o.fields[index] = value;
    } else
      push(o.fields[index]);
    return;
  }
  if (name == "CHARWO") {
    auto index = size(pop());
    auto &o = get(pop(), Object::Kind::word);
    auto chars = decode(o.text);
    if (index < 1 || index > chars.size())
      throw Fault("word character index out of bounds");
    push(Value(std::int64_t(chars[index - 1])));
    return;
  }
  if (name == "CONSWO") {
    auto n = size(pop());
    std::vector<char32_t> chars(n);
    for (std::size_t i = n; i-- > 0;) {
      auto c = integer(pop());
      if (c < 0 || c > 0x10ffff)
        throw Fault("invalid character");
      chars[i] = char32_t(c);
    }
    std::string s;
    for (auto c : chars)
      s += encode(c);
    push(word(s));
    return;
  }
  if (name == "DESTWO") {
    auto &o = get(pop(), Object::Kind::word);
    auto chars = decode(o.text);
    for (auto c : chars)
      push(Value(std::int64_t(c)));
    push(Value(std::int64_t(chars.size())));
    return;
  }
  if (name == "MEANIN" || name == "VALOF") {
    auto symbol = pop();
    get(symbol, Object::Kind::word);
    if (update)
      assign(symbol, pop());
    else
      push(binding(symbol));
    return;
  }
  if (name == "IDENTP") {
    auto &o = get(pop(), Object::Kind::word);
    static const std::set<std::string> syntax = {
        "VARS", "FUNCTI", "IF",    "ELSE",   "ELSEIF", "CLOSE",
        "END",  "LAMBDA", "MACRO", "FORALL", "LOOPIF", "RETURN",
        "GOTO", "CANCEL", "NONOP", "NONMAC", "GOON"};
    if (syntax.count(o.text))
      push(word("SYNTAX"));
    else if (macros.count(o.text))
      push(word("MACRO"));
    else
      push(Value(std::int64_t(
          operators.count(o.text) ? (100 - operators[o.text]) / 10 : 0)));
    return;
  }
  if (name == "APPLIS" || name == "MAPLIS") {
    auto f = pop();
    get(f, Object::Kind::function);
    auto v = pop();
    auto p = std::make_shared<Program>();
    if (name == "MAPLIS")
      p->code.push_back({Operation::Kind::begin_list, {}, {}, {}, {}});
    callop(*p, generated("@EACH", {v, f}));
    if (name == "MAPLIS")
      p->code.push_back({Operation::Kind::end_list, {}, {}, {}, {}});
    queue(p);
    return;
  }
  if (name == "@EACH") {
    auto v = fn->fields[0];
    if (empty_list(v))
      return;
    const auto &node = get(v, Object::Kind::pair);
    auto p = std::make_shared<Program>();
    pushop(*p, node.fields[0]);
    callop(*p, fn->fields[1]);
    callop(*p, generated("@AFTER", fn->fields));
    queue(p);
    return;
  }
  if (name == "@AFTER") {
    // Read the tail after the callback: callbacks may mutate the source list.
    auto tail = get(fn->fields[0], Object::Kind::pair).fields[1];
    schedule(generated("@EACH", {tail, fn->fields[1]}), {});
    return;
  }
  if (name == "APPDAT") {
    auto f = pop();
    get(f, Object::Kind::function);
    auto v = pop();
    auto values = data_components(v);
    auto p = std::make_shared<Program>();
    for (auto x : values) {
      pushop(*p, x);
      callop(*p, f);
    }
    queue(p);
    return;
  }
  if (name == "FNTOLI") {
    auto supplier = pop();
    get(supplier, Object::Kind::function);
    push(pair(Value(std::int64_t(1)), supplier));
    return;
  }
  if (name == "SAMEDA") {
    auto b = pop(), a = pop();
    bool same = a.data.index() == b.data.index();
    if (same && a.object()) {
      auto &x = object(a), &y = object(b);
      same = x.kind == y.kind && equal(x.type, y.type);
    }
    boolean(same);
    return;
  }
  if (name == "EQUAL") {
    std::vector<std::pair<Value, Value>> pending{{pop(), pop()}};
    std::set<std::pair<Handle, Handle>> seen;
    bool same = true;
    while (!pending.empty() && same) {
      auto [a, b] = pending.back();
      pending.pop_back();
      if (equal(a, b))
        continue;
      bool ae = empty_list(a), be = empty_list(b);
      if (ae || be) {
        same = ae && be;
        continue;
      }
      if (!a.object() || !b.object()) {
        same = false;
        break;
      }
      auto &x = object(a), &y = object(b);
      if (x.kind != Object::Kind::pair || y.kind != Object::Kind::pair) {
        same = false;
        break;
      }
      if (!seen.emplace(a.handle(), b.handle()).second)
        continue;
      pending.emplace_back(x.fields[0], y.fields[0]);
      pending.emplace_back(x.fields[1], y.fields[1]);
    }
    boolean(same);
    return;
  }
  if (name == "LISTRE") {
    auto state = make(Object::Kind::record);
    schedule(generated("@LSTEP", {state}), {});
    return;
  }
  if (name == "@LSTEP") {
    auto p = std::make_shared<Program>();
    callop(*p, binding("ITEMREAD"));
    callop(*p, generated("@LTAKE", fn->fields));
    queue(p);
    return;
  }
  if (name == "@LTAKE") {
    auto v = pop();
    auto state = fn->fields[0];
    auto &levels = object(state).fields;
    auto is = [&](const std::string &wordname) {
      return v.object() && object(v).kind == Object::Kind::word &&
             object(v).text == wordname;
    };
    if (levels.empty()) {
      if (!is("[")) {
        push(v);
        return;
      }
      levels = {Value::nil(), Value::nil()};
    } else if (std::holds_alternative<Terminator>(v.data))
      throw Fault("unfinished list input");
    else if (is("[")) {
      levels.push_back(Value::nil());
      levels.push_back(Value::nil());
    } else {
      if (is("]")) {
        v = levels[levels.size() - 2];
        levels.resize(levels.size() - 2);
        if (levels.empty()) {
          push(v);
          return;
        }
      }
      auto node = pair(v, Value::nil());
      auto &head = levels[levels.size() - 2], &tail = levels.back();
      if (head.nil_value())
        head = node;
      else
        object(tail).fields[1] = node;
      tail = node;
    }
    schedule(generated("@LSTEP", {state}), {});
    return;
  }
  if (name == "NUMBER") {
    schedule(generated("@NUM"), {});
    return;
  }
  if (name == "@NUM") {
    auto t = read_token();
    if (t.kind == Token::Kind::word && macros.count(t.text)) {
      schedule(generated("@NUM", {}, fn->index), {});
      schedule(primitive("@IMAC"), {});
      schedule(macros.at(t.text), {});
      return;
    }
    if (!fn->index && t.kind == Token::Kind::symbol &&
        (t.text == "-" || t.text == "+")) {
      schedule(generated("@NUM", {}, t.text == "-" ? 1 : 2), {});
      return;
    }
    if (t.kind != Token::Kind::number)
      throw Fault("number required by NUMBERREAD");
    if (fn->index == 1 && !t.value && t.text == "9223372036854775808") {
      push(Value(std::numeric_limits<std::int64_t>::min()));
      return;
    }
    auto v = token_value(t);
    push(fn->index == 1
             ? (v.real() ? Value(-real(v))
                         : arithmetic("-", Value(std::int64_t(0)), v))
             : v);
    return;
  }
  if (name == "INCHAR") {
    auto supplier = pop();
    get(supplier, Object::Kind::function);
    push(generated("@READ", {supplier, string("")}));
    return;
  }
  if (name == "@READ") {
    if (fn->fields.size() > 2) {
      auto value = fn->fields[2];
      fn->fields.erase(fn->fields.begin() + 2);
      push(value);
      return;
    }
    auto reader = Value::object(0);
    for (auto &[id, o] : objects)
      if (o.get() == fn) {
        reader = Value::object(id);
        break;
      }
    auto &buffer = object(fn->fields[1]).text;
    try {
      auto tokens = tokenize(buffer, "<INCHARITEM>", fn->closed, 1);
      if (tokens.size() > 1) {
        auto where = tokens.back().where;
        auto chars = decode(buffer);
        std::size_t line = 1, col = 1, offset = 0;
        while (
            offset < chars.size() &&
            (line < where.line || (line == where.line && col < where.column))) {
          if (chars[offset++] == '\n') {
            ++line;
            col = 1;
          } else
            ++col;
        }
        buffer.clear();
        for (; offset < chars.size(); ++offset)
          buffer += encode(chars[offset]);
        auto token = tokens.front();
        if (token.kind == Token::Kind::quoted) {
          fn->fields.push_back(word(token.text));
          fn->fields.push_back(word("\""));
          push(word("\""));
        } else
          push(token_value(token));
        return;
      }
      if (fn->closed) {
        push(Value::termin());
        return;
      }
    } catch (const NeedInput &) {
      if (fn->closed)
        throw Fault("unfinished character input");
    }
    auto p = std::make_shared<Program>();
    callop(*p, fn->fields[0]);
    callop(*p, generated("@RCHAR", {reader}));
    queue(p);
    return;
  }
  if (name == "@RCHAR") {
    auto c = pop();
    auto reader = fn->fields[0];
    auto &r = object(reader);
    if (std::holds_alternative<Terminator>(c.data))
      r.closed = true;
    else {
      auto n = integer(c);
      if (n < 0 || n > 0x10ffff)
        throw Fault("invalid input character");
      object(r.fields[1]).text += encode(char32_t(n));
    }
    schedule(reader, {});
    return;
  }
  if (name == "GENOUT") {
    auto consumer = pop(), printer = pop();
    get(consumer, Object::Kind::function);
    get(printer, Object::Kind::function);
    auto p = std::make_shared<Program>();
    p->function = true;
    p->args = {"CUCHOU"};
    callop(*p, printer);
    auto f = function(p);
    push(partial(f, {consumer}));
    return;
  }
  if (name == "CHARIN" || name == "CUCHIN") {
    auto first = in.get();
    if (first == std::char_traits<char>::eof()) {
      push(Value::termin());
      return;
    }
    std::string bytes(1, char(first));
    auto b = static_cast<unsigned char>(first);
    unsigned extra = b < 128              ? 0
                     : (b & 0xe0) == 0xc0 ? 1
                     : (b & 0xf0) == 0xe0 ? 2
                     : (b & 0xf8) == 0xf0 ? 3
                                          : 0;
    for (unsigned j = 0; j < extra; ++j) {
      auto next = in.get();
      if (next == std::char_traits<char>::eof())
        throw Fault("truncated UTF-8 input");
      bytes += char(next);
    }
    push(Value(std::int64_t(decode(bytes).at(0))));
    return;
  }
  if (name == "COMPIL") {
    auto f = pop();
    get(f, Object::Kind::function);
    compile_stream(f);
    return;
  }
  if (name == "@FEED") {
    auto value = pop();
    if (std::holds_alternative<Terminator>(value.data) ||
        (value.integer() && integer(value) == 94)) {
      input_eof = true;
      source_supplier = {};
    } else {
      auto c = integer(value);
      if (c < 0 || c > 0x10ffff)
        throw Fault("invalid source character");
      pending_ += encode(char32_t(c));
    }
    lex_available();
    return;
  }
  if (name == "CARRYO") {
    auto f = pop();
    get(f, Object::Kind::function);
    if (object(f).text == "CHARIN")
      schedule(primitive("@CARRY"), {});
    else
      compile_stream(f, true);
    return;
  }
  if (name == "@CARRY" || name == "@SKIP") {
    while (true) {
      auto t = read_token();
      if (t.kind == Token::Kind::end)
        throw Fault("CARRYON did not find END");
      if (t.kind == Token::Kind::word && t.text == "END") {
        if (name == "@SKIP")
          queue_parser();
        return;
      }
    }
  }
  if (name == "LIBRAR") {
    auto file = pop(), user = pop();
    auto component = [&](Value v) {
      auto &o = object(v);
      if (o.kind != Object::Kind::word && o.kind != Object::Kind::string)
        throw Fault("library name required");
      if (o.text.empty() || o.text.find('/') != std::string::npos ||
          o.text == "." || o.text == "..")
        throw Fault("invalid library name");
      return o.text;
    };
    auto relative =
        std::filesystem::path(component(user)) / (component(file) + ".pop2");
    for (auto &directory : options.library_paths) {
      auto path = std::filesystem::path(directory) / relative;
      std::ifstream source(path);
      if (source) {
        std::ostringstream text;
        text << source.rdbuf();
        compile_source(text.str(), path.string());
        return;
      }
    }
    throw Fault("source library not found: " + relative.string());
  }
  if (name == "POPMES") {
    auto args = items(pop());
    if (args.empty())
      throw Fault("empty POPMESS descriptor");
    auto mode = get(args[0], Object::Kind::word).text;
    if (mode == "TO" || mode == "CO" || mode == "LPO") {
      push(primitive("CHAROUT"));
      return;
    }
    if (mode == "TI" || mode == "CI") {
      push(primitive("CHARIN"));
      return;
    }
    if (mode == "CLOSE") {
      if (args.size() != 2)
        throw Fault("CLOSE needs a stream");
      auto &o = get(args[1], Object::Kind::function);
      if (!o.stream)
        throw Fault("file stream required");
      o.stream.reset();
      o.closed = true;
      return;
    }
    if (mode == "DI" || mode == "DO") {
      if (args.size() != 2)
        throw Fault("host file descriptor is [DI 'path'] or [DO 'path']");
      auto &path = get(args[1], Object::Kind::string);
      auto stream = std::make_shared<std::fstream>(
          path.text, mode == "DI"
                         ? std::ios::in | std::ios::binary
                         : std::ios::out | std::ios::binary | std::ios::trunc);
      if (!*stream)
        throw Fault("cannot open host file " + path.text);
      auto v = generated(mode == "DI" ? "@FILEI" : "@FILEO");
      object(v).stream = stream;
      push(v);
      return;
    }
    throw Fault("unsupported historical POPMESS transfer " + mode);
  }
  if (name == "@FILEO") {
    if (fn->closed || !fn->stream)
      throw Fault("closed output stream");
    auto v = pop();
    if (std::holds_alternative<Terminator>(v.data)) {
      fn->stream.reset();
      fn->closed = true;
      return;
    }
    auto c = integer(v);
    if (c < 0 || c > 0x10ffff)
      throw Fault("invalid character");
    *fn->stream << encode(char32_t(c));
    if (!*fn->stream)
      throw Fault("host output failed");
    return;
  }
  if (name == "@FILEI") {
    if (fn->closed || !fn->stream)
      throw Fault("closed input stream");
    auto c = fn->stream->get();
    if (c == std::char_traits<char>::eof()) {
      push(Value::termin());
      return;
    }
    std::string bytes(1, char(c));
    auto b = static_cast<unsigned char>(c);
    unsigned count = b < 128              ? 0
                     : (b & 0xe0) == 0xc0 ? 1
                     : (b & 0xf0) == 0xe0 ? 2
                     : (b & 0xf8) == 0xf0 ? 3
                                          : 0;
    for (unsigned j = 0; j < count; ++j) {
      c = fn->stream->get();
      if (c == std::char_traits<char>::eof())
        throw Fault("truncated UTF-8 file");
      bytes += char(c);
    }
    push(Value(std::int64_t(decode(bytes).at(0))));
    return;
  }
  if (name == "POPDAT") {
    auto now = options.wall_clock ? options.wall_clock()
                                  : std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm calendar = *std::localtime(&time);
    char date[16];
    std::strftime(date, sizeof date, "%d.%m.%y", &calendar);
    push(string(date));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch())
                  .count() %
              1000;
    push(Value(std::int64_t(
        (calendar.tm_hour * 3600 + calendar.tm_min * 60 + calendar.tm_sec) *
            50 +
        ms / 20)));
    return;
  }
  if (name == "@NEXT") {
    if (!parser && peek_source().kind == Token::Kind::end) {
      if (!sources.empty())
        restore_source();
      return;
    }
    if (!parser)
      parser = std::make_shared<Compiler>(*this);
    if (parser->macro_pending)
      parser->macro_done();
    if (!parser->advance()) {
      auto macro = parser->macro_call;
      schedule(primitive("@NEXT"), {});
      schedule(macro, {});
      return;
    }
    auto p = parser->result();
    parser.reset();
    schedule(primitive("@NEXT"), {});
    if (p)
      queue(p);
    return;
  }
  if (name == "ITEMRE") {
    auto t = read_token(true);
    if (t.kind == Token::Kind::word && t.text == "NONMAC") {
      schedule(primitive("@RAW"), {});
      return;
    }
    if (t.kind == Token::Kind::word && macros.count(t.text)) {
      schedule(binding("ITEMREAD"), {});
      schedule(primitive("@IMAC"), {});
      schedule(macros.at(t.text), {});
      return;
    }
    push(token_value(t));
    return;
  }
  if (name == "@RAW") {
    push(read_item());
    return;
  }
  if (name == "@IMAC") {
    prepend_tokens(macro_expansion);
    macro_expansion.clear();
    return;
  }
  if (name == "MACRES") {
    macro_expansion = text_tokens(pop(), "<macro>");
    macro_expansion.pop_back();
    return;
  }
  if (name == "@GOON") {
    if (!sources.empty()) {
      bind("PROGLIST", Value::nil());
      bind("NEXTCH", Value::nil());
      frames.back().pc = frames.back().program->code.size();
    }
    return;
  }
  if (name == "POPVAL") {
    auto source = pop();
    if (source.object() && object(source).kind == Object::Kind::string)
      compile_source(object(source).text, "<POPVAL>");
    else {
      // POPVAL advances a private cursor, preserving the supplied list.
      if (!empty_list(source)) {
        auto copy = make(Object::Kind::pair);
        object(copy) = get(source, Object::Kind::pair);
        source = copy;
      }
      compile_tokens({{}});
      bind("PROGLIST", source);
    }
    return;
  }
  if (name == "PR" || name == "PRINT" || name == "PRSTRI") {
    auto v = pop();
    // 07777..10001 checks the built-in character-strip class before output.
    if (name == "PRSTRI" && !character_strip(v))
      throw Fault("PRSTRING requires a character strip");
    print_value(v, name == "PRSTRI");
    if (name == "PRINT")
      push(v);
    return;
  }
  if (name == "NL" || name == "SP") {
    auto n = size(pop());
    output_text(std::string(n, name == "NL" ? '\n' : ' '));
    return;
  }
  if (name == "CHAROU" || name == "CUCHOU") {
    auto n = integer(pop());
    if (n < 0 || n > 0x10ffff)
      throw Fault("invalid character");
    out << encode(char32_t(n));
    return;
  }
  if (name == "PRREAL") {
    auto precision = integer(pop());
    auto width = pop();
    auto v = pop();
    if (precision < 0 || precision > 1000)
      throw Fault("invalid real precision");
    std::ostringstream s;
    s.imbue(std::locale::classic());
    if (!v.real())
      throw Fault("PRREAL requires a real");
    if (width.object()) {
      if (object(width).kind != Object::Kind::word ||
          object(width).text != "EXP")
        throw Fault("PRREAL format must be an integer or EXP");
      if (!precision)
        throw Fault("exponential precision must be positive");
      s << std::scientific << std::setprecision(int(precision - 1));
    } else {
      auto before = integer(width);
      if (before < 0 || before > 1000)
        throw Fault("invalid real field width");
      s << std::fixed
        << std::setw(int(before + (precision ? precision + 1 : 0)))
        << std::setprecision(int(precision));
    }
    s << real(v);
    output_text(s.str());
    return;
  }
  if (name == "PRBIN" || name == "PROCT") {
    auto digits = size(pop());
    if (digits > 64)
      throw Fault("invalid digit count");
    auto n = std::uint64_t(integer(pop()));
    unsigned base = name == "PRBIN" ? 2 : 8;
    std::string s;
    do {
      s += char('0' + n % base);
      n /= base;
    } while (n);
    s.resize(digits, '0');
    std::reverse(s.begin(), s.end());
    output_text(s);
    return;
  }
  if (name == "POPTIM") {
    push(Value(options.elapsed ? options.elapsed()
                               : std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - started)
                                     .count()));
    return;
  }
  if (name == "COREUS") {
    std::size_t used = cells.size() * sizeof(Value);
    for (auto &[id, o] : objects) {
      static_cast<void>(id);
      used += sizeof(Object) + o->fields.capacity() * sizeof(Value) +
              o->text.capacity();
    }
    push(Value(std::int64_t(0)));
    push(Value(std::int64_t(used)));
    return;
  }
  if (name == "JUMPOU")
    throw Fault("historical monitor handoff is unavailable in standalone mode");
  if (name == "CODIPC" || name == "CODPIC" || name == "CODIPS" ||
      name == "CODPIS")
    throw Fault("historical character-code conversion is unavailable; use "
                "Unicode scalar values");
  if (name == "@ERRDN") {
    handling_error = false;
    stack.clear();
    bind("CUCHOUT", binding("CHAROUT"));
    initialize_source();
    queue_parser();
    return;
  }
  if (name == "ERRFUN") {
    auto code = integer(pop());
    auto detail = pop();
    if (handling_error)
      errors << error_location.file << ':' << error_location.line << ':'
             << error_location.column << ": " << error_message << '\n';
    else
      diagnostic(format(detail, true) + " (error " + std::to_string(code) +
                 ")");
    return;
  }
  throw Fault("unimplemented primitive " + name);
}
} // namespace pop
