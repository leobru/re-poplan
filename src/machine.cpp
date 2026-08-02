#include "poplan/machine.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <utility>

namespace poplan {

namespace {

constexpr std::uint64_t bit40 = 00010000000000000ULL;
constexpr std::uint64_t bit41 = 00020000000000000ULL;
constexpr std::uint64_t bit42 = 00040000000000000ULL;
constexpr std::uint64_t bits40 = 00017777777777777ULL;
constexpr std::uint64_t bits41 = 00037777777777777ULL;
constexpr std::uint64_t bits42 = 00077777777777777ULL;

constexpr std::uint8_t rau_norm_disable = 001;
constexpr std::uint8_t rau_round_disable = 002;
constexpr std::uint8_t rau_logical = 004;
constexpr std::uint8_t rau_multiplicative = 010;
constexpr std::uint8_t rau_additive = 020;
constexpr std::uint8_t rau_overflow_disable = 040;
constexpr std::uint8_t rau_group_mask =
    rau_logical | rau_multiplicative | rau_additive;

struct MantissaExponent {
    explicit MantissaExponent(Word48 value)
        : exponent(static_cast<int>((value.raw() >> 41) & 0177))
    {
        const std::uint64_t raw_mantissa = value.raw() & bits41;
        mantissa = static_cast<std::int64_t>(raw_mantissa);
        if ((raw_mantissa & bit41) != 0) {
            mantissa |= static_cast<std::int64_t>(~bits41);
        }
    }

    MantissaExponent() = default;

    bool negative() const { return mantissa < 0; }

    bool denormal() const
    {
        const std::uint64_t value =
            static_cast<std::uint64_t>(mantissa);
        return (((value >> 40) ^ (value >> 41)) & 1) != 0;
    }

    void normalize_right()
    {
        mantissa >>= 1;
        ++exponent;
    }

    std::int64_t mantissa = 0;
    int exponent = 0;
};

struct E71Instruction {
    std::uint8_t reg = 0;
    std::uint16_t opcode = 0;
    std::uint16_t address = 0;
};

E71Instruction decode_e71_instruction(std::uint32_t half)
{
    E71Instruction instruction;
    instruction.reg = static_cast<std::uint8_t>((half >> 20) & 017);

    if ((half & (std::uint32_t{1} << 19)) != 0) {
        instruction.opcode = static_cast<std::uint16_t>(
            0100 | ((half >> 15) & 017));
        instruction.address = static_cast<std::uint16_t>(half & 077777);
    } else {
        instruction.opcode = static_cast<std::uint16_t>(
            (half >> 12) & 077);
        instruction.address = static_cast<std::uint16_t>(half & 07777);
        if ((half & (std::uint32_t{1} << 18)) != 0) {
            instruction.address |= 070000;
        }
    }

    // Э71 treats an expanded short address as the status/release flag. This
    // is dispak's cwadj() transformation, used by POPLAN's generated words.
    if (instruction.opcode >= 0100) {
        instruction.opcode = static_cast<std::uint16_t>(
            ((instruction.opcode - 060) << 3)
            | (instruction.address >> 12));
        instruction.address &= 07777;
    } else if ((instruction.address & 070000) != 0) {
        instruction.address &= 07777;
        instruction.opcode |= 0100;
    }
    return instruction;
}

unsigned highest_bit(std::uint64_t value)
{
    unsigned width = 32;
    unsigned index = 0;
    do {
        const std::uint64_t high = value >> width;
        if (high != 0) {
            index += width;
            value = high;
        }
    } while ((width >>= 1) != 0);
    return 48 - index;
}

std::uint64_t multiply_mantissas(std::int64_t &left,
                                 std::int64_t right)
{
    unsigned negative = 0;
    if (left < 0) {
        left = -left;
        negative ^= 1;
    }
    if (right < 0) {
        right = -right;
        negative ^= 1;
    }

    const std::uint64_t high =
        static_cast<std::uint64_t>(right) >> 20;
    const std::uint64_t low =
        static_cast<std::uint64_t>(right) & 0xfffff;
    std::uint64_t remainder =
        static_cast<std::uint64_t>(left) * low;
    left = static_cast<std::int64_t>(
        static_cast<std::uint64_t>(left) * high);
    remainder +=
        (static_cast<std::uint64_t>(left) & 0xfffff) << 20;
    left >>= 20;
    left += static_cast<std::int64_t>(remainder >> 40);
    remainder &= bits40;

    if (negative != 0) {
        left = ~left;
        remainder ^= bits40;
        ++remainder;
        left += static_cast<std::int64_t>(remainder >> 40);
        remainder &= bits40;
    }
    return remainder;
}

} // namespace

bool FunctionDescriptor::is_function(Word48 value)
{
    return (value & tag_mask) == function_tag;
}

void Machine::queue_console_input(std::vector<std::uint8_t> line)
{
    console_input_.push_back(std::move(line));
}

std::uint8_t Machine::memory_byte(std::uint16_t address,
                                  std::size_t byte_index) const
{
    const std::uint16_t word_address = address_add(
        address, static_cast<int>(byte_index / 6));
    const unsigned shift = static_cast<unsigned>(5 - byte_index % 6) * 8;
    return static_cast<std::uint8_t>(
        (memory_[word_address].raw() >> shift) & 0377);
}

void Machine::set_memory_byte(std::uint16_t address,
                              std::size_t byte_index,
                              std::uint8_t value)
{
    const std::uint16_t word_address = address_add(
        address, static_cast<int>(byte_index / 6));
    const unsigned shift = static_cast<unsigned>(5 - byte_index % 6) * 8;
    const std::uint64_t mask = std::uint64_t{0377} << shift;
    memory_[word_address] = Word48(
        (memory_[word_address].raw() & ~mask)
        | (static_cast<std::uint64_t>(value) << shift));
}

void Machine::emulate_e71(std::uint16_t control_address)
{
    constexpr std::uint8_t gost_end_of_information = 0172;
    constexpr std::uint8_t gost_eof = 0377;
    constexpr std::size_t maximum_transfer_bytes = 324 * 6;
    constexpr Word48 terminal_probe_result{0004000000040000ULL};
    constexpr Word48 poplan_ready_query{0100000077777777ULL};
    constexpr Word48 terminal_ready_result{0004000000000000ULL};
    constexpr Word48 terminal_status_result{01000000200000012ULL};

    registers_[016] = 0;
    if (control_address == 0) {
        accumulator_ = console_available_ ? terminal_probe_result : Word48();
        return;
    }

    if (memory_[control_address] == Word48(Word48::mask)
        || memory_[control_address] == poplan_ready_query) {
        accumulator_ = console_available_ ? terminal_ready_result : Word48();
        return;
    }

    for (std::size_t control_count = 0;
         control_count < core_words; ++control_count) {
        const Word48 word = memory_[control_address];
        const E71Instruction left = decode_e71_instruction(
            static_cast<std::uint32_t>(word.raw() >> 24));
        const E71Instruction right = decode_e71_instruction(
            static_cast<std::uint32_t>(word.raw() & 077777777ULL));
        const std::uint16_t start = address_add(
            registers_[left.reg], left.address);
        const std::uint16_t end = address_add(
            registers_[right.reg], right.address);

        if ((left.opcode & 0360) == 020) {
            if (end < start) {
                throw MachineError("E71: wrapped terminal buffer");
            }
            const std::size_t capacity = std::min<std::size_t>(
                maximum_transfer_bytes,
                (static_cast<std::size_t>(end - start) + 1) * 6);

            if ((left.opcode & 010) != 0) {
                if (!console_available_) {
                    throw MachineError("E71: console is unavailable");
                }
                if (console_input_.empty()) {
                    throw E71InputRequired("E71: console input is empty");
                }

                std::vector<std::uint8_t> line =
                    std::move(console_input_.front());
                console_input_.pop_front();
                std::size_t count = std::min(capacity, line.size());
                for (std::size_t index = 0; index < count; ++index) {
                    set_memory_byte(start, index, line[index]);
                }
                if (count < capacity) {
                    set_memory_byte(start, count++,
                                    (left.opcode & 1) != 0 ? 0 : gost_eof);
                }
                while (count < capacity && count % 6 != 0) {
                    set_memory_byte(start, count++, 0);
                }
            } else {
                std::size_t index = left.opcode == 0220 ? 1 : 0;
                for (; index < capacity; ++index) {
                    const std::uint8_t byte = memory_byte(start, index);
                    if ((left.opcode & 1) != 0) {
                        if (byte == 0) {
                            break;
                        }
                        console_output_.push_back(
                            static_cast<std::uint8_t>(byte & 0177));
                    } else {
                        if (byte == gost_end_of_information
                            || byte == gost_eof) {
                            break;
                        }
                        console_output_.push_back(byte);
                    }
                }
            }

            if ((right.opcode & 0100) != 0) {
                accumulator_ = terminal_status_result;
                return;
            }
        } else if ((left.opcode & 0360) == 0120) {
            accumulator_ = terminal_status_result;
            return;
        } else if ((left.opcode & 0360) == 0220) {
            // Operator-console output uses the same byte transfer, but skips
            // the leading channel byte and always returns after one word.
            std::size_t index = 1;
            const std::size_t capacity = std::min<std::size_t>(
                maximum_transfer_bytes,
                (static_cast<std::size_t>(end - start) + 1) * 6);
            for (; index < capacity; ++index) {
                const std::uint8_t byte = memory_byte(start, index);
                if (byte == gost_end_of_information || byte == gost_eof) {
                    break;
                }
                console_output_.push_back(byte);
            }
            return;
        } else {
            std::ostringstream message;
            message << "E71: unsupported operation "
                    << std::oct << left.opcode;
            throw MachineError(message.str());
        }

        control_address = address_add(control_address, 1);
    }
    throw MachineError("E71: unterminated control program");
}

FunctionDescriptor FunctionDescriptor::decode(Word48 value)
{
    if (!is_function(value)) {
        throw MachineError("POPLAN 02750: value is not a function");
    }

    FunctionDescriptor descriptor;
    descriptor.object = value;
    descriptor.environment =
        static_cast<std::uint16_t>((value.raw() >> 24) & 077777);
    descriptor.entry = value.address();
    descriptor.special = (value & special_mask).raw() != 0;
    return descriptor;
}

Word48 Machine::cyclic_add(Word48 left, Word48 right)
{
    const std::uint64_t sum = left.raw() + right.raw();
    return Word48((sum & Word48::mask) + (sum >> 48));
}

Word48 Machine::logical_shift(Word48 value, int count)
{
    if (count >= 48 || count <= -48) {
        return Word48();
    }
    if (count > 0) {
        return Word48(value.raw() >> count);
    }
    if (count < 0) {
        return Word48(value.raw() << -count);
    }
    return value;
}

Word48 Machine::pack_bits(Word48 value, Word48 mask)
{
    std::uint64_t result = 0;
    std::uint64_t source = value.raw();
    for (std::uint64_t selected = mask.raw(); selected != 0;
         selected >>= 1, source >>= 1) {
        if ((selected & 1) != 0) {
            result >>= 1;
            if ((source & 1) != 0) {
                result |= 04000000000000000ULL;
            }
        }
    }
    return Word48(result);
}

Word48 Machine::unpack_bits(Word48 value, Word48 mask)
{
    std::uint64_t result = 0;
    std::uint64_t source = value.raw();
    std::uint64_t selected = mask.raw();
    for (unsigned bit = 0; bit < 48; ++bit) {
        result <<= 1;
        if ((selected & 04000000000000000ULL) != 0) {
            if ((source & 04000000000000000ULL) != 0) {
                result |= 1;
            }
            source <<= 1;
        }
        selected <<= 1;
    }
    return Word48(result);
}

void Machine::shift_accumulator(int count)
{
    remainder_ = Word48();
    if (count > 0) {
        if (count < 48) {
            remainder_ = Word48(
                accumulator_.raw() << (48 - count));
            accumulator_ = Word48(accumulator_.raw() >> count);
        } else {
            remainder_ = Word48(
                accumulator_.raw() >> (count - 48));
            accumulator_ = Word48();
        }
    } else if (count < 0) {
        count = -count;
        if (count < 48) {
            remainder_ = Word48(
                accumulator_.raw() >> (48 - count));
            accumulator_ = Word48(accumulator_.raw() << count);
        } else {
            remainder_ = Word48(
                accumulator_.raw() << (count - 48));
            accumulator_ = Word48();
        }
    }
    select_alu_group(rau_logical);
}

void Machine::select_alu_group(std::uint8_t group)
{
    alu_mode_ = static_cast<std::uint8_t>(
        (alu_mode_ & ~rau_group_mask) | group);
}

void Machine::normalize_and_round(std::int64_t mantissa, int exponent,
                                  std::uint64_t low, bool round)
{
    std::uint64_t shifted_out = 0;

    if ((alu_mode_ & rau_norm_disable) == 0) {
        const unsigned leading = static_cast<unsigned>(
            (static_cast<std::uint64_t>(mantissa) >> 39) & 3);
        if (leading == 0) {
            std::uint64_t value =
                static_cast<std::uint64_t>(mantissa) & bits40;
            if (value != 0) {
                const int count =
                    static_cast<int>(highest_bit(value)) - 9;
                value <<= count;
                shifted_out = low >> (40 - count);
                mantissa = static_cast<std::int64_t>(
                    value | shifted_out);
                low <<= count;
                exponent -= count;
            } else if ((low & bits40) != 0) {
                const int count =
                    static_cast<int>(highest_bit(low & bits40)) - 9;
                shifted_out = low;
                mantissa = static_cast<std::int64_t>(low << count);
                low = 0;
                exponent -= 40 + count;
            } else {
                accumulator_ = Word48();
                remainder_ = Word48(
                    remainder_.raw() & ~bits40);
                return;
            }
        } else if (leading == 3) {
            std::uint64_t value =
                ~static_cast<std::uint64_t>(mantissa) & bits40;
            if (value != 0) {
                const int count =
                    static_cast<int>(highest_bit(value)) - 9;
                value = (value << count)
                    | ((std::uint64_t{1} << count) - 1);
                shifted_out = low >> (40 - count);
                mantissa = static_cast<std::int64_t>(
                    bit41 | (~value & bits40) | shifted_out);
                low <<= count;
                exponent -= count;
            } else {
                value = ~low & bits40;
                if (value != 0) {
                    const int count =
                        static_cast<int>(highest_bit(value)) - 9;
                    shifted_out = low;
                    value = (value << count)
                        | ((std::uint64_t{1} << count) - 1);
                    mantissa = static_cast<std::int64_t>(
                        bit41 | (~value & bits40));
                    low = 0;
                    exponent -= 40 + count;
                } else {
                    shifted_out = 1;
                    mantissa = static_cast<std::int64_t>(bit41);
                    low = 0;
                    exponent -= 80;
                }
            }
        }
    }

    if (shifted_out != 0) {
        round = false;
    }
    if (exponent < 0) {
        accumulator_ = Word48();
        remainder_ = Word48(remainder_.raw() & ~bits40);
        return;
    }
    if ((alu_mode_ & rau_round_disable) == 0 && round) {
        mantissa |= 1;
    }
    if (mantissa == 0 && (alu_mode_ & rau_norm_disable) == 0) {
        accumulator_ = Word48();
        remainder_ = Word48(remainder_.raw() & ~bits40);
        return;
    }

    accumulator_ = Word48(
        (static_cast<std::uint64_t>(exponent) & 0177) << 41
        | (static_cast<std::uint64_t>(mantissa) & bits41));
    remainder_ = Word48(low & bits40);
    if (exponent > 0177
        && (alu_mode_ & rau_overflow_disable) == 0) {
        throw MachineError("BESM-6 arithmetic overflow");
    }
}

void Machine::multiply(Word48 value)
{
    if (accumulator_.raw() == 0 || value.raw() == 0) {
        accumulator_ = Word48();
        remainder_ = Word48(remainder_.raw() & ~bits40);
        select_alu_group(rau_multiplicative);
        return;
    }

    MantissaExponent acc(accumulator_);
    const MantissaExponent operand(value);
    std::uint64_t low =
        multiply_mantissas(acc.mantissa, operand.mantissa);
    acc.exponent += operand.exponent - 64;
    if (acc.denormal()) {
        acc.normalize_right();
    }
    normalize_and_round(acc.mantissa, acc.exponent, low, low != 0);
    select_alu_group(rau_multiplicative);
}

void Machine::arithmetic_add(Word48 value, bool negate_accumulator,
                             bool negate_value)
{
    MantissaExponent acc(accumulator_);
    MantissaExponent operand(value);

    if (!negate_accumulator) {
        if (negate_value) {
            operand.mantissa = -operand.mantissa;
        }
    } else if (!negate_value) {
        acc.mantissa = -acc.mantissa;
    } else {
        if (acc.negative()) {
            acc.mantissa = -acc.mantissa;
        }
        if (!operand.negative()) {
            operand.mantissa = -operand.mantissa;
        }
    }

    int difference = acc.exponent - operand.exponent;
    MantissaExponent smaller;
    MantissaExponent larger;
    if (difference < 0) {
        difference = -difference;
        smaller = acc;
        larger = operand;
    } else {
        smaller = operand;
        larger = acc;
    }

    std::uint64_t low = 0;
    const bool negative = smaller.negative();
    bool round = false;
    if (difference == 0) {
        // No alignment is required.
    } else if (difference <= 40) {
        const std::uint64_t small =
            static_cast<std::uint64_t>(smaller.mantissa);
        low = (small << (40 - difference)) & bits40;
        round = low != 0;
        smaller.mantissa = static_cast<std::int64_t>(
            (small >> difference)
            | (negative ? (~std::uint64_t{0} << (40 - difference)) : 0));
        smaller.mantissa &= static_cast<std::int64_t>(bits42);
        if ((static_cast<std::uint64_t>(smaller.mantissa) & bit42) != 0) {
            smaller.mantissa |= static_cast<std::int64_t>(~bits42);
        }
    } else if (difference <= 80) {
        difference -= 40;
        round = smaller.mantissa != 0;
        const std::uint64_t small =
            static_cast<std::uint64_t>(smaller.mantissa);
        low = ((small >> difference)
               | (negative
                      ? (~std::uint64_t{0} << (40 - difference))
                      : 0))
            & bits40;
        smaller.mantissa = negative
            ? static_cast<std::int64_t>(~std::uint64_t{0}) : 0;
    } else {
        round = smaller.mantissa != 0;
        low = negative ? bits40 : 0;
        smaller.mantissa = negative
            ? static_cast<std::int64_t>(~std::uint64_t{0}) : 0;
    }

    acc.exponent = larger.exponent;
    acc.mantissa = smaller.mantissa + larger.mantissa;
    if (acc.denormal()) {
        round = round
            || (static_cast<std::uint64_t>(acc.mantissa) & 1) != 0;
        low = (low >> 1)
            | ((static_cast<std::uint64_t>(acc.mantissa) & 1) << 39);
        acc.normalize_right();
    }
    normalize_and_round(acc.mantissa, acc.exponent, low, round);
    select_alu_group(rau_additive);
}

void Machine::divide(Word48 value)
{
    if (((value.raw() ^ (value.raw() << 1)) & bit41) == 0) {
        throw MachineError("BESM-6 arithmetic division by zero");
    }

    MantissaExponent dividend(accumulator_);
    MantissaExponent divisor(value);
    MantissaExponent quotient;
    if (divisor.mantissa == static_cast<std::int64_t>(bit40)) {
        quotient.mantissa = dividend.mantissa;
        quotient.exponent =
            dividend.exponent - divisor.exponent + 65;
    } else {
        dividend.mantissa <<= 1;
        divisor.mantissa <<= 1;
        if (std::llabs(dividend.mantissa)
            >= std::llabs(divisor.mantissa)) {
            dividend.normalize_right();
        }
        quotient.exponent =
            dividend.exponent - divisor.exponent + 64;
        quotient.mantissa = 0;
        for (std::int64_t bit = static_cast<std::int64_t>(bit40);
             bit > 0; bit >>= 1) {
            if (dividend.mantissa == 0) {
                break;
            }
            if (std::llabs(dividend.mantissa)
                < static_cast<std::int64_t>(bit40)) {
                dividend.mantissa *= 2;
            } else if ((dividend.mantissa > 0)
                       == (divisor.mantissa > 0)) {
                quotient.mantissa += bit;
                dividend.mantissa =
                    dividend.mantissa * 2 - divisor.mantissa;
            } else {
                quotient.mantissa -= bit;
                dividend.mantissa =
                    dividend.mantissa * 2 + divisor.mantissa;
            }
        }
    }
    normalize_and_round(quotient.mantissa, quotient.exponent, 0, false);
    select_alu_group(rau_multiplicative);
}

void Machine::add_exponent(int delta)
{
    MantissaExponent acc(accumulator_);
    acc.exponent += delta;
    remainder_ = Word48();
    normalize_and_round(acc.mantissa, acc.exponent, 0, false);
}

void Machine::change_sign(bool negate)
{
    MantissaExponent acc(accumulator_);
    if (negate) {
        acc.mantissa = -acc.mantissa;
        if (acc.denormal()) {
            acc.normalize_right();
        }
    }
    remainder_ = Word48();
    normalize_and_round(acc.mantissa, acc.exponent, 0, false);
    select_alu_group(rau_additive);
}

void Machine::yta(int exponent_delta)
{
    if ((alu_mode_ & rau_group_mask) == rau_logical) {
        accumulator_ = remainder_;
        return;
    }

    const Word48 saved_remainder = remainder_;
    MantissaExponent acc(Word48(
        (accumulator_.raw() & ~bits41)
        | (remainder_.raw() & bits40)));
    acc.exponent += exponent_delta;
    remainder_ = Word48();
    normalize_and_round(acc.mantissa, acc.exponent, 0, false);
    remainder_ = saved_remainder;
}

void Machine::reverse_subtract(Word48 value)
{
    arithmetic_add(value, true, false);
}

void Machine::modifier_add(std::size_t destination, std::size_t source)
{
    registers_[destination] = address_add(
        registers_[source], registers_[destination]);
    registers_[0] = 0;
}

void Machine::hardware_push_acc()
{
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
}

void Machine::hardware_pop_acc()
{
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ = memory_[registers_[017]];
}

void Machine::its(std::size_t index)
{
    hardware_push_acc();
    accumulator_ = Word48(registers_[index]);
}

void Machine::xts(std::uint16_t address)
{
    hardware_push_acc();
    accumulator_ = memory_[address];
}

void Machine::sti(std::size_t index)
{
    registers_[index] = accumulator_.address();
    if (index != 017) {
        hardware_pop_acc();
    }
}

void Machine::stx(std::uint16_t address)
{
    memory_[address] = accumulator_;
    hardware_pop_acc();
}

void Machine::p03275_push_acc()
{
    // 03275: utm -1(6); atx (6)
    registers_[06] = address_add(registers_[06], -1);
    memory_[registers_[06]] = accumulator_;
}

void Machine::p03277_pop_acc()
{
    // 03277: xta (6); utm 1(6)
    accumulator_ = memory_[registers_[06]];
    registers_[06] = address_add(registers_[06], 1);
}

FunctionDescriptor Machine::p02750_decode_function() const
{
    return FunctionDescriptor::decode(accumulator_);
}

std::uint16_t Machine::p02750_dispatch()
{
    // 02750..02753: save the complete value and extract its environment.
    registers_[010] = 02745;
    memory_[03013] = accumulator_;
    memory_[03272] = accumulator_;
    accumulator_ = Word48(accumulator_.raw() >> 24);
    accumulator_ =
        Word48(accumulator_.raw() & memory_[03006].raw());
    memory_[03273] = accumulator_;

    // 02754..02755: reject anything whose high tag is not 660.
    accumulator_ = memory_[03013];
    accumulator_ =
        Word48(accumulator_.raw() & memory_[03007].raw());
    accumulator_ =
        Word48(accumulator_.raw() ^ memory_[03012].raw());
    if (accumulator_.raw() != 0) {
        registers_[016] = 012010;
        accumulator_ = memory_[03013];
        return 03014;
    }

    // 02756..02761: special functions have their own runtime path;
    // ordinary functions select the environment or direct-entry trampoline.
    accumulator_ = memory_[03013];
    accumulator_ =
        Word48(accumulator_.raw() & memory_[03010].raw());
    if (accumulator_.raw() != 0) {
        return 015765;
    }

    accumulator_ = memory_[03273];
    return accumulator_.raw() == 0 ? 03261 : 03206;
}

std::uint16_t Machine::p15765_dispatch_special_function()
{
    // 15765..15770: preserve the caller's working registers and the complete
    // 664 descriptor on the hardware stack. 16004 is the original scratch
    // cell used while expanding the descriptor.
    its(001);
    its(003);
    its(004);
    its(015);
    registers_[001] = 015765;
    xts(03272);
    memory_[016004] = accumulator_;

    // 15771..15776: the environment's +4 word points at a counted vector.
    // Its first word is one greater than the number of following values;
    // push those values on the POP stack in their stored order.
    const std::uint16_t environment = memory_[03273].address();
    accumulator_ = memory_[address_add(environment, 4)];
    registers_[003] = accumulator_.address();
    if (registers_[003] != 0) {
        accumulator_ = memory_[registers_[003]];
        registers_[004] = accumulator_.address();
        for (;;) {
            registers_[004] = address_add(registers_[004], -1);
            if (registers_[004] == 0) {
                break;
            }
            registers_[003] = address_add(registers_[003], 1);
            accumulator_ = memory_[registers_[003]];
            registers_[015] = 015774;
            p03275_push_acc();
        }
    }

    // 15777..16003: clear the scratch cell while unwinding the saved machine
    // context, then tail-dispatch the ordinary descriptor at environment +3.
    accumulator_ = memory_[0];
    stx(016004);
    sti(015);
    sti(004);
    sti(003);
    sti(001);
    accumulator_ = memory_[address_add(environment, 3)];
    return p02750_dispatch();
}

std::uint16_t Machine::p03014_dispatch_error()
{
    // 03014..03020: preserve the origin and code. Compiler errors (codes
    // without bit 010000) replace the origin with the current source object.
    registers_[001] = 03014;
    memory_[03173] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    memory_[03174] = accumulator_;
    accumulator_ = accumulator_ & memory_[03153];
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[016553];
        memory_[03173] = accumulator_;
    }

    // 03021..03024 updates the error-state cell selected through 03200.
    accumulator_ = memory_[03154];
    const std::uint16_t state_offset = memory_[03200].address();
    memory_[address_add(03200, state_offset)] = accumulator_;
    accumulator_ = Word48();
    memory_[03200] = accumulator_;
    accumulator_ = memory_[03204];
    if (accumulator_.raw() != 0) {
        throw MachineError(
            "POPLAN 03014: nested diagnostic path 03024 is not translated");
    }

    // 03030..03033 installs the pending diagnostic. An already active
    // diagnostic takes the original alternate entry at 03055.
    accumulator_ = memory_[03173];
    memory_[03176] = accumulator_;
    accumulator_ = memory_[03174];
    memory_[03204] = accumulator_;
    accumulator_ = memory_[03201];
    if (accumulator_.raw() != 0) {
        return 03055;
    }
    accumulator_ = memory_[03154];
    memory_[03200] = accumulator_;

    // 03034..03040 passes the source object followed by the error code tagged
    // as a POP integer to the function descriptor stored at 01633.
    accumulator_ = memory_[03176];
    registers_[015] = 03035;
    p03275_push_acc();
    accumulator_ = memory_[03204];
    accumulator_ =
        Word48(accumulator_.raw() ^ memory_[03155].raw());
    registers_[015] = 03037;
    p03275_push_acc();
    accumulator_ = memory_[01633];
    registers_[015] = 03041;
    return p02750_dispatch();
}

std::uint16_t Machine::p03051_unpack_error()
{
    // 03051..03054: pop the tagged code and source object. ATX/WTC through
    // r17 use the BESM hardware stack to retain the code while POP_ACC fetches
    // the object; WTC supplies the low 15 bits as the new r16.
    registers_[015] = 03052;
    p03277_pop_acc();
    hardware_push_acc();

    registers_[015] = 03053;
    p03277_pop_acc();
    const Word48 source_object = accumulator_;

    hardware_pop_acc();
    registers_[016] = accumulator_.address();
    accumulator_ = source_object;

    registers_[015] = 03235;
    return 03057;
}

std::uint16_t Machine::p03057_begin_error_format()
{
    // 03057..03064: establish the diagnostic base, save the caller's link and
    // scratch registers on the hardware stack, and select runtime page 01200.
    registers_[010] = registers_[001];
    registers_[001] = 03014;
    memory_[03173] = accumulator_;
    accumulator_ = Word48(registers_[015]);
    its(010);
    its(002);
    its(016);
    memory_[03174] = accumulator_;
    accumulator_ = Word48();
    memory_[03200] = accumulator_;
    registers_[002] = 01200;

    // 03064..03065 has a resume entry at 03075 when 03202 is already set.
    accumulator_ = memory_[03202];
    if (accumulator_.raw() != 0) {
        return 03075;
    }

    // 03065..03071 preserves the source and error state beneath the call,
    // marks formatting phase 2, and invokes the descriptor at 01200+0367.
    accumulator_ = memory_[03173];
    xts(03174);
    xts(03204);
    xts(03160);
    memory_[03200] = accumulator_;

    accumulator_ = memory_[03164];
    registers_[015] = 03071;
    p03275_push_acc();
    accumulator_ = memory_[address_add(registers_[002], 0367)];
    registers_[015] = 03072;
    return p02750_dispatch();
}

std::uint16_t Machine::p03072_resume_error_format()
{
    // 03072..03076: discard the formatter phase value, restore the diagnostic
    // words saved by 03065..03067, and reinstall the default character
    // primitive from the 01200 runtime table.
    accumulator_ = Word48();
    stx(03200);
    stx(03204);
    stx(03174);
    memory_[03173] = accumulator_;

    accumulator_ = memory_[address_add(registers_[002], 0317)];
    memory_[address_add(registers_[002], 0367)] = accumulator_;
    accumulator_ = Word48();
    memory_[03202] = accumulator_;

    // 03077..03100 starts output of the diagnostic heading at 03162.
    registers_[016] = 03162;
    registers_[014] = 013;
    registers_[015] = 03101;
    return 016313;
}

std::uint16_t Machine::p07475_cuchin()
{
    // 07475..07504: consume CUCHIN's argument and normalize the distinguished
    // 0136 value to character/control code 0012 before entering 21255.
    registers_[015] = 07476;
    p03277_pop_acc();
    registers_[010] = 07472;
    memory_[07513] = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[07511].raw());

    if (accumulator_.raw() != 0) {
        accumulator_ = accumulator_ & memory_[07510];
        if (accumulator_.raw() != 0) {
            return 07505;
        }
        accumulator_ = memory_[07513];
    } else {
        accumulator_ = memory_[07512];
    }

    registers_[015] = 03235;
    return 021255;
}

std::uint16_t Machine::p11541_match_tagged_value()
{
    // 11541..11542: the caller supplies a nonzero 640-tagged value. Both
    // failures share the original diagnostic-10100 exit at 11546/11552.
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0272)].raw());
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        registers_[016] = 010100;
        accumulator_ = memory_[address_add(registers_[017], -1)];
        select_alu_group(rau_logical);
        return 03014;
    }

    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 0304)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    if (accumulator_.raw() != 0) {
        registers_[016] = 010100;
        accumulator_ = memory_[address_add(registers_[017], -1)];
        select_alu_group(rau_logical);
        return 03014;
    }

    // 11543..11545: extract the high 24-bit field from the object addressed
    // by frame word -2, retag it, and compare it arithmetically with -1.
    const std::uint16_t object =
        memory_[address_add(registers_[017], -2)].address();
    accumulator_ = memory_[object];
    shift_accumulator(24);
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0272)].raw());
    select_alu_group(rau_logical);
    reverse_subtract(memory_[address_add(registers_[017], -1)]);

    // In additive mode UZA tests the mantissa, so 6400000000000000 is the
    // traced arithmetic zero even though its exponent field is nonzero.
    if ((accumulator_.raw() & bits41) == 0) {
        return registers_[015];
    }

    registers_[016] = 010100;
    accumulator_ = memory_[address_add(registers_[017], -1)];
    select_alu_group(rau_logical);
    return 03014;
}

std::uint16_t Machine::p16313_begin_character_sequence()
{
    // 16313..16320: save the caller's machine context and construct the
    // tagged packed-text cursor consumed by BUFFER_CHAR at 21431.
    its(015);
    its(001);
    its(002);
    hardware_push_acc();

    registers_[001] = 016313;
    registers_[002] = registers_[014];
    accumulator_ = Word48(registers_[016]);
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[016336].raw());
    memory_[016340] = accumulator_;
    registers_[016] = 016340;
    registers_[015] = 016321;
    return 021431;
}

std::uint16_t Machine::p16321_dispatch_character()
{
    // 16321..16324 tags BUFFER_CHAR's result, pushes it as CUCHIN's argument,
    // and dispatches the runtime character descriptor at 01567.
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[016335].raw());
    memory_[016337] = accumulator_;
    registers_[015] = 016323;
    p03275_push_acc();
    accumulator_ = memory_[01567];
    registers_[015] = 016325;
    return p02750_dispatch();
}

std::uint16_t Machine::p16325_continue_character_sequence()
{
    // 16325..16330: counted calls continue through 16320 until r2 reaches
    // zero. The zero-count entry uses tagged character 0012 as its sentinel.
    if (registers_[002] != 0) {
        registers_[002] = address_add(registers_[002], -1);
        if (registers_[002] != 0) {
            return 016320;
        }
    } else {
        accumulator_ = memory_[016337];
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[016334].raw());
        if (accumulator_.raw() != 0) {
            return 016320;
        }
    }

    // 16331..16333 restores r2, r1, r15, and the caller's accumulator from
    // the four hardware-stack words saved at 16313..16314.
    hardware_pop_acc();
    sti(002);
    sti(001);
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p16421_lookup_tagged_byte()
{
    // 16421..16434: reject values outside the 640-tagged low-byte form with
    // code 15. Valid bytes select one of sixteen packed table words through
    // bits 3..6, then select an eight-bit field through bits 0..2.
    const std::uint16_t scratch =
        address_add(registers_[001], 074155);
    const std::uint16_t output =
        address_add(registers_[001], 074154);

    memory_[scratch] = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074475)].raw());
    select_alu_group(rau_logical);
    shift_accumulator(7);
    if (accumulator_.raw() != 0) {
        accumulator_ =
            memory_[address_add(registers_[001], 074476)];
        select_alu_group(rau_logical);
        memory_[output] = accumulator_;
        return registers_[015];
    }

    accumulator_ = memory_[scratch]
        & memory_[address_add(registers_[001], 074477)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    shift_accumulator(3);
    registers_[012] = accumulator_.address();

    accumulator_ = memory_[scratch]
        & memory_[address_add(registers_[001], 074500)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    shift_accumulator(-1);
    memory_[scratch] = accumulator_;

    accumulator_ = cyclic_add(accumulator_, memory_[scratch]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    accumulator_ = cyclic_add(accumulator_, memory_[scratch]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074501)].raw());
    select_alu_group(rau_logical);
    registers_[013] = accumulator_.address();

    accumulator_ = memory_[address_add(
        address_add(registers_[001], 074156), registers_[012])];
    select_alu_group(rau_logical);
    const int field_shift =
        static_cast<int>((1 + registers_[013]) & 0177) - 64;
    shift_accumulator(field_shift);
    shift_accumulator(42);
    memory_[output] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p16457_shift_record()
{
    // 16457..16462: an empty +1 word selects the r1-relative continuation at
    // 74202. Otherwise shift +1 and +2 toward the head, clear +2 from word 0,
    // and select the r1-relative continuation at 74206.
    accumulator_ = memory_[address_add(registers_[003], 1)];
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        return address_add(registers_[001], 074202);
    }

    memory_[registers_[003]] = accumulator_;
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 1)] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 2)] = accumulator_;
    return address_add(registers_[001], 074206);
}

std::uint16_t Machine::p16505_begin_record_shift()
{
    // 16505..16506: preserve the incoming accumulator and caller link on the
    // hardware stack, then call the already translated record-shift entry.
    // Its 16463/16467 continuations eventually return at 16507.
    its(015);
    memory_[registers_[017]] = accumulator_;
    registers_[015] = 016507;
    return p16457_shift_record();
}

std::uint16_t Machine::p16507_resume_record_shift()
{
    // 16507..16510: restore the caller link and incoming accumulator, balance
    // r17, and select the original r1-relative continuation at 74216.
    accumulator_ = memory_[registers_[017]];
    select_alu_group(rau_logical);
    sti(015);
    return address_add(registers_[001], 074216);
}

std::uint16_t Machine::p20245_begin_input_continue()
{
    // 20245..20252: select the available console path. The returned addresses
    // 20321, 20250, 20252, and 20256 are explicit Э74/Э64/Э71 boundaries;
    // no host console behavior is substituted here.
    registers_[010] = 020170;
    accumulator_ = memory_[020377];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return 020321;
    }

    accumulator_ = memory_[020375];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[020333];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return 020250;
    }

    accumulator_ = memory_[020362];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() == 0 ? 020256 : 020252;
}

std::uint16_t Machine::p20252_query_console()
{
    // 20252: Э71 uses the POPLAN readiness-query word at r10+0146, then
    // continues directly with the status decoding at 20253.
    emulate_e71(address_add(registers_[010], 0146));
    return p20253_resume_input_continue_status();
}

std::uint16_t Machine::p20253_resume_input_continue_status()
{
    // 20253..20255: decode the word returned by Э71 0146 using the original
    // APX mask and tag comparisons. The two exceptional continuations remain
    // boundaries; the normal path proceeds to Э71 0177 at 20256.
    accumulator_ = pack_bits(accumulator_, memory_[020365]);
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[020322].raw());
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return 020261;
    }

    remainder_ = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[020323].raw());
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() == 0 ? 020715 : 020256;
}

std::uint16_t Machine::p20256_transfer_console()
{
    // 20256: execute the runtime-built terminal control word at r10+0177.
    // Its right instruction requests the standard Э71 status result.
    emulate_e71(address_add(registers_[010], 0177));
    return p20257_finish_input_continue();
}

std::uint16_t Machine::p20257_finish_input_continue()
{
    // 20257..20260: after Э71 0177, mark input/output continuation state as
    // available and return through the caller's r15 link.
    accumulator_ = memory_[020326];
    select_alu_group(rau_logical);
    memory_[020362] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p21255_begin_character_input()
{
    // 21255..21257: save the input parameter and environment-binding return,
    // then call the character conversion entry at 21275.
    registers_[010] = 021251;
    memory_[021263] = accumulator_;
    accumulator_ = Word48(registers_[015]);
    hardware_push_acc();
    accumulator_ = memory_[021263];
    registers_[015] = 021260;
    return 021275;
}

std::uint16_t Machine::p21260_forward_converted_character()
{
    // 21260: pass the converted low byte to the output/buffering entry.
    registers_[015] = 021261;
    return 025346;
}

std::uint16_t Machine::p21261_return_character()
{
    // 21261..21262: recover the environment-binding link saved by 21256 and
    // return through it. XTA (r17) performs a hardware-stack pop.
    hardware_pop_acc();
    registers_[015] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p21264_convert_character()
{
    // 21264..21273: retain the low byte, then use the original BESM floating
    // multiply/remainder path to select one six-byte table word and one byte
    // within it. NTR 3 disables normalization and rounding for this sequence.
    registers_[010] = 021264;
    accumulator_ = accumulator_ & memory_[021276];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[021430] = accumulator_;

    alu_mode_ = 003;
    accumulator_ = memory_[021430];
    select_alu_group(rau_logical);
    multiply(memory_[021427]);
    registers_[014] = accumulator_.address();
    modifier_add(014, 016);

    yta(0);
    multiply(memory_[021277]);
    reverse_subtract(memory_[021300]);
    registers_[013] = accumulator_.address();

    accumulator_ = memory_[registers_[014]];
    select_alu_group(rau_logical);
    const int shift =
        static_cast<int>((0100 + registers_[013]) & 0177) - 64;
    accumulator_ = logical_shift(accumulator_, shift);
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[021276];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p21274_decode_character()
{
    // 21274 selects the table beginning at 21301 and tail-enters the common
    // arithmetic converter at 21264.
    registers_[016] = 021301;
    return p21264_convert_character();
}

std::uint16_t Machine::p21275_encode_character()
{
    // 21275 selects the table beginning at 21354 and tail-enters the common
    // arithmetic converter at 21264.
    registers_[016] = 021354;
    return p21264_convert_character();
}

std::uint16_t Machine::p21431_buffer_char()
{
    // 21431..21440: follow the low address in the packed-text descriptor,
    // shift by its BESM exponent field, return the low character byte, and
    // advance the descriptor by cyclic addition.
    const std::uint16_t descriptor_address = registers_[016];
    const Word48 descriptor = memory_[descriptor_address];
    registers_[014] = descriptor.address();
    registers_[010] = 021431;

    accumulator_ = memory_[registers_[014]];
    const int shift =
        static_cast<int>((descriptor.raw() >> 41) & 0177) - 64;
    accumulator_ = logical_shift(accumulator_, shift);
    accumulator_ = accumulator_ & memory_[021457];
    xts(descriptor_address);
    accumulator_ = cyclic_add(accumulator_, memory_[021460]);
    memory_[descriptor_address] = accumulator_;

    // ARX leaves multiplicative mode. In that mode the original PO at 21435
    // branches when the exponent sign bit (bit 48) is set.
    if ((accumulator_.raw() & 04000000000000000ULL) == 0) {
        accumulator_ = cyclic_add(accumulator_, memory_[021461]);
        memory_[descriptor_address] = accumulator_;
        registers_[014] = 0;
    }

    hardware_pop_acc();
    return registers_[015];
}

std::uint16_t Machine::p21443_advance_descriptor()
{
    // 21443..21447: use the exponent in the descriptor at r16 to replace one
    // eight-bit field in the packed word addressed by that descriptor. The
    // temporary XTS/AAX pair uses r17 but leaves it balanced.
    const std::uint16_t descriptor_address = registers_[016];
    const Word48 descriptor = memory_[descriptor_address];
    registers_[014] = descriptor.address();
    registers_[010] = 021431;

    const int shift =
        static_cast<int>((descriptor.raw() >> 41) & 0177) - 64;
    shift_accumulator(shift);
    remainder_ = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[014]].raw());
    select_alu_group(rau_logical);

    xts(021457);
    select_alu_group(rau_logical);
    shift_accumulator(shift);
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ = accumulator_ & memory_[registers_[017]];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[014]].raw());
    select_alu_group(rau_logical);
    memory_[registers_[014]] = accumulator_;

    // 21450..21454 advances by one eight-bit field. In multiplicative mode,
    // U1A at 21452 falls through when the old descriptor has bit 48 set; that
    // is the six-byte wrap case, which restores exponent -40 and increments
    // the packed-word address.
    accumulator_ = memory_[descriptor_address];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(accumulator_, memory_[021462]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[descriptor_address] = accumulator_;
    accumulator_ = cyclic_add(accumulator_, memory_[021460]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & 04000000000000000ULL) != 0) {
        accumulator_ = cyclic_add(accumulator_, memory_[021463]);
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        memory_[descriptor_address] = accumulator_;
        registers_[014] = 0;
    }
    return registers_[015];
}

std::uint16_t Machine::p25346_begin_character_output()
{
    // 25346..25347: save the converted byte and the 21261 return link, then
    // enter the packed-descriptor helper with descriptor 25417.
    its(015);
    const std::uint16_t character_address =
        address_add(registers_[017], -1);
    xts(character_address);
    select_alu_group(rau_logical);
    registers_[016] = 025417;
    registers_[015] = 025350;
    return 021443;
}

std::uint16_t Machine::p25350_continue_character_output()
{
    // 25350..25354: count the packed byte, request the external continuation
    // at 20245 for character 0377, and otherwise return until the configured
    // field count at 25416 is reached.
    registers_[010] = 025346;
    accumulator_ = memory_[025412];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(accumulator_, memory_[025406]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[025412] = accumulator_;

    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[025407].raw());
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        registers_[015] = 025361;
        return 020245;
    }

    accumulator_ = memory_[025412];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[025416].raw());
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p25364_return_character_output();
    }

    // 25355..25357 unwinds this invocation and immediately starts another
    // 25346 pass for the 0377 terminator.
    p25364_return_character_output();
    registers_[010] = 025346;
    accumulator_ = memory_[025407];
    select_alu_group(rau_logical);
    return 025346;
}

std::uint16_t Machine::p25361_resume_character_output()
{
    // 25361..25363: after 20245, restore the initial output descriptor and
    // clear the byte count before taking the common return bracket.
    registers_[010] = 025346;
    accumulator_ = memory_[025410];
    select_alu_group(rau_logical);
    memory_[025417] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[025412] = accumulator_;
    return p25364_return_character_output();
}

std::uint16_t Machine::p25364_return_character_output()
{
    // 25364..25365: pop the saved 21261 link into r15 and the converted byte
    // back into the accumulator, balancing the two words pushed at 25346.
    hardware_pop_acc();
    sti(015);
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p03206_prepare_ordinary_call()
{
    // 03206..03213: install the called descriptor and find its environment
    // description through the scratch words at 03272..03274.
    registers_[010] = 03206;
    accumulator_ = memory_[03274];
    its(015);
    xts(03272);
    memory_[03274] = accumulator_;

    const std::uint16_t environment = memory_[03273].address();
    accumulator_ = memory_[address_add(environment, 3)];
    registers_[011] = accumulator_.address();
    if (registers_[011] != 0) {
        accumulator_ = memory_[registers_[011]];
        registers_[012] = accumulator_.address();
        registers_[011] =
            address_add(registers_[011], registers_[012]);

        // 03214..03232: prepare each captured slot. The first XTS is
        // intentionally left on the BESM hardware stack for 03235.
        for (;;) {
            registers_[012] = address_add(registers_[012], -1);
            if (registers_[012] == 0) {
                break;
            }

            registers_[011] = address_add(registers_[011], -1);
            accumulator_ = memory_[registers_[011]];
            registers_[013] = accumulator_.address();

            accumulator_ = memory_[registers_[013]];
            xts(address_add(registers_[013], -1));
            accumulator_ =
                Word48(accumulator_.raw() & memory_[03265].raw());
            accumulator_ = Word48(accumulator_.raw() >> 24);
            xts(registers_[011]);
            accumulator_ = Word48(
                accumulator_.raw()
                ^ memory_[address_add(registers_[013], -1)].raw());
            accumulator_ =
                Word48(accumulator_.raw() & memory_[03265].raw());
            accumulator_ = Word48(
                accumulator_.raw()
                ^ memory_[address_add(registers_[013], -1)].raw());
            stx(address_add(registers_[013], -1));
            memory_[registers_[013]] = accumulator_;

            accumulator_ = memory_[registers_[011]];
            accumulator_ =
                Word48(accumulator_.raw() & memory_[03266].raw());
            if (accumulator_.raw() == 0) {
                continue;
            }

            // 03225..03232: consume one actual argument from the POP stack
            // and store it through the slot address saved above.
            accumulator_ = Word48(registers_[013]);
            its(011);
            its(012);
            hardware_push_acc();
            p03277_pop_acc();

            registers_[010] = 03206;
            const std::uint16_t destination =
                memory_[address_add(registers_[017], -3)].address();
            stx(destination);
            sti(012);
            sti(011);
        }
    }

    // 03233..03234: generated code returns through 03235 so that the
    // captured environment and previous current-function word are restored.
    registers_[015] = 03235;
    return memory_[03272].address();
}

std::uint16_t Machine::p03235_bind_environment()
{
    registers_[010] = 03206;
    accumulator_ = memory_[03274];
    accumulator_ = Word48(accumulator_.raw() >> 24);
    registers_[011] = accumulator_.address();

    if (registers_[011] != 0) {
        accumulator_ = memory_[address_add(registers_[011], 3)];
        registers_[011] = accumulator_.address();
    }

    if (registers_[011] != 0) {
        accumulator_ = memory_[registers_[011]];
        registers_[012] = accumulator_.address();

        // 03242..03256: restore the captured values into their generated
        // slots. This is the original binding operation, including the
        // behavior responsible for the reduced Man-or-Boy failure.
        for (;;) {
            registers_[012] = address_add(registers_[012], -1);
            if (registers_[012] == 0) {
                break;
            }

            registers_[011] = address_add(registers_[011], 1);
            accumulator_ = memory_[registers_[011]];
            registers_[013] = accumulator_.address();

            accumulator_ =
                Word48(accumulator_.raw() & memory_[03267].raw());
            if (accumulator_.raw() != 0) {
                accumulator_ = Word48(registers_[011]);
                its(012);
                its(013);
                xts(registers_[013]);
                p03275_push_acc();

                registers_[010] = 03206;
                hardware_pop_acc();
                sti(013);
                sti(012);
                registers_[011] = accumulator_.address();
            }

            hardware_pop_acc();
            accumulator_ = Word48(accumulator_.raw() << 24);
            xts(address_add(registers_[013], -1));
            accumulator_ =
                Word48(accumulator_.raw() & memory_[03270].raw());

            registers_[017] = address_add(registers_[017], -1);
            accumulator_ = Word48(
                accumulator_.raw() ^ memory_[registers_[017]].raw());

            stx(address_add(registers_[013], -1));
            memory_[registers_[013]] = accumulator_;
        }
    }

    // 03257..03260: restore the generated-code link and previous current
    // function descriptor.
    hardware_pop_acc();
    sti(015);
    memory_[03274] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p03261_enter_function()
{
    registers_[010] = 03206;
    accumulator_ = memory_[03274];
    its(015);
    xts(03272);
    memory_[03274] = accumulator_;
    registers_[015] = 03235;
    return memory_[03272].address();
}

std::uint16_t Machine::p20110_transfer_arguments()
{
    // 20110 uses UTC (r15) followed by VZM (r16) as a computed return.
    if (registers_[016] == 0) {
        return registers_[015];
    }

    registers_[017] = address_add(registers_[017], 1);
    registers_[016] = address_add(registers_[016], -1);
    if (registers_[016] == 0) {
        p03277_pop_acc();
        return registers_[015];
    }

    registers_[016] = address_add(registers_[016], 1);
    registers_[017] =
        address_add(registers_[017], registers_[016]);

    accumulator_ = Word48(registers_[003]);
    registers_[003] = registers_[017];
    its(002);
    its(015);
    hardware_push_acc();
    registers_[002] = registers_[016];

    // 20117..20121: copy arguments from the POP stack into the new
    // activation in reverse stack order.
    do {
        registers_[003] = address_add(registers_[003], -1);
        p03277_pop_acc();
        memory_[registers_[003]] = accumulator_;
        registers_[002] = address_add(registers_[002], -1);
    } while (registers_[002] != 0);

    hardware_pop_acc();
    sti(015);
    sti(002);
    sti(003);
    return registers_[015];
}

std::uint16_t Machine::p20124_build_activation()
{
    registers_[017] = address_add(registers_[017], -1);
    registers_[016] = address_add(registers_[016], -1);
    if (registers_[015] == 0) {
        p03275_push_acc();
        return registers_[015];
    }

    registers_[017] = address_add(registers_[017], 1);
    registers_[010] = 20110;
    its(002);
    its(015);
    its(016);

    accumulator_ =
        Word48(accumulator_.raw() ^ memory_[20142].raw());
    accumulator_ = cyclic_add(accumulator_, memory_[20143]);
    registers_[002] = accumulator_.address();

    its(017);
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ =
        cyclic_add(accumulator_, memory_[registers_[017]]);
    memory_[20141] = accumulator_;

    // 20133..20135: transfer all actual values to the downward POP stack.
    do {
        const std::uint16_t source = address_add(
            registers_[017], static_cast<int>(registers_[002]) - 3);
        accumulator_ = memory_[source];
        p03275_push_acc();
        if (registers_[002] == 0) {
            break;
        }
        registers_[002] = address_add(registers_[002], 1);
    } while (true);

    hardware_pop_acc();
    sti(015);
    registers_[002] = accumulator_.address();

    const std::uint16_t activation_end = memory_[20141].address();
    registers_[017] = address_add(activation_end, -5);
    return registers_[015];
}

} // namespace poplan
