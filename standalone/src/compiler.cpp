#include "pop/runtime.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
namespace pop {
Compiler::Compiler(Runtime &r)
    : runtime_(r), program_(std::make_shared<Program>()),
      literal_roots_(std::make_shared<Program>()) {
  runtime_.compiling.push_back(program_);
  runtime_.compiling.push_back(literal_roots_);
  later([this] { start(); });
}
Token Compiler::peek(std::size_t offset) const {
  return runtime_.peek_source(offset);
}
bool Compiler::end() const { return peek().kind == Token::Kind::end; }
Token Compiler::take() {
  auto t = peek();
  if (t.kind == Token::Kind::end)
    throw Fault("unexpected end of input");
  runtime_.read_token();
  location_ = t.where;
  return t;
}
bool Compiler::at(const std::string &s) const {
  const auto t = peek();
  return (t.kind == Token::Kind::word || t.kind == Token::Kind::symbol) &&
         t.text == s;
}
bool Compiler::accept(const std::string &s) {
  if (!at(s))
    return false;
  take();
  return true;
}
void Compiler::expect(const std::string &s) {
  if (!accept(s))
    throw Fault("expected " + s + ", got " +
                (end() ? "end of input" : peek().text));
}
void Compiler::emit(Operation::Kind kind, std::string name, Value value,
                    std::size_t target) {
  Value symbol;
  if (!name.empty()) {
    auto [it, inserted] =
        program_->symbols.emplace(name, runtime_.identifier(name));
    symbol = it->second;
  }
  if (symbol.object() &&
      (kind == Operation::Kind::store || kind == Operation::Kind::define ||
       kind == Operation::Kind::declare || kind == Operation::Kind::for_setup)) {
    auto local = [&](const std::shared_ptr<Program> &scope) {
      auto contains = [&](const std::vector<std::string> &names) {
        return std::find(names.begin(), names.end(), name) != names.end();
      };
      return scope->function && (contains(scope->args) ||
             contains(scope->locals) || contains(scope->results));
    };
    bool shadowed = local(program_);
    for (const auto &scope : scopes_)
      shadowed = shadowed || local(scope);
    if (!shadowed && runtime_.protected_binding(symbol))
      throw Fault(kind == Operation::Kind::declare
                      ? "declaration of protected identifier"
                      : "assignment to protected identifier");
  }
  program_->code.push_back(
      {kind, value, std::move(name), target, location_, symbol});
}
Value Compiler::literal(Token token) {
  if (token.value)
    return *token.value;
  if (token.kind == Token::Kind::string)
    return runtime_.string(token.text);
  if (token.kind != Token::Kind::number)
    return runtime_.word(token.text);
  auto text = token.text;
  if (text.find_first_of(".e") != std::string::npos) {
    double n = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(n))
      throw Fault("real literal out of range");
    return Value(n);
  }
  int base = 10;
  if (text.size() > 2 && text[1] == ':') {
    base = text[0] - '0';
    text.erase(0, 2);
  }
  std::int64_t n = 0;
  auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), n, base);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw Fault("integer literal out of range");
  return Value(n);
}
bool Compiler::advance() {
  while (!tasks_.empty() && !macro_pending) {
    auto task = tasks_.back();
    tasks_.pop_back();
    try {
      task();
    } catch (const NeedInput &) {
      tasks_.push_back(std::move(task));
      throw;
    } catch (const NeedList &) {
      tasks_.push_back(std::move(task));
      throw;
    } catch (const Fault &error) {
      throw Fault(error.what(), error.where
                                    ? error.where
                                    : std::optional<Location>(location_));
    }
  }
  return tasks_.empty() && !macro_pending;
}
std::shared_ptr<Program> Compiler::statement() {
  if (!advance())
    throw Fault("macro requires runtime continuation");
  return result_;
}
bool Compiler::macro(std::function<void()> continuation) {
  if (peek().kind != Token::Kind::word || !runtime_.macros.count(peek().text))
    return false;
  macro_call = runtime_.macros.at(take().text);
  macro_pending = true;
  later(std::move(continuation));
  return true;
}
void Compiler::macro_done() {
  auto expanded = std::move(runtime_.macro_expansion);
  runtime_.macro_expansion.clear();
  if (expanded.empty())
    expanded.push_back({Token::Kind::symbol, "@EMPTY", location_});
  runtime_.prepend_tokens(expanded);
  macro_pending = false;
}
void Compiler::start() {
  if (accept(";") || accept(",")) {
    later([this] { start(); });
    return;
  }
  if (end())
    return;
  later([this] { statement_tail(); });
  later([this] { imperative(); });
}
void Compiler::statement_tail() {
  // A print arrow is itself an imperative separator; it needs no lookahead.
  if (!program_->code.empty() &&
      program_->code.back().kind == Operation::Kind::print) {
    resolve();
    result_ = program_;
    return;
  }
  if (accept(",")) {
    later([this] { statement_tail(); });
    later([this] { imperative(); });
    return;
  }
  accept(";");
  resolve();
  result_ = program_;
}
void Compiler::sequence(std::set<std::string> stops) {
  if (end()) {
    if (!stops.empty())
      throw Fault("unexpected end of input");
    return;
  }
  for (const auto &s : stops)
    if (at(s))
      return;
  if (accept(";") || accept(",")) {
    later([this, stops] { sequence(stops); });
    return;
  }
  later([this, stops] { sequence(stops); });
  later([this] { imperative(); });
}
void Compiler::expression(int minimum) {
  later([this, minimum] { binary(minimum); });
  later([this] { primary(); });
}
void Compiler::binary(int minimum) {
  const auto &t = peek();
  if (t.kind != Token::Kind::word && t.kind != Token::Kind::symbol)
    return;
  auto found = runtime_.operators.find(t.text);
  if (found == runtime_.operators.end() || found->second < minimum)
    return;
  int precedence = found->second;
  auto name = take().text;
  later([this, minimum] { binary(minimum); });
  later([this, name] { emit(Operation::Kind::call, name); });
  later([this, precedence] { expression(precedence + 1); });
}
void Compiler::primary() {
  if (macro([this] { primary(); }))
    return;
  auto t = take();
  if (t.kind == Token::Kind::symbol && t.text == "@EMPTY")
    return;
  if (t.kind == Token::Kind::number || t.kind == Token::Kind::quoted ||
      t.kind == Token::Kind::string) {
    emit(Operation::Kind::push, {}, literal(t));
    later([this] { postfix(); });
    return;
  }
  if (t.text == "\"") {
    later([this] { postfix(); });
    later([this] {
      // The closing marker must be available before consuming the payload.
      if (peek(1).text != "\"")
        throw Fault("expected closing word quote");
      auto quoted = take();
      take();
      emit(Operation::Kind::push, {}, runtime_.word(quoted.text));
    });
    return;
  }
  if (t.text == "NONOP" || t.text == "NONMAC") {
    later([this] { postfix(); });
    later([this] { emit(Operation::Kind::load, take().text); });
    return;
  }
  if (t.text == ".") {
    // Prefix .F applies F to the current stack top. Read the selector in its
    // own task so NeedList after consuming '.' cannot replay the dot.
    later([this] { postfix(); });
    later([this] { emit(Operation::Kind::call_value); });
    later([this] {
      if (peek().kind != Token::Kind::word)
        throw Fault("expected selector after '.'");
      emit(Operation::Kind::load, take().text);
    });
    return;
  }
  later([this] { postfix(); });
  if (t.text == "+") {
    later([this] { primary(); });
    return;
  }
  if (t.text == "-") {
    later([this] {
      if (peek().kind == Token::Kind::number &&
          peek().text == "9223372036854775808") {
        take();
        emit(Operation::Kind::push, {},
             Value(std::numeric_limits<std::int64_t>::min()));
        return;
      }
      later([this] { emit(Operation::Kind::call, "@NEG"); });
      later([this] { expression(70); });
    });
    return;
  }
  if (t.text == "(") {
    later([this] { expect(")"); });
    later([this] { sequence({")"}); });
    return;
  }
  if (t.text == "[") {
    auto lists = std::make_shared<std::vector<std::vector<Value>>>(1);
    later([this, lists] { literal_list(lists); });
    return;
  }
  if (t.text == "[%") {
    emit(Operation::Kind::begin_list);
    later([this] {
      expect("%]");
      emit(Operation::Kind::end_list);
    });
    later([this] { sequence({"%]"}); });
    return;
  }
  if (t.text == "IF") {
    later([this] { conditional(); });
    return;
  }
  if (t.text == "LAMBDA") {
    later([this] { function(false, true); });
    return;
  }
  if (t.kind != Token::Kind::word)
    throw Fault("unexpected token " + t.text);
  if (runtime_.operators.count(t.text) && runtime_.operators.at(t.text) == 90) {
    later([this, name = t.text] {
      emit(at("(") ? Operation::Kind::load : Operation::Kind::call, name);
    });
  } else
    emit(Operation::Kind::load, t.text);
}
void Compiler::postfix() {
  if (accept("(")) {
    bool named = !program_->code.empty() &&
                 program_->code.back().kind == Operation::Kind::load;
    if (named) {
      auto name = program_->code.back().name;
      program_->code.pop_back();
      later([this] { postfix(); });
      later([this, name] { emit(Operation::Kind::call, name); });
    } else {
      emit(Operation::Kind::save_callable);
      later([this] { postfix(); });
      later([this] { emit(Operation::Kind::call_saved); });
    }
    later([this] { arguments(")"); });
    return;
  }
  if (accept("(%")) {
    emit(Operation::Kind::begin_list);
    later([this] { postfix(); });
    later([this] {
      emit(Operation::Kind::end_list);
      emit(Operation::Kind::call, "PARTAP");
    });
    later([this] { arguments("%)"); });
    return;
  }
  if (accept(".")) {
    // A.TL.HD is HD(TL(A)). Selector read is a separate task so NeedList after
    // '.' cannot replay the dot.
    later([this] { postfix_dot(); });
  }
}
void Compiler::postfix_dot() {
  if (peek().kind != Token::Kind::word)
    throw Fault("expected selector after '.'");
  auto selector = take().text;
  later([this] { postfix(); });
  later([this, selector] { emit(Operation::Kind::call, selector); });
}
void Compiler::arguments(std::string end, bool separator) {
  if (accept(end))
    return;
  if (separator) {
    if (!accept(",") && !accept(";"))
      throw Fault("expected argument separator or " + end);
    later([this, end] { arguments(end); });
    return;
  }
  later([this, end] { arguments(end, true); });
  later([this] { imperative(); });
}
void Compiler::literal_list(
    std::shared_ptr<std::vector<std::vector<Value>>> lists) {
  if (accept("]")) {
    auto v = runtime_.list(lists->back());
    lists->pop_back();
    if (lists->empty()) {
      emit(Operation::Kind::push, {}, v);
      return;
    }
    lists->back().push_back(v);
    literal_roots_->code.push_back({Operation::Kind::push, v, {}, {}, {}});
  } else if (accept("["))
    lists->emplace_back();
  else {
    auto v = literal(take());
    lists->back().push_back(v);
    literal_roots_->code.push_back({Operation::Kind::push, v, {}, {}, {}});
  }
  later([this, lists] { literal_list(lists); });
}
void Compiler::assignment() {
  if (!accept("->"))
    return;
  // Consume only the arrow in this task. Destination parsing peeks the next
  // source token and may NeedList; a restart must not see a consumed arrow.
  later([this] { assignment(); });
  later([this] { assignment_target(); });
}
void Compiler::assignment_target() {
  if (accept("NONOP") || accept("NONMAC")) {
    auto name = take().text;
    later([this, name] { assigned(name); });
  } else if (at("\"")) {
    take();
    if (peek(1).text != "\"")
      throw Fault("expected closing word quote");
    auto quoted = take();
    take();
    emit(Operation::Kind::push, {}, runtime_.word(quoted.text));
    expect(".");
    later([this] { dotted_update(); });
  } else {
    auto name = take().text;
    later([this, name] { assigned(name); });
  }
}
void Compiler::assigned(std::string name) {
  if (accept("(")) {
    later([this, name] { emit(Operation::Kind::update, name); });
    later([this] { arguments(")"); });
  } else if (accept(".")) {
    emit(Operation::Kind::load, name);
    // Selector read is a separate task so NeedList after '.' cannot replay it.
    later([this] { dotted_update(); });
  } else
    emit(Operation::Kind::store, name);
}
void Compiler::dotted_update() {
  if (peek().kind != Token::Kind::word)
    throw Fault("expected selector after '.'");
  auto selector = take().text;
  // Decide whether another '.' follows in a fresh task: peeking it may
  // NeedList after the selector has already been consumed.
  later([this, selector] { dotted_update_after(selector); });
}
void Compiler::dotted_update_after(std::string selector) {
  if (accept(".")) {
    emit(Operation::Kind::call, selector);
    later([this] { dotted_update(); });
  } else
    emit(Operation::Kind::update, selector);
}
void Compiler::conditional(bool loop) {
  auto state = std::make_shared<Conditional>();
  state->loop = loop;
  state->start = program_->code.size();
  later([this, state] { condition_test(state); });
  later([this] { sequence({"AND", "OR", "THEN"}); });
}
void Compiler::condition_test(std::shared_ptr<Conditional> s) {
  if (at("AND") || at("OR")) {
    bool conjunction = take().text == "AND";
    (conjunction ? s->no : s->yes).push_back(program_->code.size());
    emit(conjunction ? Operation::Kind::branch_false
                     : Operation::Kind::branch_true);
    later([this, s] { condition_test(s); });
    later([this] { sequence({"AND", "OR", "THEN"}); });
    return;
  }
  expect("THEN");
  s->no.push_back(program_->code.size());
  emit(Operation::Kind::branch_false);
  for (auto i : s->yes)
    program_->code[i].target = program_->code.size();
  later([this, s] { condition_body(s); });
  later([this] { sequence({"ELSE", "ELSEIF", "CLOSE", "EXIT"}); });
}
void Compiler::condition_body(std::shared_ptr<Conditional> s) {
  if (accept("EXIT")) {
    emit(Operation::Kind::ret);
    for (auto i : s->no)
      program_->code[i].target = program_->code.size();
    return;
  }
  if (s->loop) {
    expect("CLOSE");
    emit(Operation::Kind::jump, {}, {}, s->start);
    for (auto i : s->no)
      program_->code[i].target = program_->code.size();
    return;
  }
  if (at("ELSE") || at("ELSEIF")) {
    s->done.push_back(program_->code.size());
    emit(Operation::Kind::jump);
  }
  for (auto i : s->no)
    program_->code[i].target = program_->code.size();
  later([this, s] {
    for (auto i : s->done)
      program_->code[i].target = program_->code.size();
  });
  if (accept("ELSEIF")) {
    later([this] { conditional(); });
    return;
  }
  if (accept("ELSE")) {
    later([this] { expect("CLOSE"); });
    later([this] { sequence({"CLOSE"}); });
  } else
    expect("CLOSE");
}
void Compiler::function(bool macro, bool anonymous) {
  auto name = anonymous ? std::string{} : take().text;
  auto parent = program_;
  scopes_.push_back(parent);
  program_ = std::make_shared<Program>();
  program_->function = true;
  runtime_.compiling.push_back(program_);
  later([this, macro, anonymous, name, parent] {
    header(macro, anonymous, name, parent);
  });
}
void Compiler::header(bool macro, bool anonymous, std::string name,
                      std::shared_ptr<Program> parent, bool results) {
  if (accept(";")) {
    later([this, macro, anonymous, name, parent] {
      expect("END");
      resolve();
      auto f = runtime_.function(program_);
      runtime_.object(f).macro = macro;
      program_ = parent;
      scopes_.pop_back();
      if (!anonymous && parent->function)
        parent->locals.push_back(name);
      emit(Operation::Kind::closure, {}, f);
      if (!anonymous)
        emit(Operation::Kind::define, name, {}, macro ? 1 : 0);
    });
    later([this] { sequence({"END"}); });
    return;
  }
  if (accept("=>"))
    results = true;
  else if (!accept(","))
    (results ? program_->results : program_->args).push_back(take().text);
  later([this, macro, anonymous, name, parent, results] {
    header(macro, anonymous, name, parent, results);
  });
}
void Compiler::declaration() {
  if (accept("MACRO")) {
    later([this] { declarations(true, {}); });
    return;
  }
  if (accept("OPERAT")) {
    later([this] {
      auto n = integer(literal(take()));
      if (n < 1 || n > 9)
        throw Fault("operator precedence must be 1 through 9");
      later([this, n] { declarations(false, int(n)); });
    });
    return;
  }
  declarations(false, {});
}
void Compiler::declarations(bool macro, std::optional<int> precedence) {
  if (at(";") || end())
    return;
  if (!accept(",")) {
    auto name = take().text;
    if (program_->function &&
        std::find(program_->locals.begin(), program_->locals.end(), name) ==
            program_->locals.end())
      program_->locals.push_back(name);
    emit(Operation::Kind::declare, name, {}, macro ? 1 : 0);
    if (precedence)
      runtime_.operators[name] = 100 - 10 * *precedence;
  }
  later([this, macro, precedence] { declarations(macro, precedence); });
}
void Compiler::cancel() {
  if (program_->function)
    throw Fault("CANCEL is not allowed inside a function body");
  if (at(";") || end())
    return;
  if (!accept(",") && !accept("NONMAC") && !accept("NONOP"))
    emit(Operation::Kind::cancel, take().text);
  later([this] { cancel(); });
}
void Compiler::imperative() {
  if (macro([this] { imperative(); }))
    return;
  if (at("SECTIO") || at("ENDSEC"))
    throw Fault("unsupported standalone syntax " + peek().text, peek().where);
  // N SWITCH L1 L2 ... Lk  — 1-based jump to label Lk (historical POPLAN).
  if (accept("SWITCH")) {
    later([this] { switch_labels(); });
    return;
  }
  if (accept("@EMPTY"))
    return;
  if (accept("FUNCTI")) {
    later([this] { function(); });
    return;
  }
  if (accept("MACRO")) {
    later([this] { function(true); });
    return;
  }
  if (accept("VARS")) {
    later([this] { declaration(); });
    return;
  }
  if (accept("CANCEL")) {
    later([this] { cancel(); });
    return;
  }
  if (accept("GOON")) {
    emit(Operation::Kind::push, {}, runtime_.primitive("@GOON"));
    emit(Operation::Kind::call_value);
    return;
  }
  if (accept("RETURN")) {
    emit(Operation::Kind::ret);
    return;
  }
  if (accept("GOTO")) {
    later([this] { emit(Operation::Kind::go, take().text); });
    return;
  }
  if (accept("LOOPIF")) {
    later([this] { conditional(true); });
    return;
  }
  if (accept("FORALL")) {
    later([this] {
      auto name = take().text;
      later([this, name] {
        emit(Operation::Kind::for_setup, name);
        auto test = program_->code.size();
        emit(Operation::Kind::for_test);
        later([this, test] {
          expect("CLOSE");
          emit(Operation::Kind::for_next, {}, {}, test);
          program_->code[test].target = program_->code.size();
          later([this] {
            if (accept("=>"))
              emit(Operation::Kind::print);
          });
        });
        later([this] { sequence({"CLOSE"}); });
      });
      for (int i = 0; i < 3; ++i)
        later([this] { expression(); });
    });
    return;
  }
  if (at("->")) {
    later([this] { assignment(); });
    return;
  }
  if (peek().kind == Token::Kind::word && peek(1).kind == Token::Kind::symbol &&
      peek(1).text == ":") {
    auto name = take().text;
    take();
    emit(Operation::Kind::label, name);
    return;
  }
  later([this] {
    // Consume SWITCH alone in this task; label reads may NeedList.
    if (accept("SWITCH")) {
      later([this] { switch_labels(); });
      return;
    }
    if (accept("=>"))
      emit(Operation::Kind::print);
  });
  later([this] { assignment(); });
  later([this] { expression(); });
}
void Compiler::switch_labels() {
  auto labels = std::make_shared<std::vector<std::string>>();
  later([this, labels] { switch_labels_collect(labels); });
}
void Compiler::switch_labels_collect(
    std::shared_ptr<std::vector<std::string>> labels) {
  // Label names continue until a statement stopper or closing keyword.
  // One name per task so NeedList cannot replay a consumed label.
  static const std::set<std::string> stops = {
      "END",    "CLOSE", "ELSE",   "ELSEIF", "THEN",   "AND",
      "OR",     "EXIT",  "RETURN", "GOTO",   "IF",     "LOOPIF",
      "FORALL"};
  if (peek().kind == Token::Kind::word && !stops.count(peek().text)) {
    labels->push_back(take().text);
    later([this, labels] { switch_labels_collect(labels); });
    return;
  }
  if (labels->empty())
    throw Fault("SWITCH requires at least one label");
  Operation op{Operation::Kind::switch_go};
  op.where = location_;
  op.names = std::move(*labels);
  program_->code.push_back(std::move(op));
}
void Compiler::resolve() {
  std::map<std::string, std::size_t> labels;
  for (std::size_t i = 0; i < program_->code.size(); ++i)
    if (program_->code[i].kind == Operation::Kind::label)
      labels[program_->code[i].name] = i;
  for (auto &op : program_->code) {
    if (op.kind == Operation::Kind::go) {
      auto found = labels.find(op.name);
      if (found == labels.end())
        throw Fault("unknown label " + op.name);
      op.kind = Operation::Kind::jump;
      op.target = found->second;
    } else if (op.kind == Operation::Kind::switch_go) {
      if (op.names.empty())
        throw Fault("SWITCH requires at least one label");
      op.targets.clear();
      for (const auto &name : op.names) {
        auto found = labels.find(name);
        if (found == labels.end())
          throw Fault("undefined labels");
        op.targets.push_back(found->second);
      }
    }
  }
}
} // namespace pop
