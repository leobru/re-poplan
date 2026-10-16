#ifndef POPLAN_NATIVE_HPP
#define POPLAN_NATIVE_HPP

#include "poplan/word48.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace poplan {

// Handles identify language objects, never host pointer addresses. The object
// Pair and reference payloads are implemented; other compound kinds are reserved.
struct NativeValue {
    enum class Kind { unbound, number, word, string, pair, list, reference, function };
    Kind kind = Kind::unbound;
    Word48 number;
    std::uint32_t handle = 0;
};

struct NativeWord {
    std::uint32_t handle;
    Word48 packed_name;
    Word48 class_word;
    Word48 properties;
    NativeValue binding;
};

// Resident names and descriptor metadata only. Historical function addresses
// are deliberately not installed as native callable bindings.
std::vector<NativeWord> native_bootstrap_dictionary();

// Logical allocation metadata, without a BESM memory image. Block addresses are
// retained for identity/accounting; no field is an execution PC.
struct NativeBlock {
    std::uint16_t address = 0;
    std::uint16_t words = 0;
};

struct NativeAllocatorSnapshot {
    std::vector<NativeBlock> common;
    std::array<std::vector<NativeBlock>, 021> small;
    std::vector<NativeBlock> live;
    // Space not reconstructed by the historical bitmap sweep (a trailing run).
    std::vector<NativeBlock> unavailable;
};

struct NativeAllocation {
    enum class Status { allocated, zero_size, collection_required, exhausted };
    Status status;
    NativeBlock block;
};

struct NativeCoreUsage {
    std::uint16_t used;
    std::uint16_t free;
};

// Implements a single allocation attempt, not the collection/retry wrapper.
// NativeRuntime supplies bootstrap bounds and owns the collection/retry policy.
class NativeAllocator {
public:
    NativeAllocator(std::uint16_t first, std::uint16_t limit);
    NativeAllocation try_allocate(std::uint32_t words);
    void release(NativeBlock block);
    NativeCoreUsage core_usage() const;
    // Marked blocks retain their addresses. Rebuild the common list in address
    // order and empty the size buckets, as at 05543..05702.
    void collect(const std::vector<std::uint16_t> &marked);
    const NativeAllocatorSnapshot &snapshot() const { return state_; }
private:
    std::uint16_t first_;
    std::uint16_t limit_;
    NativeAllocatorSnapshot state_;
};

struct NativeInternResult {
    enum class Status { found, created, collection_required, exhausted };
    Status status;
    std::uint32_t handle = 0;
};

class NativeDictionary {
public:
    explicit NativeDictionary(NativeAllocator &allocator);
    NativeDictionary(const NativeDictionary &) = delete;
    NativeDictionary &operator=(const NativeDictionary &) = delete;
    // Input is the reader's already packed six-byte name, not UTF-8 source.
    NativeInternResult intern(Word48 name);
    std::uint32_t lookup(Word48 name) const;
    const NativeWord &word(std::uint32_t handle) const;
    const std::vector<NativeWord> &words() const { return words_; }
    const std::array<std::uint32_t, 0400> &heads() const { return heads_; }
    void bind(std::uint32_t handle, NativeValue value);
    void declare(std::uint32_t handle, bool explicit_declaration = true);
    Word48 exchange_binding_class(std::uint32_t handle, Word48 classification);
private:
    static std::uint8_t hash(Word48 name);
    NativeWord &mutable_word(std::uint32_t handle);
    NativeAllocator &allocator_;
    std::vector<NativeWord> words_;
    std::array<std::uint32_t, 0400> heads_{};
};

enum class NativeBinary { add, subtract, multiply, divide };
// POP compiler operations at the source of emission, never BESM instructions.
enum class NativeEmission {
    evaluate, literal, branch_zero, branch_nonzero, jump, store_binding,
    return_value, evaluate_stack, load_capture, embedded_value, call_binding,
    update_binding, transfer_arguments, rebuild_arguments, label,
    continuation, load_binding, capture_function, jump_capture
};
struct NativeEmissionCost {
    std::uint16_t site;
    std::uint16_t node_words;
    std::uint16_t code_words;
};
NativeEmissionCost native_emission_cost(NativeEmission);

struct NativeBindingSlot {
    bool argument = false;
    bool result = false;
};
struct NativeFrameCost {
    std::uint32_t retained = 0;
    std::uint32_t entry_peak = 0;
    std::uint32_t exit_peak = 0;
};
// Peaks include retained words and are relative to the caller's frame base.
NativeFrameCost native_binding_frame_cost(const std::vector<NativeBindingSlot> &);
NativeFrameCost native_argument_frame_cost(std::uint16_t arguments);
// 20132 reads at the peak stack pointer before its stack adjustment. This
// access needs one more addressable word than the pointer high-water mark.
std::uint32_t native_argument_rebuild_extent(std::uint16_t arguments);

struct NativeNumberResult {
    Word48 value;
    std::string error;
};
// IEEE binary64 to normalized BESM encoding; discard excess mantissa bits
// toward zero, flush BESM underflow to zero, and diagnose nonfinite/overflow.
NativeNumberResult native_real_from_double(double);
// Signed decimal real with a required fractional digit and optional $ exponent.
NativeNumberResult native_parse_real_literal(std::string_view);
NativeNumberResult native_number_binary(NativeBinary, Word48, Word48);
// 03447: convert a checked real using BESM NTR 7 and tagged-zero addition.
NativeNumberResult native_number_intof(Word48);
enum class NativeComparison { equal, less, greater, less_equal, greater_equal };
NativeNumberResult native_number_compare(NativeComparison, Word48, Word48);
std::string native_format_number(Word48);

struct NativeOperation {
    enum class Kind { constant, binary, print, load, store, unary, erase, setpop,
                      compare, branch_false, jump, label, call, return_value, captured_value,
                      branch_true, logical_not, integer_part, sign,
                      is_integer, is_real, is_function };
    Kind kind;
    Word48 constant;
    NativeBinary binary = NativeBinary::add;
    // Evidence annotation, never an execution address.
    std::uint16_t provenance = 0;
    // Emission-site evidence is separate from primitive provenance. This is the
    // contribution to the final code extent, not a separate allocation request.
    std::uint16_t emission_site = 0;
    std::optional<std::uint16_t> legacy_words = std::nullopt;
    std::uint32_t binding = 0;
    NativeComparison comparison = NativeComparison::equal;
    // POP operation index for branches, program root index for captured_value;
    // never a BESM address.
    std::size_t target = 0;
    // Known straight-line argument staging at the compiler source. Zero means
    // no established rebuild annotation, not complete session accounting.
    std::uint16_t rebuilt_arguments = 0;
    bool rebuild_result = false;
};

struct NativeProgram {
    std::vector<NativeOperation> operations;
    std::string error;
    std::vector<NativeValue> roots;
    // Definition order: formals first, then locals. Entry visits in reverse.
    std::vector<std::pair<std::uint32_t, NativeBindingSlot>> bindings;
    bool function = false;
};

struct NativeFrame {
    std::size_t program, position = 0;
    NativeValue function;
    std::vector<std::pair<std::uint32_t, NativeValue>> saved_bindings;
    std::vector<Word48> saved_classes;
    std::map<std::uint32_t, std::uint64_t> environment;
    std::vector<std::uint64_t> saved_cells;
    std::uint64_t activation = 0;
};

struct NativeOptions {
    bool experimental_closures = false;
    // Zero preserves historical activation checks (experimental default: 16384).
    std::size_t activation_limit = 0;
    std::ostream *binding_trace = nullptr;
};

// One compilation unit after reader decisions and optional optimization.
// Captures/bindings are the two chains copied at 05025 and 05027. They are
// separate from the number of POP arguments at a later call.
struct NativeCompilation {
    std::vector<NativeEmission> emissions;
    std::vector<NativeValue> bindings;
    std::vector<NativeValue> captures;
    // Empty for pre-resolved operands; otherwise one optional literal per op.
    std::vector<std::optional<Word48>> literals;
};
struct NativeCompiledStorage {
    NativeBlock descriptor, bindings, captures, code;
};
struct NativeCompilationResult {
    NativeAllocation::Status status;
    std::size_t id = 0;
};

// Development slice: numeric arithmetic with resident operator precedence,
// parentheses, and =>. Mutable syntax and general unary operators are absent.
// This is deliberately not presented as the completed POP reader/compiler.
NativeProgram native_compile_probe(const std::string &source);
enum class NativeExhaustionPhase { rebuild_extent, rebuild_argument, rebuild_restore, rebuild_continuation,
                                   binding_target, binding_value, binding_count, binding_table };
struct NativeExhaustion {
    NativeExhaustionPhase phase;
    std::uint32_t activation_words;
    std::uint16_t arguments;
    std::size_t argument_index = 0;
};
// Read-watchpoint checks in source order; offsets are logical accounting slots,
// not an emulated hardware stack. Writes beyond the watchpoint do not trap.
std::optional<NativeExhaustion> native_rebuild_exhaustion(std::uint32_t base, std::uint16_t arguments);

struct NativeExecutionResult {
    std::size_t operations = 0;
    std::string error;
    bool storage_accounting_complete = false;
    std::uint16_t diagnostic = 0;
    NativeValue culprit;
    bool setpop = false;
    // Full session accounting additionally needs the historical reader and
    // language call/recovery paths; code storage alone does not establish it.
    bool legacy_accounting_complete = false;
};

struct NativeObject {
    NativeValue::Kind kind;
    NativeBlock block;
    std::vector<NativeValue> fields;
};

struct NativeObjectAllocation {
    NativeAllocation::Status status;
    NativeValue value;
};

// A runtime owns all roots and object payloads. Logical allocation addresses
// are independent of monotonically assigned object handles and host addresses.
class NativeRuntime {
public:
    NativeRuntime();
    explicit NativeRuntime(NativeOptions);
    NativeRuntime(std::uint16_t first, std::uint16_t limit);
    NativeRuntime(const NativeRuntime &) = delete;
    NativeRuntime &operator=(const NativeRuntime &) = delete;
    const NativeAllocator &allocator() const { return allocator_; }
    const NativeDictionary &dictionary() const { return dictionary_; }
    NativeInternResult intern(Word48 name);
    NativeAllocation intern_literal(Word48 value);
    void bind(std::uint32_t word, NativeValue value);
    void declare(std::uint32_t word, bool explicit_declaration = true) { dictionary_.declare(word, explicit_declaration); }
    NativeObjectAllocation make_reference(NativeValue value = {});
    NativeObjectAllocation make_pair(NativeValue first = {}, NativeValue second = {});
    const NativeObject &object(NativeValue value) const;
    void store(NativeValue object, std::size_t field, NativeValue value);
    std::vector<NativeValue> &values() { return values_; }
    void reset_stack() { values_.clear(); below_empty_ = false; }
    std::vector<std::vector<NativeValue>> &frame_roots() { return frame_roots_; }
    std::size_t retain_program(NativeProgram program);
    NativeValue make_function(NativeProgram program);
    NativeCompilationResult compile_storage(NativeCompilation compilation);
    const NativeCompiledStorage &compiled_storage(std::size_t id) const;
    void finish_compilation_context(std::size_t id);
    void release_compiled_storage(std::size_t id);
    // Activation accounting also roots active functions and saved binding values.
    // A failed entry/exit preserves the frame stack; unwinding bypasses exit work.
    bool enter_frame(NativeFrameCost cost, std::vector<NativeValue> roots = {});
    bool leave_frame();
    void unwind_frames(std::size_t depth);
    std::uint32_t activation_words() const { return activation_words_; }
    std::uint32_t activation_peak() const { return activation_peak_; }
    const std::optional<NativeExhaustion> &last_exhaustion() const { return last_exhaustion_; }
    void release_program(std::size_t program);
    NativeExecutionResult execute(std::size_t program, std::ostream &output);
    void collect(const std::vector<NativeValue> &temporary_roots = {});
    std::size_t collection_count() const { return collections_; }
    std::size_t environment_cell_count() const { return cells_.size(); }
    std::size_t function_count() const { return functions_.size(); }
private:
    NativeObjectAllocation allocate_object(NativeValue::Kind, std::vector<NativeValue>);
    void validate(NativeValue) const;
    NativeAllocator allocator_;
    NativeDictionary dictionary_;
    std::map<std::uint32_t, NativeObject> objects_;
    struct FunctionRecord {
        std::size_t program;
        std::map<std::uint32_t, std::uint64_t> environment;
        std::uint64_t defining_activation = 0;
        bool instance = false;
    };
    std::map<std::uint32_t, FunctionRecord> functions_;
    NativeOptions options_;
    struct BindingCell { NativeValue value; Word48 binding_class; };
    std::map<std::uint64_t, BindingCell> cells_;
    std::map<std::uint32_t, std::uint64_t> active_cells_;
    std::uint64_t next_cell_ = 1, next_activation_ = 1;
    NativeValue instantiate_function(NativeValue, const NativeFrame &);
    void trace_binding(const char *, std::uint32_t, std::uint64_t, NativeValue);
    std::uint32_t next_handle_ = 0100000;
    std::vector<NativeValue> values_;
    // The reference's initially zero cell immediately below the empty POP
    // stack can be consumed by a missing operand. Further underflow reaches
    // non-stack storage and is explicitly outside native support.
    NativeValue empty_stack_cell_{NativeValue::Kind::number, Word48(), 0};
    bool below_empty_ = false;
    std::vector<std::vector<NativeValue>> frame_roots_;
    struct ProgramRecord {
        NativeProgram program;
        std::optional<std::size_t> storage;
    };
    std::vector<std::optional<ProgramRecord>> programs_;
    std::vector<std::uint16_t> reservations_;
    std::map<std::uint64_t, NativeBlock> literals_;
    std::size_t collections_ = 0;
    struct CompilationRecord {
        NativeCompilation source;
        NativeCompiledStorage storage;
        std::vector<NativeBlock> temporary;
    };
    std::vector<std::optional<CompilationRecord>> compilations_;
    struct AccountingFrame {
        NativeFrameCost cost;
        std::vector<NativeValue> roots;
        std::vector<std::uint64_t> cells;
    };
    std::vector<AccountingFrame> accounting_frames_;
    std::uint32_t activation_words_ = 0, activation_peak_ = 0;
    std::optional<NativeExhaustion> last_exhaustion_;
    NativeAllocation allocate_storage(std::uint32_t words);
};
// A reader owns incomplete UTF-8 and token spelling across input suspension.
// No dictionary or execution side effects occur until a token is delivered.
struct NativeToken {
    enum class Kind { word, number, delimiter, newline, end, need_input, unsupported };
    Kind kind = Kind::need_input;
    std::string spelling; // external character codes, not UTF-8
    Word48 packed;
    std::size_t line = 1;
    std::size_t column = 0, end_column = 0;
};
class NativeReader {
public:
    void feed(std::string_view utf8, bool eof = false);
    NativeToken next();
    void discard_line();
private:
    std::string utf8_, input_, spelling_;
    std::size_t cursor_ = 0, line_ = 1, column_ = 0, start_column_ = 0;
    enum class Mode { idle, word, operators, number };
    Mode mode_ = Mode::idle;
    bool eof_ = false, discarding_ = false;
};
struct NativeSessionResult {
    enum class State { input_required, finished, unsupported };
    State state = State::input_required;
    std::size_t operations = 0, recoveries = 0;
    std::string error;
};
class NativeSession {
public:
    explicit NativeSession(NativeOptions options = {}) : runtime_(options) {}
    NativeSessionResult feed(std::string_view input, bool eof, std::ostream &output);
    NativeRuntime &runtime() { return runtime_; }
private:
    NativeRuntime runtime_;
    NativeReader reader_;
    std::vector<NativeToken> history_;
    NativeProgram program_;
    struct Pending {
        bool group; bool unary; NativeBinary binary; unsigned priority;
        std::optional<NativeComparison> comparison = {};
        std::uint32_t call = 0;
        std::optional<NativeOperation::Kind> primitive_call = {};
        std::size_t argument_start = 0;
    };
    std::vector<Pending> pending_;
    struct Conditional {
        std::size_t boundary;
        enum class Phase { condition, consequent, alternative } phase = Phase::condition;
        std::size_t false_branch = 0;
        std::vector<std::size_t> exits;
        std::vector<std::size_t> false_terms = {}, true_terms = {};
    };
    std::vector<Conditional> conditionals_;
    struct Definition {
        std::uint32_t name = 0;
        bool header = true;
        std::vector<std::pair<std::uint32_t, NativeBindingSlot>> bindings;
    };
    std::optional<Definition> definition_;
    // Suspended compilation contexts, not POP activation frames. Nested
    // descriptors are built once; experimental execution instantiates them at
    // the declaration with the current defining environment.
    struct EnclosingDefinition { NativeProgram program; Definition definition; };
    std::vector<EnclosingDefinition> enclosing_;
    std::optional<std::size_t> compilation_roots_;
    bool operand_ = true, start_ = true, assignment_ = false, assigned_ = false, declaration_ = false;
    std::size_t token_count_ = 0;
    std::optional<NativeOperation::Kind> call_;
    bool call_open_ = false;
    std::uint16_t deferred_diagnostic_ = 0;
    std::vector<std::string> lines_{std::string{}};
    NativeSessionResult result_;
    bool comment_ = false;
    std::size_t output_line_ = 0;
    void accept_token(const NativeToken &, std::ostream &output);
    void finish_statement(const NativeToken &, std::ostream &output);
    void reset_compiler();
    void recover(std::ostream &output, std::uint16_t code,
                 const NativeToken &token, std::optional<NativeValue> culprit = {});
};
NativeExecutionResult native_execute(const NativeProgram &, std::ostream &);
int run_native_shell(std::istream &, std::ostream &, std::ostream &, NativeOptions = {});

} // namespace poplan
#endif
