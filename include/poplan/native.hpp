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
private:
    static std::uint8_t hash(Word48 name);
    NativeWord &mutable_word(std::uint32_t handle);
    NativeAllocator &allocator_;
    std::vector<NativeWord> words_;
    std::array<std::uint32_t, 0400> heads_{};
};

enum class NativeBinary { add, subtract, multiply, divide };
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
std::string native_format_number(Word48);

struct NativeOperation {
    enum class Kind { constant, binary, print };
    Kind kind;
    Word48 constant;
    NativeBinary binary = NativeBinary::add;
    // Evidence annotation, never an execution address.
    std::uint16_t provenance = 0;
    // Allocation-site evidence is separate from primitive provenance. Unknown
    // cost must never be interpreted as a zero-word historical allocation.
    std::uint16_t emission_site = 0;
    std::optional<std::uint16_t> legacy_words = std::nullopt;
};

struct NativeProgram {
    std::vector<NativeOperation> operations;
    std::string error;
    std::vector<NativeValue> roots;
};

// Development slice: numeric arithmetic with resident operator precedence,
// parentheses, and =>. Mutable syntax and general unary operators are absent.
// This is deliberately not presented as the completed POP reader/compiler.
NativeProgram native_compile_probe(const std::string &source);
struct NativeExecutionResult {
    std::size_t operations = 0;
    std::string error;
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
    NativeRuntime(std::uint16_t first, std::uint16_t limit);
    NativeRuntime(const NativeRuntime &) = delete;
    NativeRuntime &operator=(const NativeRuntime &) = delete;
    const NativeAllocator &allocator() const { return allocator_; }
    const NativeDictionary &dictionary() const { return dictionary_; }
    NativeInternResult intern(Word48 name);
    void bind(std::uint32_t word, NativeValue value);
    NativeObjectAllocation make_reference(NativeValue value = {});
    NativeObjectAllocation make_pair(NativeValue first = {}, NativeValue second = {});
    const NativeObject &object(NativeValue value) const;
    void store(NativeValue object, std::size_t field, NativeValue value);
    std::vector<NativeValue> &values() { return values_; }
    std::vector<std::vector<NativeValue>> &frame_roots() { return frame_roots_; }
    std::size_t retain_program(NativeProgram program);
    void release_program(std::size_t program);
    NativeExecutionResult execute(std::size_t program, std::ostream &output);
    void collect(const std::vector<NativeValue> &temporary_roots = {});
    std::size_t collection_count() const { return collections_; }
private:
    NativeObjectAllocation allocate_object(NativeValue::Kind, std::vector<NativeValue>);
    void validate(NativeValue) const;
    NativeAllocator allocator_;
    NativeDictionary dictionary_;
    std::map<std::uint32_t, NativeObject> objects_;
    std::uint32_t next_handle_ = 0100000;
    std::vector<NativeValue> values_;
    std::vector<std::vector<NativeValue>> frame_roots_;
    std::vector<std::optional<NativeProgram>> programs_;
    std::vector<std::uint16_t> reservations_;
    std::size_t collections_ = 0;
};
NativeExecutionResult native_execute(const NativeProgram &, std::ostream &);
int run_native_shell(std::istream &, std::ostream &, std::ostream &);

} // namespace poplan
#endif
