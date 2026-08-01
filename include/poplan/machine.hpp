#ifndef POPLAN_MACHINE_HPP
#define POPLAN_MACHINE_HPP

#include "poplan/word48.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace poplan {

class MachineError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct FunctionDescriptor {
    static constexpr Word48 tag_mask{07700000000000000ULL};
    static constexpr Word48 function_tag{06600000000000000ULL};
    static constexpr Word48 special_mask{00040000000000000ULL};

    Word48 object;
    std::uint16_t environment = 0;
    std::uint16_t entry = 0;
    bool special = false;

    static bool is_function(Word48 value);
    static FunctionDescriptor decode(Word48 value);
};

class Machine {
public:
    static constexpr std::size_t core_words = 0100000;

    Word48 &accumulator() { return accumulator_; }
    const Word48 &accumulator() const { return accumulator_; }

    std::uint16_t &reg(std::size_t index) { return registers_.at(index); }
    std::uint16_t reg(std::size_t index) const { return registers_.at(index); }

    Word48 &memory(std::uint16_t address) { return memory_.at(address); }
    Word48 memory(std::uint16_t address) const { return memory_.at(address); }

    // Literal translations of POPLAN entries 03275 and 03277.
    void p03275_push_acc();
    void p03277_pop_acc();

    // Decode performed by the first part of EVAL_DISPATCH at 02750.
    FunctionDescriptor p02750_decode_function() const;
    std::uint16_t p02750_dispatch();

    // Literal translations of the ordinary-function call cluster.
    // The return value is the next BESM-6 instruction address.
    std::uint16_t p03206_prepare_ordinary_call();
    std::uint16_t p03235_bind_environment();
    std::uint16_t p03261_enter_function();

    // Literal translations of the activation/argument-transfer entries.
    std::uint16_t p20110_transfer_arguments();
    std::uint16_t p20124_build_activation();

    // Literal translations of the diagnostic call path. 03014 packages the
    // source object and tagged error code; 03051 unpacks them for 03057,
    // whose entry block preserves context and starts diagnostic formatting.
    std::uint16_t p03014_dispatch_error();
    std::uint16_t p03051_unpack_error();
    std::uint16_t p03057_begin_error_format();
    std::uint16_t p03072_resume_error_format();

    // Primitive-runtime entries reached by the first diagnostic-format call.
    std::uint16_t p07475_cuchin();
    std::uint16_t p16313_begin_character_sequence();
    std::uint16_t p16321_dispatch_character();
    std::uint16_t p16325_continue_character_sequence();
    std::uint16_t p21255_begin_character_input();
    std::uint16_t p21431_buffer_char();

private:
    static std::uint16_t address_add(std::uint16_t value, int delta)
    {
        return static_cast<std::uint16_t>((value + delta) & 077777);
    }

    static Word48 cyclic_add(Word48 left, Word48 right);
    static Word48 logical_shift(Word48 value, int count);
    void hardware_push_acc();
    void hardware_pop_acc();
    void its(std::size_t index);
    void xts(std::uint16_t address);
    void sti(std::size_t index);
    void stx(std::uint16_t address);

    Word48 accumulator_;
    std::array<std::uint16_t, 020> registers_{};
    std::array<Word48, core_words> memory_{};
};

} // namespace poplan

#endif
