#include "pop/runtime.hpp"
#include <algorithm>
#include <limits>
#include <sstream>
#include <unordered_set>
namespace pop {
Runtime::Runtime(std::istream &i, std::ostream &o, std::ostream &e,
                 Options opts)
    : in(i), out(o), errors(e), options(std::move(opts)) {
  // Handle 1 is the interned NIL word, shared by every empty static list.
  auto nil = make(Object::Kind::word);
  object(nil).text = "NIL";
  words_["NIL"] = nil;
  operators = {{"=", 30},  {"/=", 30}, {"<", 30},  {">", 30},      {"=<", 30},
               {">=", 30}, {"::", 40}, {"<>", 40}, {"FNCOMP", 40}, {"+", 50},
               {"-", 50},  {"*", 60},  {"/", 60},  {"//", 60},     {"!", 70}};
  register_primitives();
  empty_source_function = primitive("@EOF");
  bind("@NEG", primitive("@NEG"));
  bind("TRUE", Value(std::int64_t(1)));
  bind("FALSE", Value(std::int64_t(0)));
  bind("NIL", Value::nil());
  bind("PROGLIST", Value::nil());
  bind("NEXTCH", Value::nil());
  bind("UNDEF", {});
  bind("NTERM", {});
  bind("TERMIN", Value::termin());
  for (auto name :
       {"REAL", "INTEGER", "WORD", "STRIP", "CSTRIP", "PAIR", "REF", "SYNTAX"})
    bind(name, word(name));
  bind("NUMERR", Value(std::int64_t(0)));
  bind("INDEC", Value(std::int64_t(0)));
  // Resident function identifiers have class 040 in the original dictionary.
  // CUCHIN, CUCHOUT and ERRFUN have the mutable variable class 001.
  for (const auto &[name, value] : words_) {
    auto symbol = object(value).binding_record;
    if (!symbol.object() || name == "CUCHIN" || name == "CUCHOU" ||
        name == "ERRFUN" || name.front() == '@')
      continue;
    auto bound = binding(symbol);
    if (bound.object() && object(bound).kind == Object::Kind::function)
      object(symbol).protected_cell = cell(symbol);
  }
  for (auto name : {"TRUE", "FALSE", "NIL", "UNDEF", "TERMIN"}) {
    auto symbol = identifier(name);
    object(symbol).protected_cell = cell(symbol);
  }

}
Value Runtime::make(Object::Kind kind) {
  if (next_ == std::numeric_limits<Handle>::max())
    throw Fault("object identifier exhausted");
  auto id = next_++;
  objects[id] = std::make_unique<Object>(kind);
  return Value::object(id);
}
Object &Runtime::object(Value v) {
  if (!v.object() || !objects.count(v.handle()))
    throw Fault("compound value required");
  return *objects.at(v.handle());
}
const Object &Runtime::object(Value v) const {
  if (!v.object() || !objects.count(v.handle()))
    throw Fault("compound value required");
  return *objects.at(v.handle());
}
Value Runtime::word(const std::string &name) {
  auto key = canonical(name);
  auto it = words_.find(key);
  if (it != words_.end())
    return it->second;
  auto v = make(Object::Kind::word);
  object(v).text = key;
  words_[key] = v;
  return v;
}
Value Runtime::string(const std::string &s) {
  auto v = make(Object::Kind::string);
  object(v).text = s;
  return v;
}
Value Runtime::pair(Value a, Value b) {
  auto v = make(Object::Kind::pair);
  object(v).fields = {a, b};
  return v;
}
Value Runtime::list(const std::vector<Value> &vs) {
  Value v = Value::nil();
  for (auto it = vs.rbegin(); it != vs.rend(); ++it)
    v = pair(*it, v);
  return v;
}
std::vector<Value> Runtime::items(Value v) const {
  std::vector<Value> result;
  std::set<Handle> seen;
  while (!empty_list(v)) {
    auto &o = object(v);
    if (o.kind != Object::Kind::pair || !seen.insert(v.handle()).second)
      throw Fault("finite proper list required");
    result.push_back(o.fields.at(0));
    v = o.fields.at(1);
  }
  return result;
}
std::vector<Value> Runtime::data_fields(Value value) const {
  const auto &o = object(value);
  if (o.kind == Object::Kind::reference && o.referent.object())
    return {cells.at(active.at(o.referent.handle()))};
  return o.fields;
}
std::vector<Value> Runtime::data_components(Value value) const {
  if (value.object() && object(value).source_forward.object())
    empty_list(value);
  const auto &o = object(value);
  // Manual sections 7.4 and 7.7: words expose characters, partials expose
  // frozen arguments only. APPDATA uses the same snapshot as DATALIST (7.3).
  if (o.kind == Object::Kind::string || o.kind == Object::Kind::word) {
    std::vector<Value> result;
    for (auto c : decode(o.text))
      result.push_back(Value(std::int64_t(c)));
    return result;
  }
  if (o.kind == Object::Kind::function &&
      o.function_kind == Object::FunctionKind::partial)
    return {o.fields.begin(), o.fields.end() - 1};
  return data_fields(value);
}
bool Runtime::empty_list(Value v) const {
  std::vector<Value> pending;
  std::set<Handle> seen;
  auto current = v;
  while (current.object() && object(current).source_forward.object()) {
    if (!seen.insert(current.handle()).second)
      throw Fault("cyclic source cursor");
    pending.push_back(current);
    current = object(current).source_forward;
  }
  bool empty = current.nil_value();
  if (!empty && current.object() &&
      object(current).kind == Object::Kind::pair) {
    const auto &node = object(current);
    const auto tail = node.fields.at(1);
    if (tail.object() && object(tail).kind == Object::Kind::function) {
      empty = !node.fields.at(0).truth();
      if (!empty) {
        if (node.forcing)
          throw Fault("recursive forcing of a dynamic list");
        throw NeedList{current};
      }
    }
  }
  if (!pending.empty() && !current.nil_value() &&
      (!current.object() || object(current).kind != Object::Kind::pair))
    throw Fault("source tail requires a list");
  // GETHD preserves the current pair's identity, replacing its components.
  // A pending copy delays host input, but every list observer resolves it.
  for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
    auto &node = *objects.at(it->handle());
    if (current.nil_value()) {
      node.fields = {Value(std::int64_t(0)), empty_source_function};
      node.source_token.reset();
    } else {
      node.fields = object(current).fields;
      node.source_token = object(current).source_token;
    }
    node.source_forward = {};
    current = *it;
  }
  return empty;
}
void Runtime::force_list(Value v) {
  auto &p = object(v);
  auto continuation = primitive("@FORCD");
  object(continuation).fields = {v, p.fields.at(1)};
  p.forcing = true;
  schedule(continuation, {});
  schedule(p.fields.at(1), {});
}
Value Runtime::function(std::shared_ptr<Program> p) {
  for (const auto &names : {p->args, p->locals, p->results})
    for (const auto &name : names)
      p->symbols.emplace(name, identifier(name));
  auto v = make(Object::Kind::function);
  object(v).program = std::move(p);
  return v;
}
Value Runtime::primitive(const std::string &name) {
  auto v = make(Object::Kind::function);
  object(v).function_kind = Object::FunctionKind::primitive;
  object(v).text = canonical(name);
  return v;
}
bool Runtime::has_updater(Value v) const {
  std::set<Handle> seen;
  while (v.object() && seen.insert(v.handle()).second) {
    const auto &f = object(v);
    if (f.kind != Object::Kind::function)
      return false;
    if (f.updater_assigned || f.updater.object())
      return f.updater.object() &&
             object(f.updater).kind == Object::Kind::function;
    if (f.function_kind == Object::FunctionKind::array) {
      v = f.fields[1];
      continue;
    }
    if (f.function_kind != Object::FunctionKind::primitive)
      return false;
    static const std::set<std::string> paired = {
        "CONT",   "FRONT",  "BACK",   "HD",     "TL",     "SUBSCR",
        "SUBSCC", "DATAWO", "BOUNDS", "FNPART", "FROZVA", "FNPROP",
        "UPDATE", "MEANIN", "VALOF",  "@FIELD", "@SGET"};
    return paired.count(f.text) != 0;
  }
  return false;
}
Value Runtime::updater(Value value) {
  auto &f = object(value);
  if (!f.updater_assigned && !f.updater.object() && has_updater(value)) {
    auto wrapper = primitive("@UPDAT");
    object(wrapper).fields = {value};
    f.updater = wrapper;
  }
  return f.updater;
}
Value Runtime::partial(Value function, const std::vector<Value> &frozen) {
  auto value = make(Object::Kind::function);
  auto &f = object(value);
  f.function_kind = Object::FunctionKind::partial;
  f.fields = frozen;
  f.fields.push_back(function);
  f.updater_assigned = true;
  // The paired partial captures its own frozen components at creation.
  // Later FROZVAL/FNPART updates of either function remain independent.
  if (has_updater(function)) {
    auto paired = make(Object::Kind::function);
    auto &u = object(paired);
    u.function_kind = Object::FunctionKind::partial;
    u.fields = frozen;
    u.fields.push_back(updater(function));
    u.updater_assigned = true;
    f.updater = paired;
  }
  return value;
}
Value Runtime::identifier(const std::string &name) {
  return identifier(word(name));
}
Value Runtime::identifier(Value value) {
  auto &w = object(value);
  if (w.kind == Object::Kind::identifier)
    return value;
  if (w.kind != Object::Kind::word)
    throw Fault("word required");
  if (!w.binding_record.object()) {
    w.binding_record = make(Object::Kind::identifier);
    object(w.binding_record).text = w.text;
  }
  return w.binding_record;
}
Handle Runtime::cell(const std::string &name) { return cell(identifier(name)); }
Handle Runtime::cell(Value value) {
  auto symbol = identifier(value);
  auto found = active.find(symbol.handle());
  if (found != active.end())
    return found->second;
  auto id = next_++;
  cells[id] = {};
  active[symbol.handle()] = id;
  return id;
}
Value Runtime::binding(const std::string &s) { return cells.at(cell(s)); }
void Runtime::bind(const std::string &s, Value v) { cells[cell(s)] = v; }
Value Runtime::binding(Value symbol) { return cells.at(cell(symbol)); }
void Runtime::bind(Value symbol, Value v) { cells[cell(symbol)] = v; }
bool Runtime::protected_binding(Value symbol) {
  symbol = identifier(symbol);
  const auto protected_cell = object(symbol).protected_cell;
  return protected_cell && cell(symbol) == protected_cell;
}
void Runtime::assign(Value symbol, Value value) {
  if (protected_binding(symbol))
    throw Fault("assignment to protected identifier");
  bind(symbol, value);
}
void Runtime::cancel_word(Value symbol) {
  const auto name = object(symbol).text;
  auto found = words_.find(name);
  if (found != words_.end() &&
      equal(object(found->second).binding_record, symbol)) {
    object(found->second).binding_record = {};
    operators.erase(name);
    macros.erase(name);
  }
}
Value Runtime::pop() {
  if (stack.empty())
    throw Fault("value stack underflow");
  auto v = stack.back();
  stack.pop_back();
  return v;
}
void Runtime::schedule(Value f, const std::vector<Value> &args, bool update) {
  auto p = std::make_shared<Program>();
  for (auto v : args)
    p->code.push_back({Operation::Kind::push, v, {}, {}, {}});
  p->code.push_back({Operation::Kind::push, f, {}, {}, {}});
  p->code.push_back(
      {Operation::Kind::call_value, {}, {}, update ? 1u : 0u, {}});
  frames.push_back({p, 0, {}, {}, {}, {}, {}});
}
void Runtime::call(Value v, bool update) {
  auto &f = object(v);
  if (f.kind != Object::Kind::function)
    throw Fault("function required");
  if (update && (f.updater_assigned || f.updater.object())) {
    if (!f.updater.object() ||
        object(f.updater).kind != Object::Kind::function)
      throw Fault("function has no callable updater");
    call(f.updater);
    return;
  }
  // Update calls must never fall through to an ordinary primitive invocation.
  // This also prevents output/constructor/destructor side effects on failure.
  if (update && !has_updater(v))
    throw Fault("function has no callable updater");
  if (f.function_kind != Object::FunctionKind::user) {
    auto arguments = stack;
    try {
      invoke(f.text, &f, update);
    } catch (const NeedList &need) {
      stack = std::move(arguments);
      schedule(v, {}, update);
      force_list(need.value);
    }
    return;
  }
  if (options.max_frames && frames.size() >= options.max_frames + 1)
    throw Fault("native frame limit exceeded");
  if (stack.size() < f.program->args.size())
    throw Fault("value stack underflow");
  Frame frame{f.program, 0, v, f.environment, {}, {}, {}};
  auto install = [&](Handle symbol, Handle id) {
    frame.saved.emplace_back(symbol, cell(Value::object(symbol)));
    active[symbol] = id;
  };
  for (auto &[name, id] : f.environment)
    install(name, id);
  std::vector<std::string> locals = f.program->locals;
  locals.insert(locals.end(), f.program->results.begin(),
                f.program->results.end());
  for (auto &name : locals) {
    auto id = next_++;
    cells[id] = {};
    auto symbol = f.program->symbols.at(name).handle();
    frame.environment[symbol] = id;
    install(symbol, id);
  }
  for (auto it = f.program->args.rbegin(); it != f.program->args.rend(); ++it) {
    auto value = pop();
    auto id = next_++;
    cells[id] = value;
    auto symbol = f.program->symbols.at(*it).handle();
    frame.environment[symbol] = id;
    install(symbol, id);
  }
  frames.push_back(std::move(frame));
  max_depth = std::max(max_depth, frames.size() - 1);
}
bool Runtime::execute(std::shared_ptr<Program> p) {
  frames.push_back({std::move(p), 0, {}, {}, {}, {}, {}});
  return run();
}
bool Runtime::run(std::size_t stop_depth) {
  Location location;
  while (frames.size() > stop_depth) {
    try {
      if (operations && operations % 4096 == 0)
        collect();
      auto &f = frames.back();
      if (f.pc >= f.program->code.size()) {
        for (auto &name : f.program->results)
          push(binding(f.program->symbols.at(name)));
        for (auto it = f.saved.rbegin(); it != f.saved.rend(); ++it)
          active[it->first] = it->second;
        frames.pop_back();
        continue;
      }
      auto op = f.program->code[f.pc++];
      location = op.where;
      if (options.max_operations && operations >= options.max_operations)
        throw Fault("operation limit exceeded");
      ++operations;
      switch (op.kind) {
      case Operation::Kind::push:
        push(op.value);
        break;
      case Operation::Kind::save_callable:
        f.callees.push_back(pop());
        break;
      case Operation::Kind::call_saved: {
        if (f.callees.empty())
          throw Fault("missing computed function");
        auto callee = f.callees.back();
        // Do not discard the root until a potentially suspending call succeeds.
        const auto frame_index = frames.size() - 1;
        call(callee);
        frames.at(frame_index).callees.pop_back();
        break;
      }
      case Operation::Kind::load:
        push(binding(op.symbol));
        break;
      case Operation::Kind::store:
        assign(op.symbol, pop());
        if (macros.count(op.name) && words_.count(op.name) &&
            equal(object(words_.at(op.name)).binding_record, op.symbol))
          macros[op.name] = binding(op.symbol);
        break;
      case Operation::Kind::call:
        call(binding(op.symbol));
        break;
      case Operation::Kind::call_value: {
        auto callee = pop();
        try {
          call(callee, op.target != 0);
        } catch (const NeedInput &) {
          push(callee);
          throw;
        }
        break;
      }
      case Operation::Kind::update:
        call(binding(op.symbol), true);
        break;
      case Operation::Kind::closure: {
        auto v = function(object(op.value).program);
        object(v).environment = f.environment;
        object(v).macro = object(op.value).macro;
        push(v);
        break;
      }
      case Operation::Kind::define: {
        auto v = pop();
        assign(op.symbol, v);
        if (op.target)
          macros[op.name] = v;
        break;
      }
      case Operation::Kind::ret:
        f.pc = f.program->code.size();
        break;
      case Operation::Kind::jump:
        f.pc = op.target;
        break;
      case Operation::Kind::branch_false:
        if (!pop().truth())
          f.pc = op.target;
        break;
      case Operation::Kind::branch_true:
        if (pop().truth())
          f.pc = op.target;
        break;
      case Operation::Kind::label:
        break;
      case Operation::Kind::go:
        throw Fault("unresolved label");
      case Operation::Kind::cancel:
        cancel_word(op.symbol);
        break;
      case Operation::Kind::declare:
        cell(op.symbol);
        if (op.target)
          macros[op.name] = binding(op.symbol);
        break;
      case Operation::Kind::print: {
        const auto begin =
            f.program->function && !stack.empty() ? stack.size() - 1 : 0;
        std::string text = "**";
        for (std::size_t i = begin; i < stack.size(); ++i) {
          if (i != begin)
            text += ", ";
          else
            text += " ";
          text += format(stack[i]);
        }
        text += '\n';
        stack.resize(begin);
        output_text(text);
        break;
      }
      case Operation::Kind::begin_list:
        f.list_bases.push_back(stack.size());
        break;
      case Operation::Kind::end_list: {
        if (f.list_bases.empty())
          throw Fault("missing list construction marker");
        auto n = f.list_bases.back();
        f.list_bases.pop_back();
        if (stack.size() < n)
          throw Fault("list construction consumed preceding values");
        std::vector<Value> vs(stack.begin() + static_cast<std::ptrdiff_t>(n),
                              stack.end());
        stack.resize(n);
        push(list(vs));
        break;
      }
      case Operation::Kind::for_setup: {
        auto limit = pop(), step = pop(), start = pop();
        real(limit);
        real(start);
        if (compare(step, Value(std::int64_t(0))) == 0)
          throw Fault("zero loop increment");
        bind(op.symbol, start);
        f.loops.push_back({op.symbol, step, limit});
        break;
      }
      case Operation::Kind::for_test: {
        if (f.loops.empty())
          throw Fault("missing loop context");
        auto &l = f.loops.back();
        auto n = binding(l.symbol);
        if (real(l.step) > 0 ? compare(n, l.limit) > 0
                             : compare(n, l.limit) < 0) {
          f.pc = op.target;
          f.loops.pop_back();
        }
        break;
      }
      case Operation::Kind::for_next: {
        if (f.loops.empty())
          throw Fault("missing loop context");
        auto &l = f.loops.back();
        auto before = binding(l.symbol);
        auto after = arithmetic("+", before, l.step);
        if (compare(before, after) == 0)
          throw Fault("loop increment makes no progress");
        bind(l.symbol, after);
        f.pc = op.target;
        break;
      }
      case Operation::Kind::partial:
        throw Fault("invalid partial operation");
      }
      if (resetting) {
        resetting = false;
        handling_error = false;
        unwind(false);
        parser.reset();
        bind("CUCHOUT", binding("CHAROUT"));
        stack.clear();
        out << "SETPOP\n";
        initialize_source();
        queue_parser();
        continue;
      }
    } catch (const NeedList &need) {
      --frames.back().pc;
      force_list(need.value);
    } catch (const NeedInput &) {
      if (operations)
        --operations;
      if (!frames.empty() && frames.back().pc)
        --frames.back().pc;
      if (source_supplier.object()) {
        schedule(primitive("@FEED"), {});
        schedule(source_supplier, {});
        continue;
      }
      throw;
    } catch (const Fault &e) {
      location = e.where.value_or(location);
      if (handling_error ||
          (options.max_operations && operations >= options.max_operations)) {
        diagnostic(handling_error
                       ? "error during ERRFUN: " + std::string(e.what())
                       : e.what(),
                   location);
        handling_error = false;
        unwind(false);
        parser.reset();
        stack.clear();
        bind("CUCHOUT", binding("CHAROUT"));
        initialize_source();
        return false;
      }
      ++diagnostics;
      error_message = e.what();
      error_location = location;
      auto handler = binding("ERRFUN");
      const bool compiler_failed = bool(parser);
      unwind(false);
      parser.reset();
      if (compiler_failed && input_position < input.size()) {
        auto line = input[input_position].where.line;
        while (input_position < input.size() &&
               input[input_position].kind != Token::Kind::end &&
               input[input_position].where.line == line)
          ++input_position;
      }
      stack.clear();
      bind("CUCHOUT", binding("CHAROUT"));
      bind("NUMERR", Value(std::int64_t(1)));
      initialize_source();
      handling_error = true;
      schedule(primitive("@ERRDN"), {});
      schedule(handler, {string(error_message), Value(std::int64_t(1))});
    }
  }
  return true;
}
void Runtime::unwind(bool restore_bindings) {
  for (auto &[id, o] : objects)
    o->forcing = false;
  while (!sources.empty())
    restore_source();
  while (!frames.empty()) {
    if (restore_bindings)
      for (auto it = frames.back().saved.rbegin();
           it != frames.back().saved.rend(); ++it)
        active[it->first] = it->second;
    frames.pop_back();
  }
}
void Runtime::output_text(const std::string &text) {
  auto consumer = binding("CUCHOUT");
  auto &o = object(consumer);
  if (o.function_kind == Object::FunctionKind::primitive &&
      o.text == "CUCHOU") {
    out << text;
    return;
  }
  auto p = std::make_shared<Program>();
  for (auto c : decode(text)) {
    p->code.push_back(
        {Operation::Kind::push, Value(std::int64_t(c)), {}, {}, {}});
    p->code.push_back({Operation::Kind::push, consumer, {}, {}, {}});
    p->code.push_back({Operation::Kind::call_value, {}, {}, {}, {}});
  }
  frames.push_back({p, 0, {}, {}, {}, {}, {}});
}
void Runtime::diagnostic(const std::string &message, Location loc) {
  ++diagnostics;
  errors << loc.file << ':' << loc.line << ':' << loc.column << ": " << message
         << '\n';
}
void Runtime::queue_parser() { schedule(primitive("@NEXT"), {}); }
void Runtime::lex_available() {
  if (input.empty())
    input.push_back({Token::Kind::end, "", lex_location_});
  while (!pending_.empty()) {
    std::vector<Token> tokens;
    std::size_t complete = 0;
    std::string decoding_error;
    bool incomplete_utf8 = false;
    try {
      decode(pending_, &complete);
    } catch (const NeedInput &) {
      incomplete_utf8 = true;
      if (input_eof)
        decoding_error = "truncated UTF-8 input";
    } catch (const Fault &error) {
      decoding_error = error.what();
    }
    auto fail_decoding = [&] {
      if (!decoding_error.empty()) {
        lex_failure_ = decoding_error;
        pending_.clear();
      }
    };
    // Tokenize the valid prefix without inventing an EOF at the bad byte.
    // Complete earlier tokens reach the VM before the deferred reader error.
    const auto available = pending_.substr(0, complete);
    try {
      tokens = tokenize(available, pending_name_,
                        input_eof && !incomplete_utf8 && decoding_error.empty(), 1);
    } catch (const NeedInput &) {
      fail_decoding();
      break;
    } catch (const Fault &error) {
      lex_failure_ = error.what();
      pending_.clear();
      break;
    }
    auto chars = decode(available);
    auto end = tokens.back().where;
    std::size_t offset = 0, line = 1, column = 1, bytes = 0;
    while (offset < chars.size() &&
           (line < end.line || (line == end.line && column < end.column))) {
      auto c = chars[offset++];
      bytes += encode(c).size();
      if (c == '\n') {
        ++line;
        column = 1;
      } else
        ++column;
    }
    if (!bytes) {
      fail_decoding();
      break;
    }
    if (tokens.size() > 1) {
      auto t = tokens.front();
      if (t.where.line == 1)
        t.where.column += lex_location_.column - 1;
      t.where.line += lex_location_.line - 1;
      input.insert(input.end() - 1, std::move(t));
    }
    if (line == 1)
      lex_location_.column += column - 1;
    else {
      lex_location_.line += line - 1;
      lex_location_.column = column;
    }
    pending_.erase(0, bytes);
    input.back().where = lex_location_;
  }
}
Runtime::RunResult Runtime::feed(const std::string &source_id,
                                 const std::string &bytes, bool eof) {
  const auto before = diagnostics;
  try {
    if (!session_active_) {
      pending_.clear();
      lex_failure_.clear();
      pending_name_ = source_id;
      lex_location_ = {source_id, 1, 1};
      input = {{Token::Kind::end, "", lex_location_}};
      input_position = 0;
      parser.reset();
      initialize_source();
      session_active_ = true;
      queue_parser();
    } else if (source_id != pending_name_)
      throw Fault("input belongs to a different active source");
    input_eof = eof;
    pending_ += bytes;
    lex_available();
    auto result = resume();
    result.diagnostics_added = diagnostics - before;
    return result;
  } catch (const Fault &error) {
    diagnostic(error.what(), lex_location_);
    unwind();
    parser.reset();
    stack.clear();
    pending_.clear();
    session_active_ = false;
    return {SessionState::failed, diagnostics - before};
  }
}
Runtime::RunResult Runtime::resume() {
  const auto before = diagnostics;
  while (true) {
    try {
      bool ok = run();
      if (!ok) {
        bool compiler_failed = bool(parser);
        parser.reset();
        if (options.max_operations && operations >= options.max_operations) {
          session_active_ = false;
          return {SessionState::failed, diagnostics - before};
        }
        if (compiler_failed && input_position < input.size()) {
          auto line = input[input_position].where.line;
          while (input_position < input.size() &&
                 input[input_position].kind != Token::Kind::end &&
                 input[input_position].where.line == line)
            ++input_position;
          initialize_source();
        }
      }
      if (!ok) {
        queue_parser();
        continue;
      }
      session_active_ = false;
      collect();
      return {diagnostics == before ? SessionState::finished
                                    : SessionState::failed,
              diagnostics - before};
    } catch (const NeedInput &) {
      return {SessionState::need_input, diagnostics - before};
    }
  }
}
bool Runtime::source(const std::string &text, const std::string &name,
                     bool eof) {
  auto result = feed(name, text, eof);
  return result.state != SessionState::failed && result.diagnostics_added == 0;
}
void Runtime::initialize_source() {
  bind("PROGLIST", pair(Value(std::int64_t(1)), primitive("@PHYS")));
  bind("NEXTCH", Value::nil());
}
Token Runtime::read_physical_token() {
  if (input_position >= input.size() ||
      input[input_position].kind == Token::Kind::end) {
    if (!lex_failure_.empty()) {
      auto message = std::move(lex_failure_);
      lex_failure_.clear();
      throw Fault(message, lex_location_);
    }
    if (!input_eof)
      throw NeedInput{};
    return {};
  }
  return input[input_position++];
}
Token Runtime::peek_source(std::size_t offset) {
  auto cursor = binding("PROGLIST"), chains = binding("NEXTCH");
  for (std::size_t index = 0;;) {
    if (empty_list(cursor)) {
      if (empty_list(chains))
        return {};
      if (!chains.object() || object(chains).kind != Object::Kind::pair)
        throw Fault("NEXTCH requires a list of source lists");
      cursor = object(chains).fields[0];
      chains = object(chains).fields[1];
      if (index == 0 && offset == 0) {
        bind("PROGLIST", cursor);
        bind("NEXTCH", chains);
      }
      continue;
    }
    if (!cursor.object() || object(cursor).kind != Object::Kind::pair)
      throw Fault("PROGLIST requires a list of text items");
    auto &node = object(cursor);
    if (index++ == offset) {
      if (node.source_token &&
          (node.source_token->value
               ? equal(*node.source_token->value, node.fields[0])
               : std::holds_alternative<Undefined>(node.fields[0].data)))
        return *node.source_token;
      auto tokens = text_tokens(list({node.fields[0]}), "<PROGLIST>");
      return tokens.front();
    }
    cursor = node.fields[1];
  }
}
Token Runtime::read_token(bool quote_markers) {
  auto t = peek_source();
  if (t.kind == Token::Kind::end)
    return t;
  auto cursor = binding("PROGLIST");
  object(cursor).source_forward = object(cursor).fields[1];
  if (quote_markers && t.kind == Token::Kind::quoted) {
    prepend_tokens({{Token::Kind::word, canonical(t.text), t.where},
                    {Token::Kind::symbol, "\"", t.where}});
    t.kind = Token::Kind::symbol;
    t.text = "\"";
    t.value = word("\"");
  }
  return t;
}
void Runtime::prepend_tokens(const std::vector<Token> &tokens) {
  auto tail = binding("PROGLIST");
  for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
    if (it->kind == Token::Kind::end)
      continue;
    auto token = *it;
    auto value = token_value(token);
    token.value = value;
    tail = pair(value, tail);
    object(tail).source_token = token;
  }
  bind("PROGLIST", tail);
}
Value Runtime::read_item() { return token_value(read_token(true)); }
Value Runtime::token_value(Token t) {
  if (t.value)
    return *t.value;
  if (t.kind == Token::Kind::end)
    return Value::termin();
  if (t.kind == Token::Kind::string)
    return string(t.text);
  if (t.kind == Token::Kind::number) {
    Compiler c(*this);
    return c.literal(t);
  }
  return word(t.text);
}
std::vector<Token> Runtime::text_tokens(Value v, const std::string &name) {
  std::vector<Token> tokens;
  Location location{name, 1, 1};
  for (auto value : items(v)) {
    if (value.numeric())
      tokens.emplace_back(Token::Kind::number, number_text(value), location,
                          value);
    else if (value.object() && object(value).kind == Object::Kind::string)
      tokens.emplace_back(Token::Kind::string, object(value).text, location,
                          value);
    else if (value.object() && object(value).kind == Object::Kind::word) {
      auto name = object(value).text;
      // Recovered libraries contain padded punctuation words such as "-> ".
      while (!name.empty() && name.back() == ' ')
        name.pop_back();
      while (!name.empty() && name.front() == ' ')
        name.erase(0, 1);
      auto chars = decode(name);
      auto c = chars.empty() ? 0 : chars.front();
      bool alphabetic = (c >= 'A' && c <= 'Z') || c == '_' || c >= 128;
      tokens.emplace_back(alphabetic ? Token::Kind::word : Token::Kind::symbol,
                          name, location);
    } else
      throw Fault("source text requires words, numbers or strings");
    ++location.column;
  }
  tokens.emplace_back(Token::Kind::end, "", location);
  return tokens;
}
void Runtime::compile_source(const std::string &s, const std::string &name) {
  compile_tokens(tokenize(s, name, true));
}
void Runtime::compile_tokens(std::vector<Token> tokens) {
  sources.push_back({std::move(input), input_position, input_eof, parser,
                     source_supplier, pending_, pending_name_, lex_location_,
                     binding("PROGLIST"), binding("NEXTCH"), lex_failure_});
  source_supplier = {};
  lex_failure_.clear();
  parser.reset();
  input_eof = true;
  input = {{}};
  input_position = 0;
  bind("PROGLIST", Value::nil());
  bind("NEXTCH", Value::nil());
  prepend_tokens(tokens);
  schedule(primitive("@NEXT"), {});
}
void Runtime::restore_source() {
  auto previous = std::move(sources.back());
  sources.pop_back();
  input = std::move(previous.tokens);
  input_position = previous.position;
  input_eof = previous.eof;
  parser = std::move(previous.parser);
  source_supplier = previous.supplier;
  pending_ = std::move(previous.pending);
  pending_name_ = std::move(previous.name);
  lex_location_ = std::move(previous.location);
  lex_failure_ = std::move(previous.lex_failure);
  bind("PROGLIST", previous.proglist);
  bind("NEXTCH", previous.nextch);
}
void Runtime::compile_stream(Value supplier, bool skip_to_end) {
  sources.push_back({std::move(input), input_position, input_eof, parser,
                     source_supplier, pending_, pending_name_, lex_location_,
                     binding("PROGLIST"), binding("NEXTCH"), lex_failure_});
  parser.reset();
  source_supplier = supplier;
  lex_failure_.clear();
  input_eof = false;
  input_position = 0;
  pending_.clear();
  pending_name_ = "<COMPILE>";
  lex_location_ = {pending_name_, 1, 1};
  input = {{Token::Kind::end, "", lex_location_}};
  initialize_source();
  schedule(primitive(skip_to_end ? "@SKIP" : "@NEXT"), {});
}
std::string Runtime::format(Value v, bool raw) const {
  struct Task {
    enum Kind { value, tail, text, leave } kind;
    Value item;
    std::string literal;
    bool first = false;
  };
  std::vector<Task> tasks{{Task::value, v, {}, false}};
  std::set<Handle> visiting;
  std::string result;
  while (!tasks.empty()) {
    auto task = std::move(tasks.back());
    tasks.pop_back();
    if (task.kind == Task::text) {
      result += task.literal;
      continue;
    }
    if (task.kind == Task::leave) {
      visiting.erase(task.item.handle());
      continue;
    }
    auto x = task.item;
    if (task.kind == Task::tail) {
      if (empty_list(x))
        continue;
      if (!x.object() || object(x).kind != Object::Kind::pair) {
        result += " | ";
        tasks.push_back({Task::value, x, {}});
      } else if (!visiting.insert(x.handle()).second) {
        result += task.first ? "<cycle>" : " <cycle>";
      } else {
        const auto &node = object(x);
        if (!task.first)
          result += ' ';
        tasks.push_back({Task::leave, x, {}});
        tasks.push_back({Task::tail, node.fields[1], {}, false});
        tasks.push_back({Task::value, node.fields[0], {}});
      }
      continue;
    }
    if (x.numeric()) {
      result += number_text(x);
      continue;
    }
    if (empty_list(x)) {
      result += "[]";
      continue;
    }
    if (std::holds_alternative<Undefined>(x.data)) {
      result += "UNDEF";
      continue;
    }
    if (std::holds_alternative<Terminator>(x.data)) {
      result += "TERMIN";
      continue;
    }
    const auto &o = object(x);
    if (o.kind == Object::Kind::word) {
      result += o.text;
      continue;
    }
    if (o.kind == Object::Kind::string) {
      result += raw ? o.text : "'" + o.text + "'";
      continue;
    }
    if (o.kind == Object::Kind::function) {
      result += "FUNCTION";
      continue;
    }
    if (visiting.count(x.handle())) {
      result += "<cycle>";
      continue;
    }
    if (o.kind == Object::Kind::pair) {
      result += '[';
      tasks.push_back({Task::text, {}, "]"});
      tasks.push_back({Task::tail, x, {}, true});
    } else {
      visiting.insert(x.handle());
      result += o.kind == Object::Kind::reference ? "REF(" : "{";
      tasks.push_back({Task::leave, x, {}});
      tasks.push_back(
          {Task::text, {}, o.kind == Object::Kind::reference ? ")" : "}"});
      auto fields = data_fields(x);
      for (std::size_t i = fields.size(); i-- > 0;) {
        tasks.push_back({Task::value, fields[i], {}});
        if (i)
          tasks.push_back({Task::text, {}, " "});
      }
    }
  }
  return result;
}
void Runtime::collect() {
  std::vector<Value> pending = stack;
  pending.push_back(Value::nil());
  std::set<Handle> marked, marked_cells;
  std::set<const Program *> programs;
  auto mark_cell = [&](Handle id) {
    if (marked_cells.insert(id).second)
      pending.push_back(cells.at(id));
  };
  auto mark_program = [&](const std::shared_ptr<Program> &p) {
    if (p && programs.insert(p.get()).second) {
      for (auto &op : p->code) {
        pending.push_back(op.value);
        pending.push_back(op.symbol);
      }
      for (auto &[name, symbol] : p->symbols)
        pending.push_back(symbol);
    }
  };
  auto mark_tokens = [&](const std::vector<Token> &tokens) {
    for (auto &t : tokens)
      if (t.value)
        pending.push_back(*t.value);
  };
  mark_tokens(input);
  mark_tokens(macro_expansion);
  pending.push_back(source_supplier);
  pending.push_back(empty_source_function);
  for (auto &source : sources) {
    mark_tokens(source.tokens);
    pending.push_back(source.supplier);
    pending.push_back(source.proglist);
    pending.push_back(source.nextch);
  }
  for (auto it = compiling.begin(); it != compiling.end();) {
    if (auto p = it->lock()) {
      mark_program(p);
      ++it;
    } else
      it = compiling.erase(it);
  }
  for (auto &[name, v] : words_) {
    static_cast<void>(name);
    pending.push_back(v);
  }
  for (auto &[name, v] : macros) {
    static_cast<void>(name);
    pending.push_back(v);
  }
  for (auto &f : frames) {
    pending.push_back(f.function);
    pending.insert(pending.end(), f.callees.begin(), f.callees.end());
    mark_program(f.program);
    for (auto &[name, id] : f.environment) {
      pending.push_back(Value::object(name));
      mark_cell(id);
    }
    for (auto &[name, id] : f.saved) {
      pending.push_back(Value::object(name));
      mark_cell(id);
    }
  }
  while (!pending.empty()) {
    auto v = pending.back();
    pending.pop_back();
    if (!v.object() || !marked.insert(v.handle()).second)
      continue;
    auto &o = object(v);
    if (o.kind == Object::Kind::identifier && active.count(v.handle()))
      mark_cell(active.at(v.handle()));
    pending.insert(pending.end(), o.fields.begin(), o.fields.end());
    pending.push_back(o.binding_record);
    pending.push_back(o.referent);
    pending.push_back(o.binding_reference);
    pending.push_back(o.source_forward);
    pending.push_back(o.property);
    pending.push_back(o.updater);
    pending.push_back(o.type);
    mark_program(o.program);
    for (auto &[name, id] : o.environment) {
      pending.push_back(Value::object(name));
      mark_cell(id);
    }
  }
  for (auto it = objects.begin(); it != objects.end();)
    if (!marked.count(it->first))
      it = objects.erase(it);
    else
      ++it;
  for (auto it = active.begin(); it != active.end();)
    if (!marked.count(it->first))
      it = active.erase(it);
    else
      ++it;
  for (auto it = cells.begin(); it != cells.end();)
    if (!marked_cells.count(it->first))
      it = cells.erase(it);
    else
      ++it;
}
} // namespace pop
