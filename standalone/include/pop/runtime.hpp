#ifndef POP_RUNTIME_HPP
#define POP_RUNTIME_HPP
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
namespace pop {
using Handle = std::uint64_t;
struct Undefined {};
struct Terminator {};
struct ObjectRef {
  Handle id;
};
struct Value {
  using Payload =
      std::variant<Undefined, Terminator, std::int64_t, double, ObjectRef>;
  Payload data;
  Value() : data(Undefined{}) {}
  explicit Value(std::int64_t n) : data(n) {}
  explicit Value(double n);
  static Value object(Handle id) {
    Value v;
    v.data = ObjectRef{id};
    return v;
  }
  static Value nil() { return object(1); }
  bool nil_value() const { return object() && handle() == 1; }
  static Value termin() {
    Value v;
    v.data = Terminator{};
    return v;
  }
  bool integer() const { return std::holds_alternative<std::int64_t>(data); }
  bool real() const { return std::holds_alternative<double>(data); }
  bool numeric() const { return integer() || real(); }
  bool object() const { return std::holds_alternative<ObjectRef>(data); }
  Handle handle() const { return std::get<ObjectRef>(data).id; }
  bool truth() const { return !integer() || std::get<std::int64_t>(data) != 0; }
};
struct Location {
  std::string file = "<stdin>";
  std::size_t line = 1, column = 1;
};
struct Fault : std::runtime_error {
  std::optional<Location> where;
  explicit Fault(const std::string &message,
                 std::optional<Location> location = {})
      : std::runtime_error(message), where(std::move(location)) {}
};
struct NeedInput {};
struct NeedList {
  Value value;
};
std::vector<char32_t> decode(const std::string &, std::size_t *valid_bytes = nullptr);
std::string encode(char32_t);
std::string canonical(const std::string &);
std::int64_t integer(Value);
double real(Value);
Value arithmetic(const std::string &, Value, Value);
bool equal(Value, Value);
int compare(Value, Value);
std::string number_text(Value);
struct Token {
  enum class Kind { word, number, string, quoted, symbol, end };
  Kind kind = Kind::end;
  std::string text;
  Location where;
  std::optional<Value> value;
  Token() = default;
  Token(Kind k, std::string text, Location where,
        std::optional<Value> value = {})
      : kind(k), text(std::move(text)), where(std::move(where)), value(value) {}
};
std::vector<Token> tokenize(const std::string &, const std::string &, bool,
                            std::size_t limit = 0);
struct Operation {
  enum class Kind {
    push,
    load,
    store,
    call,
    call_value,
    save_callable,
    call_saved,
    update,
    jump,
    branch_false,
    branch_true,
    closure,
    define,
    ret,
    print,
    print_step,
    begin_list,
    end_list,
    partial,
    label,
    go,
    cancel,
    declare,
    for_setup,
    for_test,
    for_next
  };
  Kind kind = Kind::push;
  Value value;
  std::string name;
  std::size_t target = 0;
  Location where;
  Value symbol{};
};
struct Program {
  std::vector<Operation> code;
  std::vector<std::string> args, locals, results;
  std::map<std::string, Value> symbols;
  bool function = false;
};
struct Object {
  enum class Kind {
    word,
    string,
    pair,
    reference,
    record,
    strip,
    function,
    identifier
  };
  enum class FunctionKind {
    user,
    primitive,
    partial,
    composition,
    accessor,
    constructor,
    destructor,
    strip_init,
    strip_get,
    array,
    stream,
    reader,
    output
  };
  Kind kind;
  std::string text;
  std::vector<Value> fields;
  std::shared_ptr<Program> program;
  std::map<Handle, Handle> environment;
  FunctionKind function_kind = FunctionKind::user;
  Value property, updater, type;
  Value referent, binding_reference, binding_record;
  std::optional<Token> source_token;
  Value source_forward;
  std::vector<std::int64_t> bounds;
  std::size_t index = 0, width = 0;
  bool macro = false, closed = false, forcing = false;
  bool protected_function = false;
  bool updater_assigned = false;
  // Ordinary CONSPAIR objects and common-list nodes share two fields, but
  // retain distinct construction kinds even when their tails happen to be NIL.
  bool plain_pair = false;
  Handle protected_cell = 0;
  std::shared_ptr<std::iostream> stream;
  explicit Object(Kind k) : kind(k) {}
};
struct PrintTask {
  enum class Kind { value, list_node, list_read, text, leave, characters };
  Kind kind;
  Value item;
  std::string text;
  std::size_t index = 0, limit = 0;
};
struct PrintState {
  std::vector<PrintTask> tasks;
  std::set<Handle> visiting;
  bool raw = false;
};
struct Frame {
  std::shared_ptr<Program> program;
  std::size_t pc = 0;
  Value function;
  std::map<Handle, Handle> environment;
  std::vector<std::pair<Handle, Handle>> saved;
  std::vector<std::size_t> list_bases;
  struct Loop {
    Value symbol, step, limit;
  };
  std::vector<Loop> loops;
  std::vector<Value> callees{};
  std::shared_ptr<PrintState> printer{};
};
class Runtime;
class Compiler {
  friend class Runtime;

public:
  explicit Compiler(Runtime &);
  std::shared_ptr<Program> statement();
  bool advance();
  Value macro_call;
  bool macro_pending = false;
  void macro_done();
  std::shared_ptr<Program> result() const { return result_; }

private:
  Runtime &runtime_;
  std::shared_ptr<Program> program_, result_, literal_roots_;
  std::vector<std::function<void()>> tasks_;
  Location location_;
  Token peek(std::size_t = 0) const;
  bool end() const;
  Token take();
  bool at(const std::string &) const;
  bool accept(const std::string &);
  void expect(const std::string &);
  void later(std::function<void()> task) { tasks_.push_back(std::move(task)); }
  void emit(Operation::Kind, std::string = {}, Value = {}, std::size_t = 0);
  void sequence(std::set<std::string>);
  void imperative();
  void expression(int = 0);
  void binary(int);
  void primary();
  void postfix();
  void arguments(std::string, bool = false);
  void assignment();
  void assigned(std::string);
  void conditional(bool = false);
  void function(bool = false, bool = false);
  void declaration();
  void header(bool, bool, std::string, std::shared_ptr<Program>, bool = false);
  void declarations(bool, std::optional<int>);
  void cancel();
  std::vector<std::shared_ptr<Program>> scopes_;
  void statement_tail();
  void start();
  void literal_list(std::shared_ptr<std::vector<std::vector<Value>>>);
  bool macro(std::function<void()>);
  Value literal(Token);
  void resolve();
  struct Conditional {
    bool loop;
    std::size_t start;
    std::vector<std::size_t> no, yes, done;
  };
  void condition_test(std::shared_ptr<Conditional>);
  void condition_body(std::shared_ptr<Conditional>);
};
struct Options {
  std::size_t max_frames = 0, max_operations = 0;
  std::function<double()> elapsed;
  std::function<std::chrono::system_clock::time_point()> wall_clock;
  std::vector<std::string> library_paths;
};
class Runtime {
public:
  explicit Runtime(std::istream &, std::ostream &, std::ostream &,
                   Options = {});
  Value make(Object::Kind);
  Object &object(Value);
  const Object &object(Value) const;
  Value word(const std::string &);
  Value string(const std::string &);
  Value pair(Value, Value);
  Value list(const std::vector<Value> &);
  std::vector<Value> items(Value) const;
  std::vector<Value> data_fields(Value) const;
  std::vector<Value> data_components(Value) const;
  Value data_word(Value) const;
  bool character_strip(Value) const;
  bool empty_list(Value) const;
  void force_list(Value);
  Value function(std::shared_ptr<Program>);
  Value primitive(const std::string &);
  bool has_updater(Value) const;
  bool protected_binding(Value);
  void assign(Value, Value);
  Value updater(Value);
  Value partial(Value, const std::vector<Value> &);
  Value binding(const std::string &);
  void bind(const std::string &, Value);
  Handle cell(const std::string &);
  Handle cell(Value);
  Value identifier(const std::string &);
  Value identifier(Value);
  Value binding(Value);
  void bind(Value, Value);
  void cancel_word(Value);
  Value pop();
  void push(Value v) { stack.push_back(v); }
  void call(Value, bool = false);
  void schedule(Value, const std::vector<Value> &, bool = false);
  bool execute(std::shared_ptr<Program>);
  bool run(std::size_t stop_depth = 0);
  void unwind(bool restore_bindings = true);
  void output_text(const std::string &);
  void print_value(Value, bool = false);
  Value token_value(Token);
  std::vector<std::weak_ptr<Program>> compiling;
  void invoke(const std::string &, Object * = nullptr, bool = false);
  enum class SessionState { need_input, finished, failed };
  struct RunResult {
    SessionState state;
    std::size_t diagnostics_added;
  };
  RunResult feed(const std::string &source_id, const std::string &bytes,
                 bool eof = false);
  RunResult resume();
  bool input_eof = true;
  std::shared_ptr<Compiler> parser;
  bool source(const std::string &, const std::string & = "<stdin>",
              bool = true);
  void collect();
  std::string format(Value, bool = false) const;
  void register_primitives();
  void diagnostic(const std::string &, Location = {});
  Token read_token(bool quote_markers = false);
  Token read_physical_token();
  Token peek_source(std::size_t offset = 0);
  void prepend_tokens(const std::vector<Token> &);
  void initialize_source();
  Token physical_token;
  Value read_item();
  void compile_source(const std::string &, const std::string &);
  void compile_tokens(std::vector<Token>);
  void compile_stream(Value, bool skip_to_end = false);
  void restore_source();
  std::vector<Token> text_tokens(Value, const std::string &);
  std::vector<Value> stack;
  std::map<std::string, int> operators;
  std::map<std::string, Value> macros;
  std::map<Handle, std::unique_ptr<Object>> objects;
  std::map<Handle, Value> cells;
  std::map<Handle, Handle> active;
  std::vector<Frame> frames;
  struct InputContext {
    std::vector<Token> tokens;
    std::size_t position;
    bool eof;
    std::shared_ptr<Compiler> parser;
    Value supplier;
    std::string pending, name;
    Location location;
    Value proglist, nextch;
    std::string lex_failure;
  };
  Value source_supplier, empty_source_function;
  std::vector<InputContext> sources;
  std::vector<Token> input, macro_expansion;
  std::size_t input_position = 0;
  std::istream &in;
  std::ostream &out, &errors;
  Options options;
  bool resetting = false, handling_error = false;
  std::string error_message;
  Location error_location;
  std::size_t diagnostics = 0, operations = 0, max_depth = 0;
  std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now();

private:
  Handle next_ = 1;
  std::map<std::string, Value> words_;
  std::string pending_, pending_name_, lex_failure_;
  bool session_active_ = false;
  Location lex_location_;
  void lex_available();
  void queue_parser();
  void start_print(std::shared_ptr<PrintState>);
  std::string next_print(PrintState &) const;
};
} // namespace pop
#endif
