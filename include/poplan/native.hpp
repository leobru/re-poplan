#ifndef POPLAN_NATIVE_HPP
#define POPLAN_NATIVE_HPP

#include "poplan/word48.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace poplan {

// Handles identify language objects, never host pointer addresses. The object
// kinds are reserved here; compound allocation is not implemented yet.
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
};

struct NativeAllocation {
    enum class Status { allocated, zero_size, collection_required };
    Status status;
    NativeBlock block;
};

struct NativeCoreUsage {
    std::uint16_t used;
    std::uint16_t free;
};

// Implements a single allocation attempt, not the collection/retry wrapper.
// Arena bounds are supplied explicitly until native bootstrap is recovered.
class NativeAllocator {
public:
    NativeAllocator(std::uint16_t first, std::uint16_t limit);
    NativeAllocation try_allocate(std::uint32_t words);
    void release(NativeBlock block);
    NativeCoreUsage core_usage() const;
    const NativeAllocatorSnapshot &snapshot() const { return state_; }
private:
    std::uint16_t first_;
    std::uint16_t limit_;
    NativeAllocatorSnapshot state_;
};

struct NativeInternResult {
    enum class Status { found, created, collection_required };
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
NativeNumberResult native_number_binary(NativeBinary, Word48, Word48);
std::string native_format_number(Word48);

struct NativeOperation {
    enum class Kind { constant, binary, print };
    Kind kind;
    Word48 constant;
    NativeBinary binary = NativeBinary::add;
    // Evidence annotation, never an execution address.
    std::uint16_t provenance = 0;
};

struct NativeProgram {
    std::vector<NativeOperation> operations;
    std::string error;
};

// Development slice: decimal integer, binary operator, decimal integer, =>.
// This is deliberately not presented as the completed POP reader/compiler.
NativeProgram native_compile_probe(const std::string &source);
struct NativeExecutionResult {
    std::size_t operations = 0;
    std::string error;
};
NativeExecutionResult native_execute(const NativeProgram &, std::ostream &);
int run_native_shell(std::istream &, std::ostream &, std::ostream &);

} // namespace poplan
#endif
