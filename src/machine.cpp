#include "poplan/machine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <utility>

namespace poplan {

namespace {

constexpr std::uint64_t bit40 = 00010000000000000ULL;
constexpr std::uint64_t bit41 = 00020000000000000ULL;
constexpr std::uint64_t bit42 = 00040000000000000ULL;
constexpr std::uint64_t bit48 = 04000000000000000ULL;
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

std::uint64_t local_jiffies_since_midnight()
{
    using namespace std::chrono;

    const system_clock::time_point now = system_clock::now();
    const system_clock::time_point whole_second = time_point_cast<seconds>(now);
    const std::time_t time = system_clock::to_time_t(whole_second);
    const std::tm *local = std::localtime(&time);
    if (local == nullptr) {
        throw MachineError("cannot determine local time for E53/010");
    }
    const auto microseconds = duration_cast<std::chrono::microseconds>(
        now - whole_second).count();
    const std::uint64_t seconds_since_midnight =
        static_cast<std::uint64_t>(
            (local->tm_hour * 60 + local->tm_min) * 60 + local->tm_sec);
    return seconds_since_midnight * 50
        + static_cast<std::uint64_t>(microseconds / 20000);
}

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

bool Machine::accumulator_condition() const
{
    const std::uint8_t group = alu_mode_ & rau_group_mask;
    if (group == rau_additive) {
        return (accumulator_.raw() & bit41) != 0;
    }
    if (group == rau_multiplicative) {
        return (accumulator_.raw() & bit48) == 0;
    }
    if (group == rau_logical) {
        return accumulator_.raw() != 0;
    }
    return true;
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

void Machine::elementary_function(std::uint16_t function)
{
    remainder_ = Word48();

    const MantissaExponent operand(accumulator_);
    double value = std::ldexp(
        static_cast<double>(operand.mantissa), operand.exponent - 104);
    if (value < std::ldexp(1.0, -65)
        && value >= -std::ldexp(1.0, -65)) {
        value = 0;
    }

    switch (function) {
    case 0:
        value = std::sqrt(value);
        if (std::isnan(value)) {
            throw MachineError(
                "E50/000 square root of negative accumulator");
        }
        break;
    case 1:
        value = std::sin(value);
        break;
    case 2:
        value = std::cos(value);
        break;
    case 3:
        value = std::atan(value);
        break;
    case 4:
        value = std::asin(value);
        if (std::isnan(value)) {
            throw MachineError(
                "E50/004 arcsine of accumulator outside [-1, 1]");
        }
        break;
    case 5:
        value = std::log(value);
        if (!std::isfinite(value)) {
            throw MachineError(
                "E50/005 logarithm of nonpositive accumulator");
        }
        break;
    case 6:
        value = std::exp(value);
        if (!std::isfinite(value)) {
            throw MachineError("E50/006 exponential overflow");
        }
        break;
    default:
        throw MachineError("unsupported E50 elementary function");
    }

    int exponent = 0;
    double mantissa = std::frexp(value, &exponent);
    if (mantissa == -0.5) {
        mantissa = -1;
        --exponent;
    }
    exponent += 64;
    if (mantissa == 0 || exponent < 0) {
        accumulator_ = Word48();
        return;
    }

    const std::int64_t packed_mantissa = static_cast<std::int64_t>(
        mantissa * static_cast<double>(std::uint64_t{1} << 40));
    accumulator_ = Word48(
        (static_cast<std::uint64_t>(exponent) & 0177) << 41
        | (static_cast<std::uint64_t>(packed_mantissa) & bits41));
    if (exponent > 0177
        && (alu_mode_ & rau_overflow_disable) == 0) {
        throw MachineError("E50 elementary-function exponent overflow");
    }
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
    set_register(destination, address_add(
        register_value(source), register_value(destination)));
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
    accumulator_ = Word48(register_value(index));
    select_alu_group(rau_logical);
}

void Machine::xts(std::uint16_t address)
{
    hardware_push_acc();
    accumulator_ = memory_[address];
    select_alu_group(rau_logical);
}

void Machine::sti(std::size_t index)
{
    set_register(index, accumulator_.address());
    if (index != 017) {
        hardware_pop_acc();
    }
    select_alu_group(rau_logical);
}

void Machine::stx(std::uint16_t address)
{
    memory_[address] = accumulator_;
    hardware_pop_acc();
    select_alu_group(rau_logical);
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
    select_alu_group(rau_logical);
}

std::uint16_t Machine::p03301()
{
    // 03301..03302 loads through r16 and pushes the value to the downward
    // POP stack before returning through r15.
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    p03275_push_acc();
    return registers_[015];
}

std::uint16_t Machine::p03303_store_stack_top()
{
    // XTS (r6) preserves the incoming accumulator on the hardware stack;
    // STX (r16) stores the POP top and restores that accumulator.
    xts(registers_[006]);
    registers_[006] = address_add(registers_[006], 1);
    stx(registers_[016]);
    return registers_[015];
}

std::uint16_t Machine::p03314()
{
    registers_[015] = 03235;
    return 03275;
}

std::uint16_t Machine::p03330()
{
    registers_[013] = 03310;
    memory_[03451] = accumulator_;
    accumulator_ = accumulator_ & memory_[03454];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[03455].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p03336();
    }

    accumulator_ = memory_[03451];
    select_alu_group(rau_logical);
    const Word48 saved = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[02047].raw());
    remainder_ = saved;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p03336();
    }

    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p03336()
{
    accumulator_ = memory_[03456];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p03374()
{
    registers_[015] = 03375;
    return 03277;
}

std::uint16_t Machine::p03375()
{
    hardware_push_acc();
    registers_[015] = 03376;
    return 03277;
}

std::uint16_t Machine::p03376()
{
    // 03376..03402 compares the second POP value with the first, saved at
    // the top of the hardware stack, then selects one of two result words.
    registers_[015] = 03314;
    const Word48 old_accumulator = accumulator_;
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ = Word48(
        old_accumulator.raw() ^ memory_[registers_[017]].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    registers_[013] = 03310;
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        accumulator_ = memory_[03453];
    } else {
        accumulator_ = memory_[02207];
    }
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p03506()
{
    registers_[010] = 03506;
    registers_[013] = accumulator_.address();
    registers_[013] = address_add(registers_[013], 05502);
    shift_accumulator(-24);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return registers_[015];
    }

    memory_[address_add(registers_[017], 1)] = accumulator_;
    alu_mode_ = 003;
    arithmetic_add(memory_[03535], false, true);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) != 0) {
        return 03516;
    }

    registers_[013] = 05502;
    accumulator_ = memory_[registers_[013]];
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[017], 1)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    memory_[registers_[013]] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p03516()
{
    // 03516..03520: replace the word addressed by r13 with the address in
    // r16, preserving the replaced word at the location addressed by r16.
    accumulator_ = memory_[registers_[013]];
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    memory_[registers_[013]] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p03531()
{
    // Save the original caller in r14: p03516 changes r15 on every pass
    // through the linked list, while 03530 returns through this saved link.
    registers_[014] = registers_[015];
    registers_[013] = 05504;
    return p03532();
}

std::uint16_t Machine::p03532()
{
    registers_[016] = accumulator_.address();
    if (registers_[016] == 0) {
        return registers_[014];
    }

    registers_[015] = 03534;
    return 03516;
}

std::uint16_t Machine::p03534()
{
    accumulator_ = memory_[address_add(registers_[016], 1)];
    select_alu_group(rau_logical);
    return p03532();
}

std::uint16_t Machine::p03536()
{
    // 03536..03541 builds the five-word continuation frame used by the
    // compiler paths that install a computed continuation in r16.  The
    // scratch word at r2+0626 is preserved on the frame and then cleared.
    its(002);
    registers_[002] = 03536;
    its(001);
    its(015);
    const std::uint16_t scratch = address_add(registers_[002], 0626);
    xts(scratch);
    xts(0);
    memory_[scratch] = accumulator_;
    return registers_[016];
}

std::uint16_t Machine::p03702()
{
    // Save the caller below the value at r7+01167.  A zero value skips the
    // 05410/05215 transformation and enters the shared 17337 path directly.
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    xts(address_add(registers_[007], 01167));
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        const std::uint16_t continuation = address_add(registers_[002], 0151);
        return continuation == 03707 ? p03707() : continuation;
    }

    registers_[015] = 03704;
    return 05410;
}

std::uint16_t Machine::p03704()
{
    accumulator_ = memory_[address_add(registers_[007], 01167)];
    select_alu_group(rau_logical);
    xts(address_add(registers_[007], 01077));
    registers_[015] = 03706;
    return 05215;
}

std::uint16_t Machine::p03706()
{
    memory_[address_add(registers_[007], 01077)] = accumulator_;
    return p03707();
}

std::uint16_t Machine::p03707()
{
    registers_[015] = 03710;
    return 017337;
}

std::uint16_t Machine::p03710()
{
    stx(address_add(registers_[007], 01167));
    registers_[015] = accumulator_.address();
    return 04467;
}

std::uint16_t Machine::p03716()
{
    registers_[016] = accumulator_.address();
    if (registers_[016] != 0) {
        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        registers_[014] = accumulator_.address();
        if (registers_[014] != 0) {
            accumulator_ = memory_[registers_[014]];
            select_alu_group(rau_logical);
            accumulator_ = cyclic_add(
                accumulator_, memory_[address_add(registers_[017], -1)]);
            remainder_ = Word48();
            select_alu_group(rau_multiplicative);
            memory_[registers_[014]] = accumulator_;
        }

        accumulator_ = memory_[address_add(registers_[016], 1)];
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            return address_add(registers_[002], 0160);
        }
    }

    registers_[017] = address_add(registers_[017], -1);
    return registers_[015];
}

std::uint16_t Machine::p03724()
{
    // Preserve the incoming accumulator and caller exactly as the two
    // consecutive ITS r15 instructions do before the shared table read.
    its(015);
    its(015);
    return p03725();
}

std::uint16_t Machine::p03725()
{
    // 03725 is also the re-entry point after 04447 allocates another table
    // item.  The existing 17337 translation may itself leave through an
    // allocation continuation, in which case 03726 remains its saved link.
    constexpr std::uint16_t target = 017337;
    registers_[015] = 03726;
    if (target != 017337) {
        return target;
    }
    const std::uint16_t continuation = p17337();
    if (continuation != 03726) {
        return continuation;
    }
    return p03726();
}

std::uint16_t Machine::p03726()
{
    const std::uint16_t transformed =
        address_add(registers_[002], 0104);
    memory_[transformed] = accumulator_;
    registers_[001] = accumulator_.address();

    accumulator_ = memory_[address_add(registers_[001], -1)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[002], 0641)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0642)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[002], 0105)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[002], 0627)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);

    // UZA in multiplicative mode branches when bit 48 is set.  The normal
    // r2=03536 frame selects local restoration at 03734; retain a computed
    // target if a caller supplies a different frame base.
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        const std::uint16_t target = address_add(registers_[002], 0176);
        if (target != 03734) {
            return target;
        }

        sti(015);
        sti(015);
        accumulator_ = memory_[transformed];
        select_alu_group(rau_logical);
        return p17340();
    }

    accumulator_ = memory_[transformed];
    select_alu_group(rau_logical);
    xts(address_add(registers_[002], 0636));
    registers_[015] = 03725;
    return 04447;
}

std::uint16_t Machine::p03736()
{
    // Save ACC, r15, r2, r4, and r4 again exactly as the four ITS
    // instructions do.  The repeated r4 word is consumed by the paired STI
    // restoration at 03751.
    its(015);
    its(002);
    its(004);
    its(004);

    registers_[002] = 03536;
    registers_[015] = 03741;
    return 017417;
}

std::uint16_t Machine::p03741()
{
    if (registers_[016] != 0) {
        return p03743();
    }
    registers_[015] = 03742;
    return 04467;
}

std::uint16_t Machine::p03742()
{
    registers_[016] = 0;
    const std::uint16_t continuation =
        address_add(registers_[002], 0213);
    return continuation == 03751 ? p03751() : continuation;
}

std::uint16_t Machine::p03743()
{
    accumulator_ = memory_[address_add(registers_[002], 0105)];
    select_alu_group(rau_logical);
    const Word48 first = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0630)].raw());
    remainder_ = first;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        const std::uint16_t continuation =
            address_add(registers_[002], 0223);
        return continuation == 03761 ? p03761() : continuation;
    }

    accumulator_ = memory_[address_add(registers_[002], 0643)];
    select_alu_group(rau_logical);
    const Word48 second = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0103)].raw());
    remainder_ = second;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        const std::uint16_t continuation =
            address_add(registers_[002], 0216);
        return continuation == 03754 ? p03754() : continuation;
    }

    registers_[004] = 0;
    registers_[015] = 03750;
    return 04214;
}

std::uint16_t Machine::p03750()
{
    registers_[016] = 0;
    return p03751();
}

std::uint16_t Machine::p03751()
{
    sti(004);
    sti(004);
    sti(002);
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p03754()
{
    accumulator_ = memory_[address_add(registers_[002], 0103)];
    select_alu_group(rau_logical);
    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0644)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        const std::uint16_t continuation =
            address_add(registers_[002], 0221);
        return continuation == 03757 ? p03757() : continuation;
    }

    registers_[004] = 1;
    registers_[015] = 03750;
    const std::uint16_t continuation =
        address_add(registers_[002], 0211);
    return continuation == 03747 ? 04214 : continuation;
}

std::uint16_t Machine::p03757()
{
    accumulator_ = memory_[address_add(registers_[002], 0103)];
    select_alu_group(rau_logical);
    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0645)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return address_add(registers_[002], 0224);
    }

    registers_[016] = 1;
    const std::uint16_t continuation =
        address_add(registers_[002], 0213);
    return continuation == 03751 ? p03751() : continuation;
}

std::uint16_t Machine::p03761()
{
    registers_[016] = 1;
    const std::uint16_t continuation =
        address_add(registers_[002], 0213);
    return continuation == 03751 ? p03751() : continuation;
}

std::uint16_t Machine::p17417()
{
    // Preserve the caller's frame base and link before selecting this
    // routine's constant block through r2.
    accumulator_ = Word48(registers_[002]);
    select_alu_group(rau_logical);
    its(015);
    registers_[002] = 017417;
    xts(registers_[003]);
    shift_accumulator(1);
    accumulator_ =
        cyclic_add(accumulator_, memory_[address_add(registers_[002], 044)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);

    // UZA in multiplicative mode selects 17430 when bit 48 is set.
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        return p17430();
    }
    return p17423();
}

std::uint16_t Machine::p17423()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    registers_[015] = 017424;
    return 06134;
}

std::uint16_t Machine::p17424()
{
    xts(address_add(registers_[002], 045));
    registers_[015] = 017425;
    return 04447;
}

std::uint16_t Machine::p17425()
{
    registers_[016] = 0;
    return p17426();
}

std::uint16_t Machine::p17426()
{
    // XTA (r17) is the BESM hardware-stack pop form.
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(015);
    registers_[002] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p17430()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 046)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17442();
    }

    registers_[015] = 017432;
    return 06526;
}

std::uint16_t Machine::p17432()
{
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[002], 047)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 050)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p17435();
    }

    registers_[016] = 04400;
    registers_[015] = 017435;
    return 03014;
}

std::uint16_t Machine::p17435()
{
    registers_[015] = 017436;
    return 04467;
}

std::uint16_t Machine::p17436()
{
    registers_[016] = address_add(registers_[016], 075321);
    if (registers_[016] == 0) {
        return p17440();
    }

    registers_[016] = 04610;
    registers_[015] = 017440;
    return 03014;
}

std::uint16_t Machine::p17440()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    memory_[registers_[003]] = accumulator_;
    const std::uint16_t continuation =
        address_add(registers_[002], 4);
    return continuation == 017423 ? p17423() : continuation;
}

std::uint16_t Machine::p17442()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 051)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17454();
    }

    // The equal case transfers a five-word generated-code frame to 15322.
    registers_[015] = 017450;
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(004);
    its(007);
    its(005);
    xts(registers_[003]);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    memory_[address_add(registers_[007], 01057)] = accumulator_;
    return 015322;
}

std::uint16_t Machine::p17451()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    registers_[015] = 017452;
    return 017571;
}

std::uint16_t Machine::p17452()
{
    xts(address_add(registers_[002], 052));
    registers_[015] = 017453;
    return 04447;
}

std::uint16_t Machine::p17453()
{
    registers_[016] = 0;
    return p17426();
}

std::uint16_t Machine::p17454()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    shift_accumulator(41);
    registers_[014] = accumulator_.address();
    registers_[014] = address_add(registers_[014], 077627);
    if (registers_[014] == 0) {
        return p17461();
    }
    registers_[014] = address_add(registers_[014], -1);
    if (registers_[014] == 0) {
        return p17423();
    }
    registers_[014] = address_add(registers_[014], -1);
    if (registers_[014] == 0) {
        return p17423();
    }

    const std::uint16_t continuation =
        address_add(registers_[002], 032);
    return continuation == 017451 ? p17451() : continuation;
}

std::uint16_t Machine::p17461()
{
    registers_[016] = 1;
    const std::uint16_t continuation =
        address_add(registers_[002], 7);
    return continuation == 017426 ? p17426() : continuation;
}

std::uint16_t Machine::p17472()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(001);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);

    registers_[001] = memory_[03641].address();
    registers_[010] = 017472;
    accumulator_ = memory_[address_add(registers_[016], -1)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(accumulator_, memory_[0]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        return p17546();
    }

    accumulator_ = memory_[address_add(registers_[001], -1)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[017550];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017556].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p17523();
    }

    accumulator_ = memory_[03504];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17504();
    }

    accumulator_ = memory_[address_add(registers_[001], -1)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[017551];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() != 0 ? p17514() : p17523();
}

std::uint16_t Machine::p17504()
{
    registers_[016] = 017557;
    return p17505();
}

std::uint16_t Machine::p17505()
{
    for (;;) {
        accumulator_ = memory_[address_add(registers_[016], 1)];
        select_alu_group(rau_logical);
        registers_[016] = accumulator_.address();
        if (registers_[016] == 0) {
            return p17523();
        }

        accumulator_ = Word48(registers_[001]);
        select_alu_group(rau_logical);
        const Word48 address_word = accumulator_;
        accumulator_ =
            Word48(accumulator_.raw() ^ memory_[registers_[016]].raw());
        remainder_ = address_word;
        select_alu_group(rau_logical);
        accumulator_ = accumulator_ & memory_[017552];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            continue;
        }

        accumulator_ = memory_[017570];
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            return p17547();
        }

        accumulator_ = memory_[address_add(registers_[001], -1)];
        select_alu_group(rau_logical);
        const Word48 value = accumulator_;
        accumulator_ = Word48(accumulator_.raw() ^ memory_[017567].raw());
        remainder_ = value;
        select_alu_group(rau_logical);
        accumulator_ = accumulator_ & memory_[017550];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        return accumulator_.raw() != 0 ? p17547() : p17514();
    }
}

std::uint16_t Machine::p17514()
{
    registers_[016] = 017563;
    registers_[014] = 017;
    registers_[015] = 017516;
    return 016313;
}

std::uint16_t Machine::p17516()
{
    registers_[016] = 03637;
    registers_[015] = 017517;
    return 03301;
}

std::uint16_t Machine::p17517()
{
    registers_[016] = 02103;
    registers_[015] = 017520;
    return 02767;
}

std::uint16_t Machine::p17520()
{
    registers_[016] = 017557;
    registers_[015] = 017521;
    return 03301;
}

std::uint16_t Machine::p17521()
{
    registers_[016] = 01567;
    registers_[015] = 017522;
    return 02767;
}

std::uint16_t Machine::p17522()
{
    registers_[010] = 017472;
    return p17523();
}

std::uint16_t Machine::p17523()
{
    accumulator_ = memory_[03504];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p17536();
    }

    accumulator_ = memory_[registers_[001]];
    select_alu_group(rau_logical);
    registers_[015] = 017526;
    return 017342;
}

std::uint16_t Machine::p17526()
{
    accumulator_ = memory_[address_add(registers_[001], -1)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[017553];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    shift_accumulator(24);
    its(001);
    const Word48 address_word = accumulator_;
    hardware_pop_acc();
    accumulator_ = Word48(address_word.raw() ^ accumulator_.raw());
    remainder_ = address_word;
    select_alu_group(rau_logical);
    registers_[015] = 017531;
    return 017342;
}

std::uint16_t Machine::p17531()
{
    registers_[010] = 017472;
    accumulator_ = Word48(registers_[001]);
    select_alu_group(rau_logical);
    const Word48 address_word = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017567].raw());
    remainder_ = address_word;
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[017554];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017566].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    registers_[016] = 017560;
    registers_[015] = 017535;
    return 017602;
}

std::uint16_t Machine::p17535()
{
    registers_[010] = 017472;
    return p17536();
}

std::uint16_t Machine::p17536()
{
    accumulator_ = memory_[address_add(registers_[001], -1)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[017555];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017567].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], -1)] = accumulator_;

    accumulator_ = memory_[03504];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[address_add(registers_[001], -1)];
        select_alu_group(rau_logical);
        accumulator_ = Word48(accumulator_.raw() | memory_[017551].raw());
        remainder_ = Word48();
        select_alu_group(rau_logical);
        memory_[address_add(registers_[001], -1)] = accumulator_;
    }
    return p17543();
}

std::uint16_t Machine::p17543()
{
    registers_[016] = registers_[001];
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(001);
    registers_[015] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p17546()
{
    registers_[016] = 02025;
    return 03014;
}

std::uint16_t Machine::p17547()
{
    registers_[016] = 02020;
    return 03014;
}

std::uint16_t Machine::p17571()
{
    registers_[010] = 017571;
    its(015);
    xts(address_add(registers_[010], 010));
    hardware_push_acc();
    registers_[016] = 017577;
    accumulator_ = memory_[address_add(registers_[017], -3)];
    select_alu_group(rau_logical);
    registers_[015] = 017575;
    return 017602;
}

std::uint16_t Machine::p17575()
{
    accumulator_ = memory_[address_add(registers_[017], -1)];
    select_alu_group(rau_logical);
    registers_[017] = address_add(registers_[017], -3);
    return memory_[address_add(registers_[017], 1)].address();
}

std::uint16_t Machine::p17602()
{
    registers_[010] = 017602;
    its(001);
    registers_[001] = registers_[016];
    its(015);
    xts(address_add(registers_[017], -2));
    xts(0);
    registers_[015] = 017606;
    return 05213;
}

std::uint16_t Machine::p17606()
{
    registers_[010] = 017602;
    xts(address_add(registers_[001], 2));
    accumulator_ = cyclic_add(accumulator_, memory_[017620]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    stx(address_add(registers_[001], 2));
    const std::uint16_t generated_slot =
        address_add(memory_[address_add(registers_[001], 1)].address(), 1);
    memory_[generated_slot] = accumulator_;
    accumulator_ = accumulator_ & memory_[017621];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    stx(address_add(registers_[001], 1));
    sti(015);
    sti(001);
    return registers_[015];
}

std::uint16_t Machine::p17614()
{
    registers_[010] = 017602;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 2)] = accumulator_;
    memory_[registers_[016]] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    accumulator_ =
        cyclic_add(accumulator_, memory_[address_add(registers_[010], 020)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[address_add(registers_[016], 1)] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p17624()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(001);
    its(002);
    its(003);
    hardware_push_acc();
    registers_[001] = 017624;
    alu_mode_ = 003;
    accumulator_ = memory_[address_add(registers_[001], 0164)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0235)] = accumulator_;
    registers_[002] = 04462;
    accumulator_ = memory_[address_add(registers_[002], 1)];
    select_alu_group(rau_logical);
    registers_[003] = accumulator_.address();
    return p17632();
}

std::uint16_t Machine::p17632()
{
    accumulator_ = memory_[address_add(registers_[001], 0165)];
    select_alu_group(rau_logical);
    registers_[015] = 017633;
    return 017762;
}

std::uint16_t Machine::p17633()
{
    accumulator_ = Word48(registers_[002]);
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0236)] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0234)] = accumulator_;
    memory_[address_add(registers_[001], 0233)] = accumulator_;
    return p17636();
}

std::uint16_t Machine::p17636()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0166)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0167)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17655();
    }

    registers_[016] = memory_[address_add(registers_[003], 1)].address();
    if (registers_[016] == 0) {
        return p17655();
    }
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0166)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0244)] = accumulator_;
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p17646();
    }

    const Word48 secondary = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0170)].raw());
    remainder_ = secondary;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17655();
    }
    accumulator_ = memory_[address_add(registers_[001], 0167)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0244)] = accumulator_;
    return p17646();
}

std::uint16_t Machine::p17646()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0171)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 selected = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0244)].raw());
    remainder_ = selected;
    select_alu_group(rau_logical);
    const Word48 combined = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0172)].raw());
    remainder_ = combined;
    select_alu_group(rau_logical);
    memory_[registers_[003]] = accumulator_;

    registers_[016] = memory_[address_add(registers_[003], 1)].address();
    accumulator_ = memory_[address_add(registers_[016], 1)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 1)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0173)];
    select_alu_group(rau_logical);
    registers_[015] = 017654;
    return 03506;
}

std::uint16_t Machine::p17654()
{
    alu_mode_ = 003;
    return p17655();
}

std::uint16_t Machine::p17655()
{
    if (registers_[003] == 0) {
        return p17717();
    }
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0174)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17717();
    }

    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    shift_accumulator(39);
    registers_[016] = accumulator_.address();
    registers_[016] = address_add(registers_[016], 020036);
    const std::uint16_t indirect = memory_[registers_[016]].address();
    accumulator_ = memory_[indirect];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0243)] = accumulator_;

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    shift_accumulator(24);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0175)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0241)] = accumulator_;

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    shift_accumulator(27);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0175)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0240)] = accumulator_;

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    shift_accumulator(30);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0175)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0237)] = accumulator_;

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    shift_accumulator(33);
    memory_[address_add(registers_[001], 0242)] = accumulator_;

    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    shift_accumulator(39);
    arithmetic_add(memory_[address_add(registers_[001], 0176)],
                   false, true);
    memory_[address_add(registers_[001], 0244)] = accumulator_;
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0177)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17722();
    }

    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    shift_accumulator(24);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0174)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    registers_[016] = 025575;
    registers_[015] = 017700;
    return 016254;
}

std::uint16_t Machine::p17700()
{
    alu_mode_ = 007;
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17722();
    }

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    const std::uint16_t first_shift_address = address_add(
        address_add(registers_[001], 0206),
        memory_[address_add(registers_[001], 0244)].address());
    const int first_shift = static_cast<int>(
        (memory_[first_shift_address].raw() >> 41) & 0177) - 64;
    shift_accumulator(first_shift);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0200)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0245)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[016], 1)];
    select_alu_group(rau_logical);
    const std::uint16_t second_shift_address = address_add(
        address_add(registers_[001], 0210),
        memory_[address_add(registers_[001], 0244)].address());
    const int second_shift = static_cast<int>(
        (memory_[second_shift_address].raw() >> 41) & 0177) - 64;
    shift_accumulator(second_shift);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0201)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0246)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 0245)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0175)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0241)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0245)];
    select_alu_group(rau_logical);
    shift_accumulator(3);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0175)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0240)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0245)];
    select_alu_group(rau_logical);
    shift_accumulator(6);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0175)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0237)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0245)];
    select_alu_group(rau_logical);
    shift_accumulator(9);
    memory_[address_add(registers_[001], 0242)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 0246)];
    select_alu_group(rau_logical);
    const Word48 second_part = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0202)].raw());
    remainder_ = second_part;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0243)] = accumulator_;
    return p17722();
}

std::uint16_t Machine::p17717()
{
    accumulator_ = memory_[address_add(registers_[001], 0164)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0237)] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0242)] = accumulator_;
    memory_[address_add(registers_[001], 0240)] = accumulator_;
    memory_[address_add(registers_[001], 0241)] = accumulator_;
    return p17722();
}

std::uint16_t Machine::p17722()
{
    accumulator_ = memory_[address_add(registers_[001], 0242)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p17730();
    }
    accumulator_ = memory_[address_add(registers_[001], 0242)];
    select_alu_group(rau_logical);
    const Word48 field = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0164)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p17726();
    }
    accumulator_ = memory_[address_add(registers_[001], 0243)];
    select_alu_group(rau_logical);
    memory_[registers_[003]] = accumulator_;
    return p17730();
}

std::uint16_t Machine::p17726()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0171)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 selected = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0243)].raw());
    remainder_ = selected;
    select_alu_group(rau_logical);
    memory_[registers_[003]] = accumulator_;
    return p17730();
}

std::uint16_t Machine::p17730()
{
    accumulator_ = memory_[address_add(registers_[001], 0237)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p17740();
    }
    accumulator_ = memory_[address_add(registers_[001], 0233)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[address_add(registers_[001], 0240)],
                   false, false);
    memory_[address_add(registers_[001], 0233)] = accumulator_;
    arithmetic_add(memory_[address_add(registers_[001], 0234)],
                   false, true);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) != 0) {
        return p17735();
    }
    accumulator_ = memory_[address_add(registers_[001], 0233)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0234)] = accumulator_;
    return p17735();
}

std::uint16_t Machine::p17735()
{
    accumulator_ = memory_[address_add(registers_[001], 0233)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[address_add(registers_[001], 0241)],
                   false, true);
    memory_[address_add(registers_[001], 0233)] = accumulator_;
    registers_[015] = 017636;
    return 017774;
}

std::uint16_t Machine::p17740()
{
    accumulator_ = memory_[address_add(registers_[001], 0234)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[address_add(registers_[001], 0233)],
                   false, true);
    const Word48 difference = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[0].raw());
    remainder_ = difference;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        shift_accumulator(-24);
        const Word48 shifted = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[001], 0203)].raw());
        remainder_ = shifted;
        select_alu_group(rau_logical);
        registers_[015] = 017744;
        return 017762;
    }
    return p17744();
}

std::uint16_t Machine::p17744()
{
    accumulator_ = memory_[address_add(registers_[001], 0240)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        shift_accumulator(-24);
        const Word48 shifted = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[001], 0165)].raw());
        remainder_ = shifted;
        select_alu_group(rau_logical);
        registers_[015] = 017747;
        return 017762;
    }
    return p17747();
}

std::uint16_t Machine::p17747()
{
    accumulator_ = memory_[address_add(registers_[001], 0234)];
    select_alu_group(rau_logical);
    shift_accumulator(-24);
    registers_[016] = memory_[address_add(registers_[001], 0236)].address();
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[016]].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    registers_[015] = 017753;
    return 017774;
}

std::uint16_t Machine::p17753()
{
    accumulator_ = memory_[address_add(registers_[001], 0241)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p17632();
    }
    shift_accumulator(-24);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0203)].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    registers_[015] = 017632;
    return 017762;
}

std::uint16_t Machine::p17756()
{
    accumulator_ = memory_[address_add(registers_[001], 0235)];
    select_alu_group(rau_logical);
    stx(03645);
    sti(003);
    sti(002);
    sti(001);
    registers_[015] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p04074()
{
    its(015);
    xts(04464);
    registers_[015] = 04076;
    return 017340;
}

std::uint16_t Machine::p04076()
{
    accumulator_ = memory_[04463];
    select_alu_group(rau_logical);
    registers_[015] = 04100;
    return 017340;
}

std::uint16_t Machine::p04100()
{
    accumulator_ = memory_[address_add(registers_[007], 01173)];
    select_alu_group(rau_logical);
    registers_[015] = 04101;
    return 017340;
}

std::uint16_t Machine::p04101()
{
    accumulator_ = memory_[address_add(registers_[007], 01167)];
    select_alu_group(rau_logical);
    registers_[015] = 04102;
    return 017340;
}

std::uint16_t Machine::p04102()
{
    accumulator_ = memory_[address_add(registers_[002], 0107)];
    select_alu_group(rau_logical);
    registers_[015] = 04103;
    return 017340;
}

std::uint16_t Machine::p04103()
{
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[04463] = accumulator_;
    memory_[address_add(registers_[007], 01173)] = accumulator_;
    memory_[address_add(registers_[007], 01167)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[002], 0627)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[002], 0107)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[002], 0647)];
    select_alu_group(rau_logical);
    stx(04464);
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p04161()
{
    // Preserve the three-word compiler frame exactly: the incoming value,
    // the old r2 base, and the caller, leaving a second caller copy in ACC.
    its(002);
    its(015);
    its(015);
    registers_[002] = 03536;
    registers_[015] = 04164;
    return 04322;
}

std::uint16_t Machine::p04164()
{
    if (registers_[016] != 0) {
        return p04200();
    }

    accumulator_ = memory_[address_add(registers_[002], 0103)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[002], 0104)] = accumulator_;
    registers_[015] = 04166;
    return 04467;
}

std::uint16_t Machine::p04166()
{
    registers_[015] = 04167;
    return 017242;
}

std::uint16_t Machine::p04167()
{
    if (registers_[016] != 0) {
        return p04175();
    }

    const std::uint16_t slot = address_add(
        memory_[address_add(registers_[002], 0104)].address(), -1);
    accumulator_ = memory_[slot];
    select_alu_group(rau_logical);
    shift_accumulator(47);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        const std::uint16_t continuation =
            address_add(registers_[002], 0454);
        return continuation == 04212 ? p04212() : continuation;
    }

    accumulator_ = memory_[address_add(registers_[002], 0104)];
    select_alu_group(rau_logical);
    xts(address_add(registers_[002], 0655));
    registers_[015] = 04173;
    return 04447;
}

std::uint16_t Machine::p04173()
{
    sti(015);
    sti(015);
    sti(002);
    return registers_[015];
}

std::uint16_t Machine::p04175()
{
    accumulator_ = memory_[address_add(registers_[002], 0103)];
    select_alu_group(rau_logical);
    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0634)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        const std::uint16_t continuation =
            address_add(registers_[002], 0443);
        return continuation == 04201 ? p04201() : continuation;
    }

    registers_[016] = 03570;
    return p04177();
}

std::uint16_t Machine::p04177()
{
    registers_[015] = 04202;
    return registers_[002];
}

std::uint16_t Machine::p04200()
{
    registers_[016] = 03544;
    const std::uint16_t continuation =
        address_add(registers_[002], 0441);
    return continuation == 04177 ? p04177() : continuation;
}

std::uint16_t Machine::p04201()
{
    registers_[016] = 03564;
    registers_[015] = 04202;
    return 03536;
}

std::uint16_t Machine::p04202()
{
    if (registers_[016] == 0) {
        return p04213();
    }

    registers_[016] = memory_[04464].address();
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[002], 0650)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04210();
    }

    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0636)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        const std::uint16_t continuation =
            address_add(registers_[002], 0455);
        return continuation == 04213 ? p04213() : continuation;
    }

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0633)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    const std::uint16_t continuation =
        address_add(registers_[002], 0435);
    return continuation == 04173 ? p04173() : continuation;
}

std::uint16_t Machine::p04210()
{
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0656)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    const std::uint16_t continuation =
        address_add(registers_[002], 0435);
    return continuation == 04173 ? p04173() : continuation;
}

std::uint16_t Machine::p04212()
{
    registers_[016] = 04040;
    registers_[015] = 04213;
    return 03014;
}

std::uint16_t Machine::p04213()
{
    registers_[016] = 04060;
    return 03014;
}

std::uint16_t Machine::p04322()
{
    // 04322 first recognizes the common already-classified value.  Preserve
    // the original logical masks and the RMR value produced by each branch.
    registers_[014] = 03536;
    registers_[016] = 1;
    accumulator_ = memory_[address_add(registers_[014], 0101)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[014], 0663)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[014], 0664)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return registers_[015];
    }

    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();
    accumulator_ = memory_[address_add(registers_[014], 0103)];
    select_alu_group(rau_logical);
    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[014], 0665)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04335();
    }

    registers_[015] = 04330;
    return 04467;
}

std::uint16_t Machine::p04330()
{
    registers_[014] = 03536;
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04334();
    }

    accumulator_ = memory_[address_add(registers_[014], 0105)];
    select_alu_group(rau_logical);
    const Word48 compared = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[014], 0666)].raw());
    remainder_ = compared;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04343();
    }

    accumulator_ = memory_[address_add(registers_[014], 0105)];
    select_alu_group(rau_logical);
    const Word48 secondary = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[014], 0630)].raw());
    remainder_ = secondary;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() != 0 ? p04354() : p04334();
}

std::uint16_t Machine::p04334()
{
    registers_[016] = 04000;
    return 03014;
}

std::uint16_t Machine::p04335()
{
    accumulator_ = memory_[address_add(registers_[014], 0105)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04354();
    }

    const Word48 first = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[014], 0667)].raw());
    remainder_ = first;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04354();
    }

    accumulator_ = memory_[address_add(registers_[014], 0105)];
    select_alu_group(rau_logical);
    const Word48 second = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[014], 0666)].raw());
    remainder_ = second;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04343();
    }

    registers_[016] = 1;
    hardware_pop_acc();
    select_alu_group(rau_logical);
    registers_[015] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p04343()
{
    registers_[013] =
        memory_[address_add(registers_[014], 0103)].address();
    const std::uint16_t slot = address_add(registers_[013], -1);
    accumulator_ = memory_[slot];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[014], 0641)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[slot].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    memory_[slot] = accumulator_;
    registers_[016] = 04357;
    registers_[014] = 020;
    registers_[015] = 04350;
    return 016313;
}

std::uint16_t Machine::p04350()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    registers_[015] = 04351;
    return 03275;
}

std::uint16_t Machine::p04351()
{
    registers_[016] = 07667;
    registers_[015] = 04352;
    return 02764;
}

std::uint16_t Machine::p04352()
{
    accumulator_ = memory_[address_add(registers_[007], 01007)];
    select_alu_group(rau_logical);
    registers_[015] = 04353;
    return 03275;
}

std::uint16_t Machine::p04353()
{
    registers_[016] = 07601;
    registers_[015] = 04354;
    return 02764;
}

std::uint16_t Machine::p04354()
{
    registers_[016] = 0;
    hardware_pop_acc();
    select_alu_group(rau_logical);
    registers_[015] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p04426()
{
    // 04426..04434 recognizes either of two exact record-field markers and
    // replaces it with the paired table value.  Keep the XOR/RMR state on
    // ordinary mismatches instead of reducing this to a host comparison.
    registers_[013] = 04426;

    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 first_field = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[013], 076)].raw());
    remainder_ = first_field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[address_add(registers_[013], 077)];
        select_alu_group(rau_logical);
    } else {
        accumulator_ = memory_[address_add(registers_[003], 2)];
        select_alu_group(rau_logical);
        const Word48 second_field = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[013], 0100)].raw());
        remainder_ = second_field;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            return registers_[015];
        }
        accumulator_ = memory_[address_add(registers_[013], 0101)];
        select_alu_group(rau_logical);
    }

    memory_[address_add(registers_[003], 2)] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p04447()
{
    // Save the incoming value and the preceding stack word in the two
    // original scratch cells, then build the first argument for the already
    // translated two-word allocator at 05215.
    registers_[013] = 04426;
    stx(address_add(registers_[013], 057));
    memory_[address_add(registers_[013], 060)] = accumulator_;
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();

    accumulator_ = memory_[address_add(registers_[013], 060)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[address_add(registers_[013], 020)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    shift_accumulator(-24);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[013], 057)].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    xts(0);
    registers_[015] = 04455;
    return 05215;
}

std::uint16_t Machine::p04455()
{
    registers_[013] = 04426;
    const std::uint16_t generated_slot =
        address_add(memory_[address_add(registers_[013], 036)].address(), 1);
    memory_[generated_slot] = accumulator_;
    memory_[address_add(registers_[013], 036)] = accumulator_;

    registers_[014] = 03645;
    accumulator_ = memory_[registers_[014]];
    select_alu_group(rau_logical);
    accumulator_ =
        cyclic_add(accumulator_, memory_[address_add(registers_[013], 0102)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    stx(registers_[014]);
    registers_[015] = accumulator_.address();

    accumulator_ = memory_[address_add(registers_[013], 035)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[007], 01163)] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p04467()
{
    // Preserve the incoming link exactly as ATX (r17), then enter the
    // already translated compiler/evaluator cluster.  That cluster returns
    // its result in the accumulator with link 04471.
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();
    registers_[015] = 04471;
    return 06343;
}

std::uint16_t Machine::p04471()
{
    memory_[registers_[003]] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    stx(address_add(registers_[003], 2));
    registers_[015] = accumulator_.address();

    registers_[013] = 04426;
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[address_add(registers_[013], 0104)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[013], 0105)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 5)] = accumulator_;
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04503();
    }

    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[013], 020)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 1)] = accumulator_;
    registers_[016] = memory_[address_add(registers_[003], 2)].address();
    accumulator_ = memory_[address_add(registers_[016], -1)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[address_add(registers_[013], 0106)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 4)] = accumulator_;
    return p04504();
}

std::uint16_t Machine::p04503()
{
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 2)] = accumulator_;
    return p04504();
}

std::uint16_t Machine::p04504()
{
    accumulator_ = memory_[address_add(registers_[003], 5)];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p04507()
{
    // The source is two cells below r17 after saving the caller. Its masked
    // low address becomes the continuation selected at 04515.
    its(015);
    // XTS evaluates -2(r17) after its implicit push, so the precomputed
    // address passed to the helper is one cell below the current r17.
    xts(address_add(registers_[017], -1));
    registers_[013] = 04426;
    accumulator_ = accumulator_ & memory_[address_add(registers_[013], 020)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[013], 0107)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    xts(0);
    registers_[015] = 04513;
    return 05215;
}

std::uint16_t Machine::p04513()
{
    registers_[013] = 04426;
    const std::uint16_t generated_slot =
        address_add(memory_[address_add(registers_[013], 036)].address(), 1);
    memory_[generated_slot] = accumulator_;
    stx(address_add(registers_[013], 036));
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p04536()
{
    // Preserve the three-word compiler frame and the incoming accumulator.
    // The table base installed in r2 is used by every generated branch below.
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(003);
    its(002);
    registers_[002] = 04536;
    xts(address_add(registers_[002], 0223));
    registers_[015] = 04541;
    return 017340;
}

std::uint16_t Machine::p04541()
{
    registers_[003] = 03637;
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 field = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[002], 0224)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04544();
    }

    registers_[015] = 04614;
    return 03461;
}

std::uint16_t Machine::p04544()
{
    accumulator_ = memory_[address_add(registers_[003], 5)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04600();
    }

    const auto equals_table_word = [this](std::uint16_t offset) {
        accumulator_ = memory_[address_add(registers_[003], 2)];
        select_alu_group(rau_logical);
        const Word48 field = accumulator_;
        accumulator_ =
            Word48(accumulator_.raw() ^
                   memory_[address_add(registers_[002], offset)].raw());
        remainder_ = field;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        return accumulator_.raw() == 0;
    };

    if (equals_table_word(0225)) {
        return address_add(registers_[002], 066);
    }
    if (equals_table_word(0226) || equals_table_word(0227) ||
        equals_table_word(0230)) {
        return p04623();
    }

    accumulator_ = memory_[address_add(registers_[003], 4)];
    select_alu_group(rau_logical);
    const Word48 fourth_field = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[002], 0231)].raw());
    remainder_ = fourth_field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04600();
    }

    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[002], 062)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[002], 063)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[003], 4)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[002], 064)] = accumulator_;
    registers_[015] = 04561;
    return 04467;
}

std::uint16_t Machine::p04561()
{
    accumulator_ = memory_[address_add(registers_[002], 0232)];
    select_alu_group(rau_logical);
    const Word48 table_word = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[003], 2)].raw());
    remainder_ = table_word;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return 04721;
    }

    xts(registers_[003]);
    xts(address_add(registers_[003], 2));
    xts(address_add(registers_[003], 4));
    xts(address_add(registers_[002], 062));
    memory_[registers_[003]] = accumulator_;
    accumulator_ = memory_[address_add(registers_[002], 063)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 2)] = accumulator_;
    memory_[address_add(registers_[003], 3)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[002], 064)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[003], 4)] = accumulator_;
    registers_[015] = 04571;
    return 04322;
}

std::uint16_t Machine::p04571()
{
    if (registers_[016] != 0) {
        registers_[015] = 04576;
        return 04426;
    }
    registers_[016] = 03564;
    return p04572();
}

std::uint16_t Machine::p04572()
{
    stx(address_add(registers_[003], 4));
    stx(address_add(registers_[003], 4));
    stx(address_add(registers_[003], 2));
    stx(registers_[003]);
    const std::uint16_t continuation = address_add(registers_[002], 043);
    return continuation == 04601 ? p04601() : continuation;
}

std::uint16_t Machine::p04576()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    registers_[015] = 04577;
    return 017340;
}

std::uint16_t Machine::p04577()
{
    registers_[016] = 03542;
    const std::uint16_t continuation = address_add(registers_[002], 034);
    return continuation == 04572 ? p04572() : continuation;
}

std::uint16_t Machine::p04600()
{
    registers_[016] = 03544;
    return p04601();
}

std::uint16_t Machine::p04601()
{
    registers_[015] = 04602;
    return 03536;
}

std::uint16_t Machine::p04602()
{
    registers_[003] = 03637;
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 field = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[002], 0233)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return address_add(registers_[002], 074);
    }
    registers_[015] = 04605;
    return 04665;
}

std::uint16_t Machine::p04605()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 field = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[002], 0234)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04614();
    }
    registers_[015] = 04607;
    return 04467;
}

std::uint16_t Machine::p04607()
{
    registers_[015] = 04610;
    return 04161;
}

std::uint16_t Machine::p04610()
{
    const std::uint16_t continuation = address_add(registers_[002], 047);
    return continuation == 04605 ? p04605() : continuation;
}

std::uint16_t Machine::p04611()
{
    accumulator_ = memory_[address_add(registers_[002], 0235)];
    select_alu_group(rau_logical);
    stx(address_add(registers_[003], 2));
    return p04612();
}

std::uint16_t Machine::p04612()
{
    sti(002);
    sti(003);
    registers_[015] = accumulator_.address();
    return 017337;
}

std::uint16_t Machine::p04614()
{
    registers_[015] = 04615;
    return 017253;
}

std::uint16_t Machine::p04615()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    if (registers_[016] == 0) {
        return p04612();
    }
    registers_[016] = 04700;
    registers_[015] = 04617;
    return 03014;
}

std::uint16_t Machine::p04623()
{
    registers_[015] = 04611;
    return 03461;
}

std::uint16_t Machine::p04665()
{
    // 04665..04666 replaces ACC with r2, then saves that compiler base and
    // the caller. The r0-indexed short instructions use architectural zero.
    accumulator_ = Word48(registers_[002]);
    select_alu_group(rau_logical);
    its(015);
    its(015);
    registers_[002] = 04536;
    return p04667();
}

std::uint16_t Machine::p04667()
{
    // This is both the first entry and the re-entry after 04467.  Keep the
    // shared 03536 frame builder as an independent semantic boundary.
    registers_[016] = 03544;
    registers_[015] = 04670;
    return 03536;
}

std::uint16_t Machine::p04670()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 field = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0244)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);

    // U1A copies the comparison result to RMR on either path.  A mismatch
    // selects the r2-relative restoration block; equality revisits this
    // wrapper after the independent 04467 call.
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return address_add(registers_[002], 0135);
    }
    registers_[015] = 04667;
    return 04467;
}

std::uint16_t Machine::p04673()
{
    // Restore the link and compiler base from the two words installed by
    // 04665.  ATI r2 then copies that base without another stack pop.
    constexpr std::size_t first_target = 015;
    sti(first_target);
    sti(015);
    registers_[002] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p04675()
{
    accumulator_ = Word48(registers_[002]);
    select_alu_group(rau_logical);
    its(015);
    its(015);
    registers_[002] = 04536;
    return p04677();
}

std::uint16_t Machine::p04677()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 field = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0235)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04702();
    }
    registers_[015] = 04677;
    return 04467;
}

std::uint16_t Machine::p04702()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    const Word48 field = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0245)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04711();
    }

    accumulator_ = memory_[03504];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04710();
    }
    accumulator_ = memory_[address_add(registers_[002], 0245)];
    select_alu_group(rau_logical);
    return p04706();
}

std::uint16_t Machine::p04706()
{
    xts(address_add(registers_[002], 0241));
    registers_[015] = 04707;
    return 04447;
}

std::uint16_t Machine::p04707()
{
    registers_[015] = 04677;
    return 04467;
}

std::uint16_t Machine::p04710()
{
    accumulator_ = memory_[address_add(registers_[002], 0246)];
    select_alu_group(rau_logical);
    return p04706();
}

std::uint16_t Machine::p04711()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    Word48 field = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[003], 032)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04730();
    }

    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    field = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[003], 033)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04755();
    }

    registers_[015] = 04715;
    return 017253;
}

std::uint16_t Machine::p04715()
{
    if (registers_[016] == 0) {
        return p04717();
    }
    registers_[015] = 04677;
    return registers_[002];
}

std::uint16_t Machine::p04717()
{
    sti(015);
    sti(015);
    registers_[002] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p04721()
{
    accumulator_ = memory_[address_add(registers_[002], 062)];
    select_alu_group(rau_logical);
    registers_[015] = 04722;
    return 04740;
}

std::uint16_t Machine::p04722()
{
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04726();
    }

    accumulator_ = memory_[address_add(registers_[003], 6)];
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    registers_[015] = 04725;
    return 04507;
}

std::uint16_t Machine::p04725()
{
    registers_[015] = 04544;
    return 04467;
}

std::uint16_t Machine::p04726()
{
    registers_[016] = 015100;
    accumulator_ = memory_[address_add(registers_[002], 062)];
    select_alu_group(rau_logical);
    registers_[015] = 04730;
    return 03014;
}

std::uint16_t Machine::p04730()
{
    registers_[015] = 04731;
    return 04467;
}

std::uint16_t Machine::p04731()
{
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p04737();
    }

    accumulator_ = memory_[address_add(registers_[003], 4)];
    select_alu_group(rau_logical);
    const Word48 field = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 0231)].raw());
    remainder_ = field;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p04737();
    }

    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    registers_[015] = 04734;
    return 04740;
}

std::uint16_t Machine::p04734()
{
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    xts(address_add(registers_[002], 0247));
    registers_[015] = 04736;
    return 04447;
}

std::uint16_t Machine::p04736()
{
    registers_[015] = 04677;
    return 04467;
}

std::uint16_t Machine::p04737()
{
    registers_[016] = 04140;
    return 03014;
}

std::uint16_t Machine::p04740()
{
    its(015);
    xts(address_add(registers_[007], 01167));
    remainder_ = accumulator_;
    return accumulator_.raw() == 0 ? p04746() : p04742();
}

std::uint16_t Machine::p04742()
{
    for (;;) {
        registers_[014] = accumulator_.address();
        accumulator_ = memory_[registers_[014]];
        select_alu_group(rau_logical);
        registers_[016] = accumulator_.address();
        accumulator_ = memory_[address_add(registers_[016], 1)];
        select_alu_group(rau_logical);
        const Word48 value = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[017], -2)].raw());
        remainder_ = value;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return p04753();
        }

        accumulator_ = memory_[address_add(registers_[014], 1)];
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return p04746();
        }
    }
}

std::uint16_t Machine::p04746()
{
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    // XTS -3(r17) evaluates its operand after pushing the zero accumulator.
    xts(address_add(registers_[017], -2));
    registers_[015] = 04750;
    return 05215;
}

std::uint16_t Machine::p04750()
{
    xts(address_add(registers_[007], 01167));
    registers_[015] = 04751;
    return 05215;
}

std::uint16_t Machine::p04751()
{
    memory_[address_add(registers_[007], 01167)] = accumulator_;
    registers_[016] = accumulator_.address();
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    registers_[016] = accumulator_.address();
    return p04753();
}

std::uint16_t Machine::p04753()
{
    // XTA (r17) is the stack-pop form: decrement r17 before loading.
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p04755()
{
    xts(address_add(registers_[002], 0250));
    registers_[015] = 04756;
    return 04447;
}

std::uint16_t Machine::p04756()
{
    registers_[015] = 04677;
    return 04467;
}

std::uint16_t Machine::p05007()
{
    its(001);
    its(015);
    its(002);
    its(0);
    registers_[001] = 05007;
    accumulator_ = memory_[05203];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p05014();
    }
    registers_[016] = 0;
    registers_[015] = 05014;
    return 017623;
}

std::uint16_t Machine::p05014()
{
    accumulator_ = memory_[03504];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p05021();
    }

    accumulator_ = memory_[05202];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p05021();
    }
    registers_[015] = 05017;
    return 017624;
}

std::uint16_t Machine::p05017()
{
    accumulator_ = memory_[05203];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p05021();
    }
    registers_[016] = 0;
    registers_[015] = 05021;
    return 017623;
}

std::uint16_t Machine::p05021()
{
    registers_[016] = 5;
    registers_[015] = 05022;
    return 05430;
}

std::uint16_t Machine::p05022()
{
    registers_[002] = registers_[016];
    accumulator_ = Word48(registers_[002]);
    select_alu_group(rau_logical);
    shift_accumulator(-24);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[05201].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    memory_[05206] = accumulator_;
    registers_[016] = 017560;
    registers_[015] = 05026;
    return 05160;
}

std::uint16_t Machine::p05026()
{
    memory_[address_add(registers_[002], 3)] = accumulator_;
    registers_[016] = 017577;
    registers_[015] = 05030;
    return 05160;
}

std::uint16_t Machine::p05030()
{
    memory_[address_add(registers_[002], 4)] = accumulator_;
    accumulator_ = accumulator_ & memory_[05174];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(accumulator_, memory_[05175]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    shift_accumulator(-24);
    memory_[05205] = accumulator_;
    // UTC 017577 modifies the following XTA 0 with the literal address;
    // unlike WTC, it does not dereference the word at 017577.
    accumulator_ = memory_[017577];
    select_alu_group(rau_logical);
    registers_[015] = 05034;
    return 03531;
}

std::uint16_t Machine::p05034()
{
    registers_[016] = 017577;
    registers_[015] = 05035;
    return 017614;
}

std::uint16_t Machine::p05035()
{
    // UTC 03645 supplies the literal address to the following XTA 0.
    accumulator_ = memory_[03645];
    select_alu_group(rau_logical);
    memory_[registers_[002]] = accumulator_;
    registers_[016] = accumulator_.address();
    registers_[015] = 05040;
    return 05430;
}

std::uint16_t Machine::p05040()
{
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    const Word48 address_word = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[05206].raw());
    remainder_ = address_word;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[017], -4)] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[05206] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(accumulator_, memory_[05176]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[05204] = accumulator_;

    registers_[002] = 04462;
    registers_[013] = registers_[016];
    return p05045();
}

std::uint16_t Machine::p05045()
{
    // 05045..05051 selects the generated continuation encoded in the word
    // reached through r2. A zero address retains the original 05143 exit.
    accumulator_ = memory_[address_add(registers_[002], 1)];
    select_alu_group(rau_logical);
    registers_[002] = accumulator_.address();
    if (registers_[002] == 0) {
        return 05143;
    }
    accumulator_ = memory_[registers_[002]];
    select_alu_group(rau_logical);
    shift_accumulator(39);
    registers_[011] = accumulator_.address();
    registers_[012] = registers_[011];
    registers_[011] = address_add(registers_[011], 05077);
    return address_add(registers_[012], 05052);
}

std::uint16_t Machine::p05160()
{
    its(015);
    its(002);
    its(003);
    registers_[002] = registers_[016];
    xts(address_add(registers_[016], 2));
    registers_[015] = 05163;
    return 011464;
}

std::uint16_t Machine::p05163()
{
    // Replace the saved input with 11464's result, restore r3, and copy the
    // selected address chain through consecutive r3 words.
    memory_[address_add(registers_[017], -4)] = accumulator_;
    registers_[002] = address_add(registers_[002], -1);
    registers_[003] = accumulator_.address();

    for (;;) {
        accumulator_ = memory_[address_add(registers_[002], 1)];
        select_alu_group(rau_logical);
        registers_[002] = accumulator_.address();
        if (registers_[002] == 0) {
            break;
        }

        registers_[003] = address_add(registers_[003], 1);
        accumulator_ = memory_[registers_[002]];
        select_alu_group(rau_logical);
        memory_[registers_[003]] = accumulator_;

        const std::uint16_t continuation = address_add(registers_[001], 0156);
        if (continuation != 05165) {
            return continuation;
        }
    }

    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(003);
    sti(002);
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p05207()
{
    registers_[016] = accumulator_.address();
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p05211()
{
    registers_[016] = accumulator_.address();
    accumulator_ = memory_[address_add(registers_[016], 1)];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p05213()
{
    registers_[010] = 05213;
    xts(05226);
    return p05217();
}

std::uint16_t Machine::p05215()
{
    // 05215..05220: save the incoming value, tag constant, and caller link,
    // then request the original two-word allocation through 05430.
    registers_[010] = 05213;
    xts(05227);
    return p05217();
}

std::uint16_t Machine::p05217()
{
    its(015);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[016] = 2;
    registers_[015] = 05221;
    return 05430;
}

std::uint16_t Machine::p05221()
{
    // 05221..05225: initialize the allocated pair from the caller frame,
    // tag its address, discard the four frame words, and return through the
    // saved link selected by the original WTC/UJ pair.
    registers_[010] = 05213;
    accumulator_ = memory_[address_add(registers_[017], -4)];
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    accumulator_ = memory_[address_add(registers_[017], -3)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 1)] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[017], -2)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    registers_[017] = address_add(registers_[017], -4);
    return memory_[address_add(registers_[017], 3)].address();
}

std::uint16_t Machine::p05230()
{
    accumulator_ = Word48(registers_[001]);
    select_alu_group(rau_logical);
    registers_[001] = 05230;
    memory_[address_add(registers_[001], 0150)] = accumulator_;
    accumulator_ = Word48(registers_[003]);
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0152)] = accumulator_;
    registers_[003] = 0;
    accumulator_ = Word48(registers_[002]);
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0151)] = accumulator_;
    accumulator_ = Word48(registers_[004]);
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0153)] = accumulator_;
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0154)] = accumulator_;
    registers_[004] = memory_[01000].address();
    registers_[015] = 05240;
    return 020263;
}

std::uint16_t Machine::p05240()
{
    accumulator_ = memory_[address_add(registers_[001], 0136)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0141)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0137)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0142)] = accumulator_;
    if (registers_[003] != 0) {
        return p05252();
    }

    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0147)] = accumulator_;
    accumulator_ = memory_[01000];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0124)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p05252();
    }

    registers_[015] = 05250;
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    registers_[015] = 05375;
    its(015);
    registers_[015] = 05250;
    return 00564;
}

std::uint16_t Machine::p05250()
{
    stx(address_add(registers_[001], 0147));
    stx(address_add(registers_[001], 0142));
    const Word48 saved = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0143)].raw());
    remainder_ = saved;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0141)] = accumulator_;
    return p05252();
}

std::uint16_t Machine::p05252()
{
    if (registers_[003] == 0) {
        registers_[017] = address_add(memory_[017010].address(), 1);
    }
    return p05254();
}

std::uint16_t Machine::p05254()
{
    if (registers_[003] != 0) {
        return p05255();
    }
    registers_[015] = 05255;
    return 020660;
}

std::uint16_t Machine::p05255()
{
    if (registers_[003] == 0) {
        for (unsigned offset = 0; offset != 7; ++offset) {
            accumulator_ = memory_[address_add(
                registers_[004], static_cast<int>(offset) + 1)];
            select_alu_group(rau_logical);
            memory_[address_add(020647, static_cast<int>(offset))] =
                accumulator_;
        }
        registers_[016] = 0;
    }
    return p05261();
}

std::uint16_t Machine::p05261()
{
    registers_[006] = memory_[017011].address();
    registers_[016] = 020571;
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0125)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 first = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017012].raw());
    remainder_ = first;
    select_alu_group(rau_logical);
    const Word48 second = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0126)].raw());
    remainder_ = second;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    registers_[015] = 05266;
    return 020144;
}

std::uint16_t Machine::p05266()
{
    accumulator_ = memory_[017011];
    select_alu_group(rau_logical);
    shift_accumulator(-26);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0127)].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0146)] = accumulator_;

    // The C++ execution layer accepts this formatted-output E72 operation as
    // a no-op while retaining its architectural effective address and group.
    registers_[016] = address_add(registers_[001], 0146);
    select_alu_group(rau_logical);
    if (registers_[003] == 0) {
        accumulator_ = memory_[address_add(registers_[004], 012)];
        select_alu_group(rau_logical);
        memory_[020375] = accumulator_;
    }
    return p05273();
}

std::uint16_t Machine::p05273()
{
    registers_[016] = 05370;
    accumulator_ = memory_[020377];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        registers_[016] = 05365;
    }
    registers_[014] = 01200;
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[014], 0317)] = accumulator_;
    memory_[address_add(registers_[014], 0367)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[016], 1)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[014], 0313)] = accumulator_;
    memory_[address_add(registers_[014], 0363)] = accumulator_;
    accumulator_ = memory_[01513];
    select_alu_group(rau_logical);
    memory_[020666] = accumulator_;

    registers_[016] = 012553;
    accumulator_ = memory_[address_add(registers_[001], 0141)];
    select_alu_group(rau_logical);
    shift_accumulator(-24);
    Word48 before = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[registers_[016]].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0130)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    before = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[registers_[016]].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;

    registers_[016] = 012555;
    accumulator_ = memory_[address_add(registers_[001], 0142)];
    select_alu_group(rau_logical);
    shift_accumulator(-24);
    before = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[registers_[016]].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0130)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    before = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[registers_[016]].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;

    accumulator_ = memory_[registers_[004]];
    select_alu_group(rau_logical);
    registers_[015] = 05314;
    return 016005;
}

std::uint16_t Machine::p05314()
{
    memory_[03175] = accumulator_;
    registers_[002] = 017003;
    return p05316();
}

std::uint16_t Machine::p05316()
{
    for (;;) {
        registers_[002] = address_add(registers_[002], 1);
        accumulator_ = memory_[registers_[002]];
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return p05326();
        }
        accumulator_ = accumulator_
            & memory_[address_add(registers_[001], 0131)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            return 05353;
        }
        accumulator_ = Word48(registers_[002]);
        select_alu_group(rau_logical);
        const Word48 register_word = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[001], 0132)].raw());
        remainder_ = register_word;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return 05353;
        }

        accumulator_ = memory_[registers_[002]];
        select_alu_group(rau_logical);
        registers_[016] = 0200;
        accumulator_ = Word48();
        select_alu_group(rau_logical);
        const Word48 cleared = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[001], 0133)].raw());
        remainder_ = cleared;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            continue;
        }
        accumulator_ = memory_[registers_[002]];
        select_alu_group(rau_logical);
        registers_[015] = 05316;
        return 016145;
    }
}

std::uint16_t Machine::p05326()
{
    if (registers_[003] != 0) {
        return p05335();
    }
    accumulator_ = memory_[address_add(registers_[001], 0147)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p05335();
    }

    accumulator_ = memory_[address_add(registers_[001], 0144)];
    select_alu_group(rau_logical);
    xts(045);
    shift_accumulator(-13);
    shift_accumulator(43);
    shift_accumulator(-10);
    const std::uint16_t selected =
        memory_[address_add(registers_[001], 0147)].address();
    memory_[address_add(selected, 1)] = accumulator_;
    registers_[015] = 05334;
    return 016054;
}

std::uint16_t Machine::p05334()
{
    const std::uint16_t selected =
        memory_[address_add(registers_[001], 0147)].address();
    memory_[selected] = accumulator_;
    return p05335();
}

std::uint16_t Machine::p05335()
{
    if (registers_[003] != 0) {
        return p05336();
    }
    registers_[015] = 05336;
    return 020673;
}

std::uint16_t Machine::p05336()
{
    registers_[015] = 05337;
    return 020456;
}

std::uint16_t Machine::p05337()
{
    if (registers_[003] == 0) {
        accumulator_ = memory_[address_add(registers_[001], 0134)];
        select_alu_group(rau_logical);
        memory_[013450] = accumulator_;
    }
    return p05341();
}

std::uint16_t Machine::p05341()
{
    accumulator_ = memory_[address_add(registers_[001], 0154)];
    select_alu_group(rau_logical);
    registers_[015] = accumulator_.address();
    accumulator_ = memory_[address_add(registers_[001], 0153)];
    select_alu_group(rau_logical);
    registers_[004] = accumulator_.address();
    accumulator_ = memory_[address_add(registers_[001], 0152)];
    select_alu_group(rau_logical);
    registers_[003] = accumulator_.address();
    accumulator_ = memory_[address_add(registers_[001], 0151)];
    select_alu_group(rau_logical);
    registers_[002] = accumulator_.address();
    accumulator_ = memory_[address_add(registers_[001], 0150)];
    select_alu_group(rau_logical);
    registers_[001] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p05405()
{
    registers_[010] = 05405;
    accumulator_ = memory_[05407];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() == 0 ? registers_[015] : 02750;
}

std::uint16_t Machine::p05410()
{
    accumulator_ = Word48(registers_[001]);
    select_alu_group(rau_logical);
    its(015);
    xts(address_add(registers_[007], 01077));
    xts(address_add(registers_[007], 0647));
    memory_[address_add(registers_[007], 01077)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[007], 01167)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() == 0 ? p05422() : p05414();
}

std::uint16_t Machine::p05414()
{
    registers_[001] = accumulator_.address();
    const std::uint16_t selected = memory_[registers_[001]].address();
    accumulator_ = memory_[selected];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() == 0 ? p05425() : p05416();
}

std::uint16_t Machine::p05416()
{
    accumulator_ = memory_[address_add(registers_[001], 1)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p05414();
    }

    accumulator_ = memory_[address_add(registers_[007], 01077)];
    select_alu_group(rau_logical);
    const Word48 saved = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[007], 0647)].raw());
    remainder_ = saved;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p05422();
    }

    accumulator_ = memory_[address_add(registers_[007], 01077)];
    select_alu_group(rau_logical);
    registers_[016] = 015000;
    return 03014;
}

std::uint16_t Machine::p05422()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    stx(address_add(registers_[007], 01077));
    sti(015);
    registers_[001] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p05425()
{
    const std::uint16_t selected = memory_[registers_[001]].address();
    accumulator_ = memory_[address_add(selected, 1)];
    select_alu_group(rau_logical);
    xts(address_add(registers_[007], 01077));
    registers_[015] = 05427;
    return 05215;
}

std::uint16_t Machine::p05427()
{
    memory_[address_add(registers_[007], 01077)] = accumulator_;
    return p05416();
}

std::uint16_t Machine::p03413_numeric_update()
{
    // 03413..03415: preserve the input on the hardware stack and normalize
    // it through the original additive instruction in NTR 6 mode.
    registers_[013] = 03310;
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    alu_mode_ = 006;
    arithmetic_add(memory_[0], false, false);
    memory_[03451] = accumulator_;

    // 03415..03420: form the quotient and its tagged correction using the
    // constants addressed relative to r13. Keep BESM normalization and
    // rounding behavior rather than replacing this with host arithmetic.
    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    divide(memory_[03451]);
    arithmetic_add(memory_[03457], false, false);
    alu_mode_ = 007;
    arithmetic_add(memory_[03453], false, false);
    memory_[03451] = accumulator_;

    // 03420..03423: both arithmetic operands use the r17 stack addressing
    // side effect. XTS leaves the computed difference in RMR and returns the
    // saved tagged result in the accumulator.
    alu_mode_ = 006;
    hardware_pop_acc();
    const Word48 multiplier = accumulator_;
    accumulator_ = memory_[03451];
    multiply(multiplier);
    alu_mode_ = 007;
    const Word48 product = accumulator_;
    hardware_pop_acc();
    const Word48 minuend = accumulator_;
    accumulator_ = product;
    reverse_subtract(minuend);
    xts(03451);
    return registers_[015];
}

std::uint16_t Machine::p05430()
{
    // 05430 is the zero-size fast return. Nonzero requests preserve the
    // caller link and requested word count before entering 05447.
    if (registers_[016] == 0) {
        return registers_[015];
    }
    accumulator_ = Word48(registers_[015]);
    its(016);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[015] = 05433;
    return 05447;
}

std::uint16_t Machine::p05433()
{
    // A successful 05447 result selects the size-specific continuation.
    // Failure invokes the original storage-recovery routine and retries.
    if (registers_[016] != 0) {
        return 05436;
    }
    registers_[015] = 05434;
    return 05523;
}

std::uint16_t Machine::p05434()
{
    accumulator_ = memory_[address_add(registers_[017], -1)];
    select_alu_group(rau_logical);
    registers_[015] = 05435;
    return 05447;
}

std::uint16_t Machine::p05435()
{
    return registers_[016] == 0 ? 05440 : 05436;
}

std::uint16_t Machine::p05436()
{
    // UTM -1 followed by WTC (r17) performs two decrements: the second is
    // the hardware-stack addressing side effect which exposes the caller
    // link saved by ITS at 05431.
    registers_[017] = address_add(registers_[017], -2);
    return memory_[registers_[017]].address();
}

std::uint16_t Machine::p05440()
{
    // Preserve the historical out-of-memory diagnostic boundary.
    registers_[016] = 05442;
    registers_[015] = 05441;
    return 020674;
}

std::uint16_t Machine::p05441()
{
    return 020715;
}

std::uint16_t Machine::p05447()
{
    // 05447..05454 first checks the size-specific free-list head. Requests
    // at or above the table limit use the common list beginning at 05502.
    registers_[011] = 05447;
    registers_[010] = accumulator_.address();
    alu_mode_ = 003;
    arithmetic_add(memory_[06102], false, true);

    bool use_common_list = (accumulator_.raw() & bit41) == 0;
    if (!use_common_list) {
        registers_[010] = address_add(registers_[010], 05502);
        registers_[016] = memory_[registers_[010]].address();
        if (registers_[016] != 0) {
            accumulator_ = memory_[registers_[016]] & memory_[06103];
            remainder_ = Word48();
            select_alu_group(rau_logical);
            memory_[registers_[010]] = accumulator_;
        } else {
            use_common_list = true;
        }
    }

    if (use_common_list) {
        // 05463..05501 walks the address-linked common list. A larger block
        // is split in place; an exact block is unlinked without changing its
        // contents beyond the original masked link fields.
        registers_[016] = 05502;
        for (;;) {
            accumulator_ = memory_[registers_[016]];
            select_alu_group(rau_logical);
            registers_[010] = accumulator_.address();
            if (registers_[010] == 0) {
                registers_[016] = 0;
                return registers_[015];
            }

            accumulator_ = memory_[registers_[010]];
            select_alu_group(rau_logical);
            shift_accumulator(24);
            arithmetic_add(memory_[address_add(registers_[017], -1)],
                           false, true);
            if ((accumulator_.raw() & bit41) != 0) {
                registers_[016] = registers_[010];
                continue;
            }

            const bool exact = accumulator_.raw() == 0;
            if (exact) {
                accumulator_ = memory_[registers_[010]] & memory_[06103];
                remainder_ = Word48();
                select_alu_group(rau_logical);
                const Word48 predecessor = accumulator_;
                accumulator_ = memory_[registers_[016]] & memory_[06104];
                remainder_ = Word48();
                select_alu_group(rau_logical);
                accumulator_ = Word48(
                    accumulator_.raw() ^ predecessor.raw());
                select_alu_group(rau_logical);
                memory_[registers_[016]] = accumulator_;
                registers_[016] = registers_[010];
            } else {
                registers_[016] = accumulator_.address();
                modifier_add(016, 010);
                shift_accumulator(-24);
                const Word48 shortened_size = accumulator_;
                accumulator_ = memory_[registers_[010]] & memory_[06103];
                remainder_ = Word48();
                select_alu_group(rau_logical);
                accumulator_ = Word48(
                    accumulator_.raw() ^ shortened_size.raw());
                select_alu_group(rau_logical);
                memory_[registers_[010]] = accumulator_;
            }
            break;
        }
    }

    // 05455..05461 initializes every word in the selected block with the
    // original tagged template at 02213, working backward by request size.
    registers_[010] =
        memory_[address_add(registers_[017], -1)].address();
    accumulator_ = memory_[02213];
    select_alu_group(rau_logical);
    while (registers_[010] != 0) {
        const std::uint16_t target = address_add(
            registers_[016], address_add(registers_[010], -1));
        memory_[target] = accumulator_;
        registers_[010] = address_add(registers_[010], -1);
    }
    return registers_[015];
}

std::uint16_t Machine::p01107()
{
    // 01107..01121 builds the two-word working descriptor at 01171 and
    // derives the register setup used by the shared 01122 body.
    registers_[012] = 00100;
    registers_[013] = 00150;
    registers_[016] = 01171;
    memory_[registers_[016]] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 1)] = accumulator_;

    for (;;) {
        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        shift_accumulator(
            static_cast<int>(registers_[012] & 0177) - 64);
        shift_accumulator(
            static_cast<int>(registers_[013] & 0177) - 64);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            break;
        }
        accumulator_ = cyclic_add(
            accumulator_, memory_[address_add(registers_[016], 1)]);
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        memory_[address_add(registers_[016], 1)] = accumulator_;
        registers_[012] = address_add(registers_[012], -010);
        registers_[013] = address_add(registers_[013], -1);
    }

    accumulator_ = memory_[address_add(registers_[016], 1)]
        & memory_[address_add(registers_[016], 2)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    registers_[011] = 01200;
    registers_[012] = 4;
    registers_[013] = 1;
    return p01122_shared();
}

std::uint16_t Machine::p01167()
{
    // 01167..01170 is the alternate register setup for the shared 01122
    // hash-table body.  It has no accumulator or ALU side effects of its own.
    registers_[011] = 06143;
    registers_[012] = 2;
    registers_[013] = 0;
    return p01122_shared();
}

std::uint16_t Machine::p01122_shared()
{
    // Preserve the seven-word frame exactly: the last XTS word is consumed
    // first during the common 01160 restoration sequence.
    its(001);
    its(005);
    its(003);
    its(004);
    its(015);
    its(007);
    registers_[001] = 01120;
    // XTS evaluates its r17-relative operand after pushing the accumulator.
    xts(address_add(registers_[017], -6));
    shift_accumulator(1);
    registers_[003] = accumulator_.address();
    modifier_add(003, 011);
    registers_[007] = registers_[013];
    registers_[005] = 0;

    accumulator_ = memory_[address_add(registers_[017], -7)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 054)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        registers_[005] = 1;
    }

    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    if (registers_[005] == 0) {
        shift_accumulator(24);
    }
    registers_[004] = registers_[016];
    registers_[016] = accumulator_.address();

    if (registers_[016] == 0) {
        registers_[016] = registers_[012];
        registers_[015] = 01140;
        return 05430;
    }

    for (;;) {
        registers_[003] = registers_[016];
        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        const Word48 old_accumulator = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[registers_[004]].raw());
        remainder_ = old_accumulator;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return p01160_finish();
        }

        accumulator_ = memory_[address_add(
            registers_[016], address_add(registers_[007], 1))];
        select_alu_group(rau_logical);
        registers_[016] = accumulator_.address();
        if (registers_[016] != 0) {
            continue;
        }
        registers_[016] = registers_[012];
        registers_[015] = 01151;
        return 05430;
    }
}

std::uint16_t Machine::p01140()
{
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    if (registers_[005] == 0) {
        shift_accumulator(-24);
    }
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[003]].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    memory_[registers_[003]] = accumulator_;
    return p01154_finish();
}

std::uint16_t Machine::p01151()
{
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    const std::uint16_t slot = address_add(
        registers_[003], address_add(registers_[007], 1));
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[slot].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    memory_[slot] = accumulator_;
    return p01154_finish();
}

std::uint16_t Machine::p01154_finish()
{
    accumulator_ = memory_[registers_[004]];
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    if (registers_[007] == 0) {
        accumulator_ = memory_[0];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[016], 1)] = accumulator_;
        return p01160_finish();
    }

    accumulator_ = memory_[address_add(registers_[001], 055)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 2)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 056)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 1)] = accumulator_;
    memory_[address_add(registers_[016], 3)] = accumulator_;
    return p01160_finish();
}

std::uint16_t Machine::p01160_finish()
{
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[001], 057)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    stx(address_add(registers_[017], -7));
    sti(007);
    sti(015);
    sti(004);
    sti(003);
    sti(005);
    sti(001);
    return registers_[015];
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

std::uint16_t Machine::p02764()
{
    registers_[010] = 02745;
    its(016);
    const Word48 saved_register = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[address_add(registers_[010], 045)].raw());
    remainder_ = saved_register;
    select_alu_group(rau_logical);
    stx(03272);
    return 03261;
}

std::uint16_t Machine::p02767()
{
    // 02767 is the common indirect evaluator wrapper.
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    return 02750;
}

std::uint16_t Machine::p02770()
{
    // 02770 and 02774..03005 validate an indirect function cell, select its
    // environment word, and then re-enter the ordinary evaluator.
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    registers_[010] = 02745;
    memory_[03013] = accumulator_;

    accumulator_ = accumulator_ & memory_[03007];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked_tag = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[03012].raw());
    remainder_ = masked_tag;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        accumulator_ = memory_[03013];
        select_alu_group(rau_logical);
        registers_[016] = 012011;
        return 03014;
    }

    accumulator_ = memory_[03013];
    select_alu_group(rau_logical);
    shift_accumulator(24);
    registers_[016] = accumulator_.address();
    if (registers_[016] == 0) {
        accumulator_ = memory_[03011];
        select_alu_group(rau_logical);
        registers_[016] = 012010;
        return 03014;
    }

    accumulator_ = memory_[address_add(registers_[016], 2)];
    select_alu_group(rau_logical);
    return 02750;
}

std::uint16_t Machine::p01004()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();
    registers_[002] = 01001;
    registers_[003] = 03637;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[007], 01167)] = accumulator_;
    memory_[address_add(registers_[007], 01173)] = accumulator_;
    memory_[address_add(registers_[007], 01077)] = accumulator_;
    memory_[address_add(registers_[007], 01047)] = accumulator_;
    registers_[015] = 01011;
    return 05405;
}

std::uint16_t Machine::p01011()
{
    registers_[015] = 01012;
    return 04467;
}

std::uint16_t Machine::p01012()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    Word48 before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 065)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    registers_[015] = 01016;
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return 03461;
    }

    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 066)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return 03461;
    }
    registers_[015] = 01016;
    return 04536;
}

std::uint16_t Machine::p01016()
{
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    Word48 before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 067)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p01023();
    }

    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[002], 070)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return address_add(registers_[002], 027);
    }

    accumulator_ = memory_[address_add(registers_[002], 070)];
    select_alu_group(rau_logical);
    xts(address_add(registers_[002], 071));
    registers_[015] = 01023;
    return 04447;
}

std::uint16_t Machine::p01023()
{
    registers_[015] = 01024;
    return 05410;
}

std::uint16_t Machine::p01024()
{
    registers_[015] = 01025;
    return 05007;
}

std::uint16_t Machine::p01025()
{
    memory_[address_add(registers_[007], 01047)] = accumulator_;
    registers_[015] = 01026;
    return 02750;
}

std::uint16_t Machine::p01026()
{
    accumulator_ = memory_[address_add(registers_[007], 01047)];
    select_alu_group(rau_logical);
    registers_[015] = 01006;
    return 03521;
}

std::uint16_t Machine::p06134()
{
    // Preserve the original two-word frame around the alternate hash-table
    // entry at 01167. r16 retains the frame base for the final VTA.
    registers_[016] = registers_[017];
    its(015);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[010] = 06143;

    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    shift_accumulator(33);
    accumulator_ =
        cyclic_add(accumulator_, memory_[address_add(registers_[017], -2)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    accumulator_ = accumulator_ & memory_[address_add(registers_[010], 0423)];
    remainder_ = Word48();
    select_alu_group(rau_logical);

    registers_[015] = 06141;
    return 01167;
}

std::uint16_t Machine::p06141()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(015);
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p06343()
{
    its(005);
    its(007);
    its(015);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[005] = 06143;
    registers_[015] = 06346;
    return 06526;
}

std::uint16_t Machine::p06345()
{
    registers_[005] = 06143;
    registers_[015] = 06346;
    return 06526;
}

std::uint16_t Machine::p06346()
{
    memory_[address_add(registers_[017], -4)] = accumulator_;
    registers_[016] = accumulator_.address();
    accumulator_ = accumulator_
        & memory_[address_add(registers_[005], 0424)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[005], 0425)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p06352();
    }
    registers_[016] = 0;
    return p06360();
}

std::uint16_t Machine::p06352()
{
    accumulator_ = memory_[address_add(registers_[017], -4)];
    select_alu_group(rau_logical);
    const Word48 left = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[005], 0243)].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p06363();
    }
    registers_[015] = 06354;
    return 06424;
}

std::uint16_t Machine::p06354()
{
    registers_[016] = address_add(registers_[016], 1);
    accumulator_ = memory_[address_add(registers_[005], 0242)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p06360();
    }

    accumulator_ = memory_[address_add(registers_[016], -1)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[005], 0426)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[005], 0427)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() == 0 ? p06367() : p06360();
}

std::uint16_t Machine::p06360()
{
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    stx(address_add(registers_[005], 0242));
    sti(015);
    sti(007);
    sti(005);
    return registers_[015];
}

std::uint16_t Machine::p06363()
{
    accumulator_ = memory_[address_add(registers_[005], 0242)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[005], 0430)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[address_add(registers_[005], 0242)] = accumulator_;
    const Word48 left = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[005], 0430)].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p06345();
    }
    registers_[015] = 06354;
    return 06424;
}

std::uint16_t Machine::p06367()
{
    registers_[007] = registers_[016];
    accumulator_ = memory_[02047];
    select_alu_group(rau_logical);
    xts(address_add(registers_[005], 0244));
    registers_[015] = 06372;
    return 05215;
}

std::uint16_t Machine::p06372()
{
    memory_[address_add(registers_[005], 0244)] = accumulator_;
    accumulator_ = memory_[registers_[007]];
    select_alu_group(rau_logical);
    registers_[015] = 06374;
    return 02750;
}

std::uint16_t Machine::p06374()
{
    const std::uint16_t pair =
        memory_[address_add(registers_[005], 0244)].address();
    accumulator_ = memory_[pair];
    select_alu_group(rau_logical);
    registers_[007] = accumulator_.address();
    const Word48 left = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[005], 0341)].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p06403();
    }

    accumulator_ = memory_[registers_[007]];
    select_alu_group(rau_logical);
    xts(02117);
    registers_[015] = 06401;
    return 021125;
}

std::uint16_t Machine::p06375()
{
    registers_[007] = accumulator_.address();
    const Word48 left = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[005], 0341)].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p06403();
    }

    accumulator_ = memory_[registers_[007]];
    select_alu_group(rau_logical);
    xts(02117);
    registers_[015] = 06401;
    return 021125;
}

std::uint16_t Machine::p06401()
{
    memory_[02117] = accumulator_;
    accumulator_ = memory_[address_add(registers_[007], 1)];
    select_alu_group(rau_logical);
    return p06375();
}

std::uint16_t Machine::p06403()
{
    const std::uint16_t pair =
        memory_[address_add(registers_[005], 0244)].address();
    accumulator_ = memory_[address_add(pair, 1)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[005], 0244)] = accumulator_;
    return p06345();
}

std::uint16_t Machine::p06424()
{
    registers_[016] = address_add(registers_[016], 2);
    registers_[010] = 06444;

    for (;;) {
        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        accumulator_ =
            accumulator_ & memory_[address_add(registers_[010], 0125)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        const Word48 masked = accumulator_;
        accumulator_ =
            Word48(accumulator_.raw() ^
                   memory_[address_add(registers_[010], 0130)].raw());
        remainder_ = masked;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            return registers_[015];
        }

        registers_[014] = registers_[016];
        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        shift_accumulator(15);
        registers_[016] = accumulator_.address();
        if (registers_[016] != 0) {
            continue;
        }
        break;
    }

    its(007);
    its(015);
    hardware_push_acc();
    registers_[007] = registers_[014];
    registers_[016] = 2;
    registers_[015] = 06435;
    return 05430;
}

std::uint16_t Machine::p06435()
{
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    shift_accumulator(-15);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[registers_[007]].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    memory_[registers_[007]] = accumulator_;

    registers_[010] = 06444;
    accumulator_ = memory_[address_add(registers_[010], 0131)];
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    accumulator_ = memory_[02213];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 1)] = accumulator_;

    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(015);
    sti(007);
    return registers_[015];
}

std::uint16_t Machine::p06526()
{
    its(007);
    its(015);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[007] = 01200;
    return p06530();
}

std::uint16_t Machine::p06530()
{
    const std::uint16_t descriptor =
        memory_[address_add(registers_[007], 0717)].address();
    accumulator_ = memory_[address_add(descriptor, 1)];
    select_alu_group(rau_logical);
    const Word48 left = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[007], 0203)].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    shift_accumulator(42);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    accumulator_ = memory_[address_add(registers_[007], 0717)];
    select_alu_group(rau_logical);
    registers_[015] = 06534;
    return 017045;
}

std::uint16_t Machine::p06534()
{
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        accumulator_ = memory_[address_add(registers_[007], 0643)];
        select_alu_group(rau_logical);
        registers_[015] = 06556;
        return 017045;
    }
    accumulator_ = memory_[address_add(registers_[007], 0717)];
    select_alu_group(rau_logical);
    registers_[015] = 06536;
    return 017013;
}

std::uint16_t Machine::p06536()
{
    memory_[address_add(registers_[017], -4)] = accumulator_;
    registers_[015] =
        memory_[address_add(registers_[007], 0717)].address();
    const std::uint16_t modifier =
        memory_[address_add(registers_[015], 1)].address();
    registers_[016] = modifier;

    accumulator_ = memory_[address_add(registers_[016], 1)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[015], 1)] = accumulator_;
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    memory_[registers_[015]] = accumulator_;
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ = memory_[registers_[017]];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        registers_[013] = 05504;
        registers_[015] = 06545;
        return 03516;
    }

    registers_[016] = address_add(registers_[016], 075734);
    if (registers_[016] == 0) {
        accumulator_ = memory_[address_add(registers_[007], 0437)];
        select_alu_group(rau_logical);
        memory_[registers_[015]] = accumulator_;
        accumulator_ = memory_[address_add(registers_[007], 0203)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[015], 1)] = accumulator_;
    }
    return p06545();
}

std::uint16_t Machine::p06545()
{
    registers_[010] = 06444;
    accumulator_ = memory_[06505];
    select_alu_group(rau_logical);
    registers_[013] = accumulator_.address();
    accumulator_ = memory_[06503];
    select_alu_group(rau_logical);
    registers_[014] = accumulator_.address();
    memory_[address_add(registers_[013], 1)] = accumulator_;
    memory_[06505] = accumulator_;
    accumulator_ = memory_[address_add(registers_[014], 1)];
    select_alu_group(rau_logical);
    memory_[06503] = accumulator_;
    accumulator_ = memory_[06504];
    select_alu_group(rau_logical);
    stx(address_add(registers_[014], 1));
    sti(015);
    sti(007);
    memory_[registers_[014]] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p06556()
{
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return 020715;
    }
    accumulator_ = memory_[address_add(registers_[007], 0643)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[017], -1)] = accumulator_;
    registers_[015] = 06560;
    return 017013;
}

std::uint16_t Machine::p06560()
{
    stx(address_add(registers_[007], 0717));
    registers_[015] = 06561;
    return 017021;
}

std::uint16_t Machine::p06561()
{
    memory_[address_add(registers_[007], 0643)] = accumulator_;
    return p06530();
}

std::uint16_t Machine::p06712()
{
    // 06712..06726 normalizes the two arithmetic operands kept immediately
    // below r17.  r11 selects the caller-specific arithmetic continuation.
    alu_mode_ = 006;
    registers_[013] = 077776;
    registers_[010] = 06712;

    accumulator_ = memory_[address_add(registers_[017], -1)];
    select_alu_group(rau_logical);
    shift_accumulator(1);
    accumulator_ = cyclic_add(accumulator_, memory_[06755]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        accumulator_ = memory_[address_add(registers_[017], -1)];
        select_alu_group(rau_logical);
        return registers_[011];
    }
    accumulator_ = cyclic_add(accumulator_, memory_[06756]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        registers_[013] = address_add(registers_[013], 1);
        accumulator_ = memory_[address_add(registers_[017], -1)];
        select_alu_group(rau_logical);
        arithmetic_add(memory_[0], false, false);
        memory_[address_add(registers_[017], -1)] = accumulator_;
    }

    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    shift_accumulator(1);
    accumulator_ = cyclic_add(accumulator_, memory_[06755]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        accumulator_ = memory_[address_add(registers_[017], -2)];
        select_alu_group(rau_logical);
        return registers_[011];
    }
    accumulator_ = cyclic_add(accumulator_, memory_[06756]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) == 0) {
        accumulator_ = memory_[address_add(registers_[017], -2)];
        select_alu_group(rau_logical);
        return address_add(registers_[011], 1);
    }

    registers_[013] = address_add(registers_[013], 1);
    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[0], false, false);
    return address_add(registers_[011], 1);
}

std::uint16_t Machine::p06733()
{
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[011] = 06734;
    return 06712;
}

std::uint16_t Machine::p06740()
{
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[011] = 06741;
    return 06712;
}

std::uint16_t Machine::p06744()
{
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[011] = 06745;
    return 06712;
}

std::uint16_t Machine::p06750()
{
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[011] = 06751;
    return 06712;
}

std::uint16_t Machine::p06623_generated_compare(
    std::uint16_t link, std::uint16_t selector,
    bool reverse_subtraction, bool exchange_results)
{
    registers_[016] = selector;
    registers_[014] = link;

    const std::uint16_t continuation = p06650();
    if (continuation != link) {
        return continuation;
    }

    // XTA ,17 followed by A-X ,17 or X-A ,17: both operands use the hardware
    // stack convention and therefore decrement r17 before reading.
    hardware_pop_acc();
    select_alu_group(rau_logical);
    registers_[017] = address_add(registers_[017], -1);
    if (reverse_subtraction) {
        this->reverse_subtract(memory_[registers_[017]]);
    } else {
        arithmetic_add(memory_[registers_[017]], false, true);
    }

    // UZA copies ACC into RMR and branches when the additive sign condition
    // is false, i.e. when bit 41 is clear.  The sibling templates exchange
    // only the two diagnostic destinations.
    remainder_ = accumulator_;
    const bool nonnegative = (accumulator_.raw() & bit41) == 0;
    const bool select_06657 = nonnegative == exchange_results;
    return address_add(registers_[013], select_06657 ? 057 : 060);
}

std::uint16_t Machine::p06623()
{
    return p06623_generated_compare(06624, 014040, false, false);
}

std::uint16_t Machine::p06631()
{
    return p06623_generated_compare(06632, 014030, true, false);
}

std::uint16_t Machine::p06637()
{
    return p06623_generated_compare(06640, 014050, false, true);
}

std::uint16_t Machine::p06645()
{
    return p06623_generated_compare(06646, 014060, true, true);
}

std::uint16_t Machine::p06650()
{
    // 06650 is linked through r14 by the generated templates at
    // 06623/06631/06637/06645.  Preserve both comparisons, their RMR flag
    // values, and the two original diagnostic continuations.
    registers_[013] = 06600;
    hardware_push_acc();
    shift_accumulator(1);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[013], 071)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        hardware_pop_acc();
        select_alu_group(rau_logical);
        registers_[016] = address_add(registers_[016], 1);
        registers_[015] = 06654;
        return 03014;
    }

    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    shift_accumulator(1);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[013], 071)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return registers_[014];
    }
    registers_[015] = 06657;
    return 03014;
}

std::uint16_t Machine::p06657()
{
    accumulator_ = memory_[address_add(registers_[013], 070)];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p06660()
{
    accumulator_ = memory_[address_add(registers_[013], 067)];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p06661()
{
    registers_[015] = 03235;
    return 03275;
}

std::uint16_t Machine::p06734_error()
{
    registers_[016] = 010000;
    return 03014;
}

std::uint16_t Machine::p06741_error()
{
    registers_[016] = 010001;
    return 03014;
}

std::uint16_t Machine::p06745_error()
{
    registers_[016] = 010002;
    return 03014;
}

std::uint16_t Machine::p06751_error()
{
    registers_[016] = 010003;
    return 03014;
}

std::uint16_t Machine::p06727_finish_arithmetic()
{
    memory_[address_add(registers_[017], -1)] = accumulator_;
    shift_accumulator(1);
    accumulator_ = cyclic_add(accumulator_, memory_[06755]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        registers_[016] = 013017;
        return 03014;
    }
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ = memory_[registers_[017]];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p06735_add()
{
    registers_[017] = address_add(registers_[017], -1);
    arithmetic_add(memory_[registers_[017]], false, false);
    if (registers_[013] == 0) {
        alu_mode_ = 003;
        arithmetic_add(memory_[01637], false, false);
    }
    return p06727_finish_arithmetic();
}

std::uint16_t Machine::p06742_subtract()
{
    registers_[017] = address_add(registers_[017], -1);
    arithmetic_add(memory_[registers_[017]], false, true);
    if (registers_[013] == 0) {
        alu_mode_ = 003;
        arithmetic_add(memory_[01637], false, false);
    }
    return p06727_finish_arithmetic();
}

std::uint16_t Machine::p06746_multiply()
{
    registers_[017] = address_add(registers_[017], -1);
    multiply(memory_[registers_[017]]);
    if (registers_[013] == 0) {
        alu_mode_ = 003;
        arithmetic_add(memory_[01637], false, false);
    }
    return p06727_finish_arithmetic();
}

std::uint16_t Machine::p06752_divide()
{
    registers_[017] = address_add(registers_[017], -1);
    divide(memory_[registers_[017]]);
    return p06727_finish_arithmetic();
}

std::uint16_t Machine::p07673()
{
    // 07673..07745 preserves the caller and compiler registers in a
    // five-word frame.  Calls to evaluator/stack helpers remain independent
    // semantic boundaries; local continuations retain the original links.
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    alu_mode_ = 006;
    its(001);
    its(002);
    its(005);
    registers_[005] = 07514;
    xts(address_add(registers_[005], 0243));
    hardware_push_acc();

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[005], 0243)] = accumulator_;
    shift_accumulator(1);
    accumulator_ =
        cyclic_add(accumulator_, memory_[address_add(registers_[005], 0325)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        return p07703();
    }

    accumulator_ = memory_[address_add(registers_[005], 0243)];
    select_alu_group(rau_logical);
    registers_[015] = 07742;
    return 012674;
}

std::uint16_t Machine::p07703()
{
    accumulator_ = memory_[address_add(registers_[005], 0243)];
    select_alu_group(rau_logical);
    registers_[015] = 07704;
    return 03330;
}

std::uint16_t Machine::p07704()
{
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p07746();
    }
    accumulator_ = memory_[address_add(registers_[005], 0326)];
    select_alu_group(rau_logical);
    registers_[015] = 07706;
    return 03275;
}

std::uint16_t Machine::p07706()
{
    registers_[016] = 1;
    registers_[015] = 07707;
    return 07652;
}

std::uint16_t Machine::p07707()
{
    accumulator_ = memory_[address_add(registers_[005], 0243)];
    select_alu_group(rau_logical);
    registers_[015] = 07710;
    return 010353;
}

std::uint16_t Machine::p07710()
{
    shift_accumulator(-7);
    remainder_ = accumulator_;
    return accumulator_.raw() != 0 ? p07714() : p07711();
}

std::uint16_t Machine::p07711()
{
    accumulator_ = memory_[address_add(registers_[005], 0243)];
    select_alu_group(rau_logical);
    registers_[015] = 07712;
    return 03330;
}

std::uint16_t Machine::p07712()
{
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p07726();
    }
    registers_[016] = 07757;
    registers_[015] = 07742;
    return 07673;
}

std::uint16_t Machine::p07714()
{
    accumulator_ = memory_[address_add(registers_[005], 0243)];
    select_alu_group(rau_logical);
    registers_[015] = 07715;
    return 017021;
}

std::uint16_t Machine::p07715()
{
    xts(address_add(registers_[005], 0243));
    registers_[015] = 07716;
    return 017013;
}

std::uint16_t Machine::p07716()
{
    memory_[address_add(registers_[005], 0243)] = accumulator_;
    registers_[016] = 07757;
    registers_[015] = 07720;
    return 07673;
}

std::uint16_t Machine::p07720()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[005], 0243)] = accumulator_;
    registers_[015] = 07722;
    return 010353;
}

std::uint16_t Machine::p07722()
{
    shift_accumulator(-7);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p07711();
    }
    accumulator_ = memory_[address_add(registers_[005], 0322)];
    select_alu_group(rau_logical);
    registers_[015] = 07724;
    return 03275;
}

std::uint16_t Machine::p07724()
{
    registers_[016] = 1;
    registers_[015] = 07725;
    return 07652;
}

std::uint16_t Machine::p07725()
{
    return p07714();
}

std::uint16_t Machine::p07726()
{
    accumulator_ = memory_[address_add(registers_[005], 0327)];
    select_alu_group(rau_logical);
    registers_[015] = 07727;
    return 03275;
}

std::uint16_t Machine::p07727()
{
    registers_[016] = 1;
    return p07741();
}

std::uint16_t Machine::p07746()
{
    accumulator_ = memory_[address_add(registers_[005], 0243)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[005], 0317)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 selected = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[005], 0332)].raw());
    remainder_ = selected;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p07730();
    }
    registers_[016] = 07753;
    registers_[015] = 07751;
    return 07673;
}

std::uint16_t Machine::p07751()
{
    registers_[016] = 07754;
    registers_[015] = 07752;
    return 07673;
}

std::uint16_t Machine::p07752()
{
    return p07742();
}

std::uint16_t Machine::p07730()
{
    accumulator_ = memory_[address_add(registers_[005], 0243)];
    select_alu_group(rau_logical);
    registers_[016] = accumulator_.address();
    accumulator_ = accumulator_
        & memory_[address_add(registers_[005], 0317)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 selected = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[005], 0330)].raw());
    remainder_ = selected;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return address_add(registers_[005], 0245);
    }

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    registers_[001] = 0;
    registers_[002] = 077773;
    return p07734();
}

std::uint16_t Machine::p07734()
{
    memory_[address_add(registers_[005], 0244)] = accumulator_;
    accumulator_ = accumulator_
        & memory_[address_add(registers_[005], 0331)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        registers_[001] = address_add(registers_[001], 1);
        const Word48 selected = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[005], 0320)].raw());
        remainder_ = selected;
        select_alu_group(rau_logical);
        registers_[015] = 07737;
        return 03275;
    }
    return p07737();
}

std::uint16_t Machine::p07737()
{
    accumulator_ = memory_[address_add(registers_[005], 0244)];
    select_alu_group(rau_logical);
    shift_accumulator(8);
    if (registers_[002] != 0) {
        registers_[002] = address_add(registers_[002], 1);
        return p07734();
    }
    registers_[016] = registers_[001];
    return p07741();
}

std::uint16_t Machine::p07741()
{
    registers_[015] = 07742;
    return 07652;
}

std::uint16_t Machine::p07742()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    stx(address_add(registers_[005], 0243));
    sti(005);
    sti(002);
    sti(001);
    registers_[015] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p11464()
{
    // 11464..11470 preserves the incoming word, r1, and caller in a
    // three-word frame.  A nonzero address allocates through the independent
    // 05430 boundary; zero addresses skip directly to local restoration.
    its(001);
    its(015);
    hardware_push_acc();

    registers_[001] = 011464;
    registers_[016] = memory_[address_add(registers_[017], -3)].address();
    if (registers_[016] == 0) {
        return p11474();
    }

    registers_[016] = address_add(registers_[016], 1);
    registers_[015] = 011471;
    return 05430;
}

std::uint16_t Machine::p11471()
{
    accumulator_ = memory_[address_add(registers_[017], -3)];
    select_alu_group(rau_logical);
    shift_accumulator(-24);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[017], -3)].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 0310)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[registers_[016]] = accumulator_;
    return p11474();
}

std::uint16_t Machine::p11474()
{
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    const Word48 allocated_address = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 036)].raw());
    remainder_ = allocated_address;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[017], -3)] = accumulator_;

    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(015);
    sti(001);
    return registers_[015];
}

std::uint16_t Machine::p11500()
{
    // 11500..11502 indexes the address in the accumulator by frame word -1,
    // consumes that frame word, and returns the selected value.
    registers_[016] = accumulator_.address();
    const std::uint16_t modifier =
        memory_[address_add(registers_[017], -1)].address();
    accumulator_ = memory_[address_add(registers_[016], modifier)];
    select_alu_group(rau_logical);
    registers_[017] = address_add(registers_[017], -1);
    return registers_[015];
}

std::uint16_t Machine::p11524()
{
    registers_[015] = 011525;
    return 03277;
}

std::uint16_t Machine::p11525()
{
    hardware_push_acc();
    registers_[015] = 011526;
    return 03277;
}

std::uint16_t Machine::p11526()
{
    hardware_push_acc();
    registers_[015] = 011527;
    return 011536;
}

std::uint16_t Machine::p11527()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    registers_[015] = 011530;
    return 011500;
}

std::uint16_t Machine::p11530()
{
    return p03314();
}

std::uint16_t Machine::p13007()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();
    return p13010();
}

std::uint16_t Machine::p13010()
{
    alu_mode_ = 003;
    accumulator_ = memory_[address_add(registers_[001], 0460)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[address_add(registers_[001], 0454)],
                   false, true);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) == 0) {
        return p13015();
    }
    registers_[015] = 013013;
    return 013047;
}

std::uint16_t Machine::p13013()
{
    registers_[015] = 013014;
    return 013216;
}

std::uint16_t Machine::p13014()
{
    memory_[address_add(registers_[001], 0462)] = accumulator_;
    return p13010();
}

std::uint16_t Machine::p13015()
{
    registers_[015] = 013016;
    return 013017;
}

std::uint16_t Machine::p13016()
{
    hardware_pop_acc();
    return accumulator_.address();
}

std::uint16_t Machine::p13017()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();

    accumulator_ = memory_[address_add(registers_[001], 0462)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 0427)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) == 0) {
        accumulator_ = memory_[address_add(registers_[001], 0410)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[001], 0501)] = accumulator_;
        accumulator_ = memory_[0];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[001], 0471)] = accumulator_;
    } else {
        accumulator_ = memory_[0];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[001], 0501)] = accumulator_;
        accumulator_ = memory_[address_add(registers_[001], 0430)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[001], 0471)] = accumulator_;
    }

    accumulator_ = memory_[address_add(registers_[001], 0470)];
    select_alu_group(rau_logical);
    alu_mode_ = 023;
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) == 0) {
        accumulator_ = cyclic_add(
            accumulator_, memory_[address_add(registers_[001], 0501)]);
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        registers_[015] = 013031;
        return 013072;
    }

    accumulator_ = memory_[address_add(registers_[001], 0501)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0461)] = accumulator_;
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p13043();
    }

    alu_mode_ = 003;
    accumulator_ = memory_[address_add(registers_[001], 0466)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[address_add(registers_[001], 0467)],
                   false, true);
    memory_[address_add(registers_[001], 0466)] = accumulator_;
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) != 0) {
        return p13046();
    }

    accumulator_ = memory_[address_add(registers_[001], 0455)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 0410)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[address_add(registers_[001], 0455)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0410)];
    select_alu_group(rau_logical);
    registers_[015] = 013041;
    return 013072;
}

std::uint16_t Machine::p13031()
{
    return p13043();
}

std::uint16_t Machine::p13041()
{
    return p13043();
}

std::uint16_t Machine::p13043()
{
    registers_[015] = 013044;
    return 013063;
}

std::uint16_t Machine::p13044()
{
    accumulator_ = memory_[address_add(registers_[001], 0472)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p13046();
    }
    registers_[015] = 013046;
    return 013111;
}

std::uint16_t Machine::p13046()
{
    hardware_pop_acc();
    return accumulator_.address();
}

std::uint16_t Machine::p13047()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();

    accumulator_ = memory_[address_add(registers_[001], 0460)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 0410)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[address_add(registers_[001], 0460)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 0462)];
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0430)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[address_add(registers_[001], 0466)];
        select_alu_group(rau_logical);
        accumulator_ = cyclic_add(
            accumulator_, memory_[address_add(registers_[001], 0410)]);
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        memory_[address_add(registers_[001], 0466)] = accumulator_;
        return p13062();
    }

    accumulator_ = memory_[address_add(registers_[001], 0470)];
    select_alu_group(rau_logical);
    alu_mode_ = 023;
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) == 0) {
        registers_[015] = 013057;
        return 013072;
    }
    return p13057();
}

std::uint16_t Machine::p13057()
{
    accumulator_ = memory_[address_add(registers_[001], 0462)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0470)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0430)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0471)] = accumulator_;
    registers_[015] = 013062;
    return 013063;
}

std::uint16_t Machine::p13062()
{
    hardware_pop_acc();
    return accumulator_.address();
}

std::uint16_t Machine::p13063()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);

    accumulator_ = memory_[address_add(registers_[001], 0466)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        const std::uint16_t continuation =
            address_add(registers_[001], 0234);
        return continuation == 013071 ? p13071() : continuation;
    }

    accumulator_ = memory_[address_add(registers_[001], 0471)];
    select_alu_group(rau_logical);
    registers_[015] = 013066;
    return 013072;
}

std::uint16_t Machine::p13066()
{
    alu_mode_ = 003;
    accumulator_ = memory_[address_add(registers_[001], 0466)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[address_add(registers_[001], 0410)],
                   false, true);
    memory_[address_add(registers_[001], 0466)] = accumulator_;
    return address_add(registers_[001], 0227);
}

std::uint16_t Machine::p13071()
{
    registers_[017] = address_add(registers_[017], -1);
    return memory_[registers_[017]].address();
}

std::uint16_t Machine::p13072()
{
    memory_[address_add(registers_[001], 0477)] = accumulator_;
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);

    accumulator_ = memory_[address_add(registers_[001], 0457)];
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0455)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p13076();
    }

    registers_[015] = 013076;
    return 013111;
}

std::uint16_t Machine::p13076()
{
    accumulator_ = memory_[address_add(registers_[001], 0457)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 0410)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[address_add(registers_[001], 0457)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 0465)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return address_add(registers_[001], 0246);
    }

    accumulator_ = memory_[address_add(registers_[001], 0477)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return address_add(registers_[001], 0246);
    }

    accumulator_ = memory_[address_add(registers_[001], 0431)];
    select_alu_group(rau_logical);
    return address_add(registers_[001], 0250);
}

std::uint16_t Machine::p13111()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();
    registers_[015] = 013113;
    return 013121;
}

std::uint16_t Machine::p13113()
{
    accumulator_ = memory_[address_add(registers_[001], 0504)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p13117();
    }

    accumulator_ = memory_[address_add(registers_[001], 0432)];
    select_alu_group(rau_logical);
    registers_[015] = 013115;
    return 03275;
}

std::uint16_t Machine::p13115()
{
    accumulator_ = memory_[01567];
    select_alu_group(rau_logical);
    registers_[015] = 013117;
    return 02750;
}

std::uint16_t Machine::p13117()
{
    accumulator_ = memory_[address_add(registers_[001], 0410)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0472)] = accumulator_;
    hardware_pop_acc();
    return accumulator_.address();
}

std::uint16_t Machine::p13121()
{
    // VTA r15 replaces the incoming accumulator; ATX (r17) then pushes the
    // caller consumed by the indirect WTC/UJ return at 13127.
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);

    accumulator_ = memory_[address_add(registers_[001], 0465)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        const std::uint16_t continuation =
            address_add(registers_[001], 0272);
        return continuation == 013127 ? p13127() : continuation;
    }

    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0465)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0463)];
    select_alu_group(rau_logical);
    registers_[015] = 013125;
    return 03275;
}

std::uint16_t Machine::p13125()
{
    accumulator_ = memory_[01567];
    select_alu_group(rau_logical);
    registers_[015] = 013127;
    return 02750;
}

std::uint16_t Machine::p13127()
{
    registers_[017] = address_add(registers_[017], -1);
    return memory_[registers_[017]].address();
}

std::uint16_t Machine::p13130()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    hardware_push_acc();

    accumulator_ = memory_[address_add(registers_[001], 0431)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0463)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 0451)];
    select_alu_group(rau_logical);
    alu_mode_ = 002;
    arithmetic_add(memory_[0], false, false);
    memory_[address_add(registers_[001], 0451)] = accumulator_;
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) != 0) {
        reverse_subtract(memory_[0]);
        memory_[address_add(registers_[001], 0451)] = accumulator_;
        accumulator_ = memory_[address_add(registers_[001], 0424)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[001], 0463)] = accumulator_;
    }

    accumulator_ = memory_[address_add(registers_[001], 0451)];
    select_alu_group(rau_logical);
    alu_mode_ = 003;
    arithmetic_add(memory_[address_add(registers_[001], 0420)],
                   false, false);
    memory_[address_add(registers_[001], 0464)] = accumulator_;
    alu_mode_ = 002;
    arithmetic_add(memory_[0], false, false);
    reverse_subtract(memory_[address_add(registers_[001], 0451)]);
    memory_[address_add(registers_[001], 0451)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0464)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0433)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0464)] = accumulator_;

    registers_[016] = 077765;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    for (;;) {
        const std::uint16_t address = address_add(
            address_add(registers_[001], 0507),
            address_add(registers_[016], 013));
        memory_[address] = accumulator_;
        if (registers_[016] == 0) {
            break;
        }
        registers_[016] = address_add(registers_[016], 1);
    }

    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0456)] = accumulator_;
    alu_mode_ = 003;
    for (;;) {
        accumulator_ = memory_[address_add(registers_[001], 0464)];
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            break;
        }

        accumulator_ = memory_[address_add(registers_[001], 0456)];
        select_alu_group(rau_logical);
        accumulator_ = cyclic_add(
            accumulator_, memory_[address_add(registers_[001], 0410)]);
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        memory_[address_add(registers_[001], 0456)] = accumulator_;

        accumulator_ = memory_[address_add(registers_[001], 0464)];
        select_alu_group(rau_logical);
        divide(memory_[address_add(registers_[001], 0445)]);
        arithmetic_add(memory_[address_add(registers_[001], 0434)],
                       false, false);
        accumulator_ = accumulator_
            & memory_[address_add(registers_[001], 0433)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        hardware_push_acc();
        multiply(memory_[address_add(registers_[001], 0444)]);
        yta(0);
        reverse_subtract(
            memory_[address_add(registers_[001], 0464)]);
        const std::uint16_t output = address_add(
            address_add(registers_[001], 0506),
            memory_[address_add(registers_[001], 0456)].address());
        stx(output);
        memory_[address_add(registers_[001], 0464)] = accumulator_;
    }

    accumulator_ = memory_[address_add(registers_[001], 0435)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0500)] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    for (const std::uint16_t offset : {
             0457, 0460, 0466, 0461, 0472}) {
        memory_[address_add(registers_[001], offset)] = accumulator_;
    }
    accumulator_ = memory_[address_add(registers_[001], 0410)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0467)] = accumulator_;
    memory_[address_add(registers_[001], 0465)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0433)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0470)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0504)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p13172();
    }
    registers_[015] = 013172;
    return 013121;
}

std::uint16_t Machine::p13172()
{
    hardware_pop_acc();
    return accumulator_.address();
}

std::uint16_t Machine::p13216()
{
    // WTC r1+0500; UJ 0. WTC only supplies the modifier for the following
    // transfer and otherwise leaves architectural state untouched.
    return memory_[address_add(registers_[001], 0500)].address();
}

std::uint16_t Machine::p13217()
{
    accumulator_ = memory_[address_add(registers_[001], 0450)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[address_add(registers_[001], 0410)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[001], 0450)] = accumulator_;
        return registers_[015];
    }

    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 0410)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[address_add(registers_[001], 0450)] = accumulator_;
    registers_[016] = 077727;
    accumulator_ = memory_[address_add(registers_[001], 0451)];
    select_alu_group(rau_logical);
    for (;;) {
        const std::uint16_t address = address_add(
            address_add(registers_[001], 0451),
            address_add(registers_[016], 052));
        xts(address);
        if (registers_[016] == 0) {
            break;
        }
        registers_[016] = address_add(registers_[016], 1);
    }
    hardware_push_acc();
    return registers_[015];
}

std::uint16_t Machine::p16005()
{
    registers_[010] = 016005;
    shift_accumulator(-15);
    memory_[address_add(registers_[010], 0244)] = accumulator_;
    registers_[016] = 021123;
    for (;;) {
        accumulator_ = memory_[address_add(registers_[016], 1)];
        select_alu_group(rau_logical);
        registers_[016] = accumulator_.address();
        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        accumulator_ = accumulator_
            & memory_[address_add(registers_[010], 0216)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            accumulator_ = Word48(registers_[015]);
            select_alu_group(rau_logical);
            hardware_push_acc();
            registers_[016] = 2;
            registers_[015] = 016022;
            return 05430;
        }

        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        accumulator_ = accumulator_
            & memory_[address_add(registers_[010], 0217)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        const Word48 selected = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[010], 0244)].raw());
        remainder_ = selected;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            continue;
        }

        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        accumulator_ = accumulator_
            & memory_[address_add(registers_[010], 0220)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        const Word48 masked = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[010], 0220)].raw());
        remainder_ = masked;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            accumulator_ = Word48(registers_[016]);
            select_alu_group(rau_logical);
            return registers_[015];
        }

        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        accumulator_ = cyclic_add(
            accumulator_, memory_[address_add(registers_[010], 0221)]);
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        memory_[registers_[016]] = accumulator_;
        accumulator_ = Word48(registers_[016]);
        select_alu_group(rau_logical);
        return registers_[015];
    }
}

std::uint16_t Machine::p16022()
{
    registers_[010] = 016005;
    accumulator_ = memory_[address_add(registers_[010], 0244)];
    select_alu_group(rau_logical);
    const Word48 saved = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0221)].raw());
    remainder_ = saved;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    hardware_push_acc();
    registers_[015] = 016026;
    return 021107;
}

std::uint16_t Machine::p16026()
{
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ = memory_[registers_[017]];
    select_alu_group(rau_logical);
    registers_[017] = address_add(registers_[017], -1);
    return memory_[registers_[017]].address();
}

std::uint16_t Machine::p16151()
{
    registers_[010] = 016005;
    memory_[016252] = accumulator_;
    accumulator_ = memory_[016237];
    select_alu_group(rau_logical);
    registers_[016] = accumulator_.address();
    return p16153();
}

std::uint16_t Machine::p16153()
{
    for (;;) {
        if (registers_[016] == 0) {
            return p16171();
        }
        registers_[016] = address_add(registers_[016], -1);
        accumulator_ = memory_[016252];
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return p16157();
        }

        const Word48 compared = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(016240, registers_[016])].raw());
        remainder_ = compared;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return p16157();
        }
    }
}

std::uint16_t Machine::p16157()
{
    accumulator_ = memory_[address_add(016240, registers_[016])];
    select_alu_group(rau_logical);
    hardware_push_acc();
    return p16161();
}

std::uint16_t Machine::p16161()
{
    for (;;) {
        registers_[016] = address_add(registers_[016], 1);
        accumulator_ = Word48(registers_[016]);
        select_alu_group(rau_logical);
        const Word48 compared = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[016237].raw());
        remainder_ = compared;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return p16166();
        }

        accumulator_ = memory_[address_add(016240, registers_[016])];
        select_alu_group(rau_logical);
        memory_[address_add(016237, registers_[016])] = accumulator_;
    }
}

std::uint16_t Machine::p16166()
{
    accumulator_ = memory_[016237];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(accumulator_, memory_[016233]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    accumulator_ = accumulator_ & memory_[016233];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    stx(016237);
    return registers_[015];
}

std::uint16_t Machine::p16171()
{
    registers_[016] = 021123;
    for (;;) {
        accumulator_ = memory_[address_add(registers_[016], 1)];
        select_alu_group(rau_logical);
        shift_accumulator(24);
        registers_[016] = accumulator_.address();
        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        accumulator_ = accumulator_ & memory_[016223];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            return p16222();
        }

        accumulator_ = memory_[registers_[016]];
        select_alu_group(rau_logical);
        accumulator_ = accumulator_ & memory_[016233];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            continue;
        }

        const Word48 masked = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[016252].raw());
        remainder_ = masked;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return p16201();
        }

        accumulator_ = memory_[016252];
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            continue;
        }
        return p16201();
    }
}

std::uint16_t Machine::p16201()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(016);
    xts(registers_[016]);
    accumulator_ = accumulator_ & memory_[016225];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p16215();
    }

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[016227];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p16210();
    }

    const Word48 selected = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[016]].raw());
    remainder_ = selected;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    registers_[015] = 016207;
    return 020575;
}

std::uint16_t Machine::p16207()
{
    registers_[010] = 016005;
    return p16210();
}

std::uint16_t Machine::p16210()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(016);
    registers_[015] = accumulator_.address();
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[016233];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    hardware_push_acc();
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[016]].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    hardware_pop_acc();
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p16215()
{
    registers_[015] = 016216;
    return 021075;
}

std::uint16_t Machine::p16216()
{
    registers_[010] = 016005;
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(016);
    registers_[015] = accumulator_.address();
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[016233];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p16222()
{
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p16254()
{
    // 16254..16303 is a leaf table-search routine.  Keep its arithmetic and
    // RMR path literal: the multiply/YTA pair derives a table offset, while
    // the two stack-pop arms narrow the pair of search values.
    registers_[010] = 016254;
    memory_[016312] = accumulator_;

    alu_mode_ = 003;
    accumulator_ = memory_[016304];
    select_alu_group(rau_logical);
    memory_[016310] = accumulator_;
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    memory_[016311] = accumulator_;

    for (;;) {
        accumulator_ = memory_[016310];
        select_alu_group(rau_logical);
        arithmetic_add(memory_[016305], false, false);
        const Word48 left_bound = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[016311].raw());
        remainder_ = left_bound;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            break;
        }

        accumulator_ = memory_[016310];
        select_alu_group(rau_logical);
        arithmetic_add(memory_[016311], false, false);
        shift_accumulator(1);
        hardware_push_acc();
        const Word48 midpoint = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[016306].raw());
        remainder_ = midpoint;
        select_alu_group(rau_logical);
        multiply(memory_[address_add(registers_[016], 1)]);
        yta(0);
        registers_[014] = accumulator_.address();
        registers_[014] = address_add(
            registers_[014], address_add(registers_[016], 3));

        accumulator_ = memory_[registers_[014]];
        select_alu_group(rau_logical);
        accumulator_ = accumulator_
            & memory_[address_add(registers_[016], 2)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        const Word48 selected = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[016307].raw());
        remainder_ = selected;
        select_alu_group(rau_logical);
        accumulator_ = cyclic_add(accumulator_, memory_[016312]);
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        remainder_ = accumulator_;

        hardware_pop_acc();
        select_alu_group(rau_logical);
        if ((remainder_.raw() & bit48) == 0) {
            memory_[016310] = accumulator_;
        } else {
            memory_[016311] = accumulator_;
            registers_[013] = registers_[014];
        }
    }

    accumulator_ = memory_[016311];
    select_alu_group(rau_logical);
    const Word48 upper_bound = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[016]].raw());
    remainder_ = upper_bound;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        registers_[013] = registers_[014];
        registers_[013] = address_add(
            registers_[013],
            memory_[address_add(registers_[016], 1)].address());
        accumulator_ = memory_[016305];
        select_alu_group(rau_logical);
    } else {
        accumulator_ = memory_[registers_[013]];
        select_alu_group(rau_logical);
        accumulator_ = accumulator_
            & memory_[address_add(registers_[016], 2)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        const Word48 selected = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[016312].raw());
        remainder_ = selected;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            accumulator_ = memory_[016305];
            select_alu_group(rau_logical);
        }
    }

    registers_[016] = registers_[013];
    return registers_[015];
}

std::uint16_t Machine::p16341()
{
    // 16341..16346 preserves the compiler registers and enters the shared
    // record shifter.  The dynamic dispatch after 16350 remains expressed in
    // original BESM addresses.
    its(001);
    its(004);
    its(005);
    its(002);
    its(007);
    its(003);
    registers_[003] = registers_[015];
    registers_[001] = 022261;
    xts(0);
    memory_[address_add(registers_[017], -7)] = accumulator_;
    registers_[015] = 016347;
    return 016457;
}

std::uint16_t Machine::p16477()
{
    // 16477 returns immediately when record word +1 is already populated.
    // The empty case preserves the caller and record descriptor around the
    // ordinary evaluator before filling that word from the POP stack.
    accumulator_ = memory_[address_add(registers_[003], 1)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return registers_[015];
    }

    its(015);
    xts(address_add(registers_[003], -2));
    registers_[015] = 016502;
    return 02750;
}

std::uint16_t Machine::p16502()
{
    registers_[015] = 016503;
    return 03277;
}

std::uint16_t Machine::p16503()
{
    stx(address_add(registers_[003], 1));
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p16742()
{
    // 16742..16743 preserves the caller in r7, then uses the existing
    // tagged-byte lookup as an independent semantic boundary.
    accumulator_ = memory_[address_add(registers_[003], 1)];
    select_alu_group(rau_logical);
    registers_[007] = registers_[015];
    registers_[015] = 016744;
    return 016421;
}

std::uint16_t Machine::p16744()
{
    const Word48 lookup_code = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074511)].raw());
    remainder_ = lookup_code;
    select_alu_group(rau_logical);
    return registers_[007];
}

std::uint16_t Machine::p16347()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    registers_[015] = 016350;
    return 016421;
}

std::uint16_t Machine::p16350()
{
    registers_[015] = accumulator_.address();
    return address_add(016351, registers_[015]);
}

std::uint16_t Machine::p16376()
{
    // 16376..16401 restores the six modifier registers saved by 16341 and
    // leaves the bottom frame word in the accumulator for environment bind.
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(003);
    sti(007);
    sti(002);
    sti(005);
    sti(004);
    sti(001);
    return 03235;
}

std::uint16_t Machine::p16417()
{
    accumulator_ = memory_[address_add(registers_[017], -7)];
    select_alu_group(rau_logical);
    registers_[015] = 016420;
    return 01107;
}

std::uint16_t Machine::p16420()
{
    registers_[015] = 016376;
    return 03275;
}

std::uint16_t Machine::p17013()
{
    its(015);
    xts(address_add(registers_[017], -1));
    registers_[015] = 017015;
    return 021464;
}

std::uint16_t Machine::p17015()
{
    registers_[010] = 017013;
    stx(address_add(registers_[017], -2));
    sti(015);
    const Word48 left = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017067].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        registers_[016] = 012021;
        return 03014;
    }
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017067].raw());
    select_alu_group(rau_logical);
    return 05207;
}

std::uint16_t Machine::p17021()
{
    its(015);
    xts(address_add(registers_[017], -1));
    registers_[015] = 017023;
    return 021464;
}

std::uint16_t Machine::p17023()
{
    registers_[010] = 017013;
    stx(address_add(registers_[017], -2));
    sti(015);
    const Word48 left = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017067].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        registers_[016] = 012021;
        return 03014;
    }
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017067].raw());
    select_alu_group(rau_logical);
    return 05211;
}

std::uint16_t Machine::p17045()
{
    its(015);
    xts(address_add(registers_[017], -1));
    registers_[015] = 017047;
    return 021464;
}

std::uint16_t Machine::p17047()
{
    registers_[010] = 017013;
    stx(address_add(registers_[017], -2));
    sti(015);
    const Word48 left = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017067].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[017064];
        select_alu_group(rau_logical);
        return registers_[015];
    }
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017067].raw());
    select_alu_group(rau_logical);
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p17070()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[015] = 017072;
    return 06343;
}

std::uint16_t Machine::p17072()
{
    memory_[03637] = accumulator_;
    hardware_pop_acc();
    select_alu_group(rau_logical);
    registers_[015] = accumulator_.address();
    return 025421;
}

std::uint16_t Machine::p20077()
{
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    its(015);
    its(015);
    registers_[015] = 020101;
    return 03277;
}

std::uint16_t Machine::p20101()
{
    registers_[010] = 020073;
    const Word48 left = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[020107].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    const bool indirect_return = accumulator_.raw() == 0;
    sti(015);
    sti(015);
    if (indirect_return) {
        registers_[016] = accumulator_.address();
        return registers_[016];
    }
    return registers_[015];
}

std::uint16_t Machine::p21464()
{
    // 21464 saves its private base and return link, then classifies frame
    // word -3 before selecting the indirect-load/evaluation paths below.
    its(001);
    its(015);
    registers_[001] = 021464;
    xts(address_add(registers_[017], -2));
    memory_[021535] = accumulator_;

    const Word48 left = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[021532].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[021532];
        select_alu_group(rau_logical);
        memory_[021535] = accumulator_;
        return 021511;
    }

    accumulator_ = memory_[021535];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[021524];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[021525].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return 021516;
    }

    accumulator_ = memory_[021535];
    select_alu_group(rau_logical);
    registers_[015] = 021473;
    return 05211;
}

std::uint16_t Machine::p21473()
{
    memory_[021533] = accumulator_;
    accumulator_ = accumulator_ & memory_[021523];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[021526].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return 021511;
    }

    accumulator_ = memory_[021535];
    select_alu_group(rau_logical);
    registers_[015] = 021476;
    return 05207;
}

std::uint16_t Machine::p21476()
{
    const Word48 left = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[021527].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[021532];
        select_alu_group(rau_logical);
        memory_[021535] = accumulator_;
        return 021511;
    }

    accumulator_ = memory_[021533];
    select_alu_group(rau_logical);
    xts(021535);
    xts(021533);
    registers_[015] = 021501;
    return 02750;
}

std::uint16_t Machine::p21501()
{
    registers_[015] = 021502;
    return 03277;
}

std::uint16_t Machine::p21502()
{
    stx(021534);
    stx(021535);
    memory_[021533] = accumulator_;
    accumulator_ = memory_[021534];
    select_alu_group(rau_logical);
    const Word48 left = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[021531].raw());
    remainder_ = left;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[021527];
        select_alu_group(rau_logical);
        memory_[memory_[021535].address()] = accumulator_;
        accumulator_ = memory_[021532];
        select_alu_group(rau_logical);
        memory_[021535] = accumulator_;
        return 021511;
    }

    accumulator_ = memory_[021534];
    select_alu_group(rau_logical);
    memory_[memory_[021535].address()] = accumulator_;
    accumulator_ = memory_[021530];
    select_alu_group(rau_logical);
    xts(021533);
    registers_[015] = 021510;
    return 05215;
}

std::uint16_t Machine::p21510()
{
    memory_[address_add(memory_[021535].address(), 1)] = accumulator_;
    return p21511();
}

std::uint16_t Machine::p21511()
{
    accumulator_ = memory_[021535];
    select_alu_group(rau_logical);
    stx(address_add(registers_[017], -3));
    sti(015);
    sti(001);
    return registers_[015];
}

std::uint16_t Machine::p21516()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(015);
    sti(001);
    registers_[016] = 012020;
    return 03014;
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

std::uint16_t Machine::p07472()
{
    // 07472 calls the shared character extractor with the following word as
    // its return. The 07473 continuation supplies the original runtime base.
    registers_[015] = 07473;
    return 021251;
}

std::uint16_t Machine::p07473()
{
    registers_[010] = 07472;
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[07507].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    registers_[015] = 03235;
    return 03275;
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

std::uint16_t Machine::p07533()
{
    hardware_push_acc();
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    registers_[016] = 07545;
    xts(address_add(registers_[016], -1));
    registers_[015] = 07536;
    return 021443;
}

std::uint16_t Machine::p07536()
{
    registers_[013] = 07514;
    registers_[016] = address_add(registers_[013], 062);
    select_alu_group(rau_logical);
    accumulator_ = memory_[address_add(registers_[013], 027)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[013], 064)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[013], 060)];
    select_alu_group(rau_logical);
    stx(address_add(registers_[013], 031));
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p11536()
{
    // 11536..11540: verify the tag bits of frame word -2. The failure path
    // retains the original 11547 diagnostic boundary; success supplies
    // frame word -1 to the shared 11541 body.
    registers_[010] = 011506;
    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 0271)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 014)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    // U1A copies the tested accumulator into RMR whether or not it branches.
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return 011547;
    }
    accumulator_ = memory_[address_add(registers_[017], -1)];
    select_alu_group(rau_logical);
    return 011541;
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
    arithmetic_add(memory_[address_add(registers_[017], -1)],
                   false, true);

    // In additive mode UZA tests the mantissa sign, so both the traced zero
    // and positive differences return through r15.
    if ((accumulator_.raw() & bit41) == 0) {
        return registers_[015];
    }

    registers_[016] = 010100;
    accumulator_ = memory_[address_add(registers_[017], -1)];
    select_alu_group(rau_logical);
    return 03014;
}

std::uint16_t Machine::p11673_begin_generated_update()
{
    // 11673..11674 saves the r14 return, working pointer, and incoming
    // accumulator before obtaining the first POP value at 11675.
    its(014);
    its(016);
    its(016);
    registers_[015] = 011675;
    return 03277;
}

std::uint16_t Machine::p11675_continue_generated_update()
{
    // 11675..11700 records the first POP value and validates the generated
    // object reference saved four hardware-stack words below it.
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    const std::uint16_t descriptor =
        memory_[address_add(registers_[017], -4)].address();
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(descriptor, -2)].raw());
    select_alu_group(rau_logical);
    shift_accumulator(24);
    shift_accumulator(-24);
    registers_[017] = address_add(registers_[017], 1);
    if (accumulator_.raw() != 0) {
        return 011547;
    }

    registers_[015] = 011701;
    return 03277;
}

std::uint16_t Machine::p11701_match_generated_value()
{
    // 11701..11702 saves the second POP value and enters the translated
    // tagged-field comparison with the original runtime-table base.
    memory_[address_add(registers_[017], -1)] = accumulator_;
    registers_[010] = 011506;
    registers_[015] = 011703;
    return 011541;
}

std::uint16_t Machine::p11703_update_generated_value()
{
    // 11703..11707 decrements and retags the second value, derives the
    // numeric field selected by the saved generated descriptor, and hands it
    // to 03413 without collapsing that routine boundary.
    accumulator_ = memory_[address_add(registers_[017], -1)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[010], 0311)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 0312)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[017], -1)] = accumulator_;

    const std::uint16_t selector =
        memory_[address_add(registers_[017], -3)].address();
    accumulator_ = memory_[selector]
        & memory_[address_add(registers_[010], 0274)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0272)].raw());
    select_alu_group(rau_logical);
    memory_[address_add(registers_[017], -6)] = accumulator_;
    registers_[015] = 011710;
    return 03413;
}

std::uint16_t Machine::p11710_finish_generated_update()
{
    // 11710..11716 performs the remaining tagged arithmetic in NTR 3 mode.
    // Its computed return is the address saved five stack words below the
    // multiplication operand: 11720 or 11727 in the observed callers.
    registers_[010] = 011506;
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    alu_mode_ = 003;
    accumulator_ = memory_[address_add(registers_[017], -7)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[address_add(registers_[010], 0313)],
                   false, true);
    arithmetic_add(memory_[address_add(registers_[017], -2)],
                   false, true);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);

    const std::uint16_t selector =
        memory_[address_add(registers_[017], -5)].address();
    accumulator_ = memory_[selector];
    select_alu_group(rau_logical);
    shift_accumulator(6);
    const Word48 multiplicand = accumulator_;
    hardware_pop_acc();
    const Word48 multiplier = accumulator_;
    accumulator_ = multiplicand;
    multiply(multiplier);
    yta(0);
    return memory_[address_add(registers_[017], -5)].address();
}

std::uint16_t Machine::p11717_begin_generated_binding()
{
    // 11717 uses r14, uniquely in the quine trace, as 11673's link.
    its(015);
    registers_[014] = 011720;
    return 011673;
}

std::uint16_t Machine::p11720_finish_generated_binding()
{
    // 11720..11725 extracts one byte from the saved generated object, tags
    // it as a POP value, releases the seven-word frame, and tail-enters the
    // ordinary PUSH_ACC/BIND_ENVIRONMENT bracket.
    registers_[016] = accumulator_.address();
    const std::uint16_t object =
        memory_[address_add(registers_[017], -3)].address();
    registers_[014] = address_add(object, 1);
    const std::uint16_t index =
        memory_[address_add(registers_[017], -1)].address();
    accumulator_ = memory_[address_add(registers_[014], index)];
    select_alu_group(rau_logical);
    shift_accumulator(
        static_cast<int>((0100 + registers_[016]) & 0177) - 64);
    const std::uint16_t mask_base =
        memory_[address_add(registers_[017], -4)].address();
    accumulator_ = accumulator_
        & memory_[address_add(mask_base, 2)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0272)].raw());
    select_alu_group(rau_logical);
    registers_[017] = address_add(registers_[017], -7);
    registers_[015] = 03235;
    return 03275;
}

std::uint16_t Machine::p11726_begin_generated_rebinding()
{
    its(015);
    registers_[014] = 011727;
    return 011673;
}

std::uint16_t Machine::p11727_continue_generated_rebinding()
{
    // 11727..11730 saves the converted selector and obtains the third POP
    // value before the generated record update at 11731.
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0274)].raw());
    select_alu_group(rau_logical);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[015] = 011731;
    return 03277;
}

std::uint16_t Machine::p11731_finish_generated_rebinding()
{
    // 11731..11745 combines two selected byte fields in the generated record
    // and releases the ten-word frame before returning through 03235.
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[010] = 011506;
    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 0303)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0272)].raw());
    select_alu_group(rau_logical);
    if (accumulator_.raw() != 0) {
        return 011551;
    }

    accumulator_ = memory_[address_add(registers_[017], -6)];
    select_alu_group(rau_logical);
    registers_[016] = accumulator_.address();
    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    registers_[014] = accumulator_.address();
    accumulator_ = memory_[address_add(registers_[017], -5)];
    select_alu_group(rau_logical);
    registers_[013] = accumulator_.address();
    registers_[013] = address_add(
        registers_[013],
        address_add(memory_[address_add(registers_[017], -3)].address(), 1));

    accumulator_ = memory_[address_add(registers_[016], 2)];
    select_alu_group(rau_logical);
    const int shift =
        static_cast<int>((1 + registers_[014]) & 0177) - 64;
    shift_accumulator(shift);
    accumulator_ = accumulator_ & memory_[registers_[013]];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[013]].raw());
    select_alu_group(rau_logical);
    memory_[registers_[013]] = accumulator_;

    hardware_pop_acc();
    accumulator_ = accumulator_
        & memory_[address_add(registers_[016], 2)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    shift_accumulator(shift);
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[013]].raw());
    select_alu_group(rau_logical);
    memory_[registers_[013]] = accumulator_;
    registers_[017] = address_add(registers_[017], -010);
    return 03235;
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

std::uint16_t Machine::p16406()
{
    // 16406..16411 conditionally masks and shifts the current record word,
    // folds it into the saved word at r17-7, and advances the two modifiers.
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    if (registers_[004] != 0) {
        accumulator_ = accumulator_
            & memory_[address_add(registers_[001], 074472)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        shift_accumulator(
            static_cast<int>(registers_[005] & 0177) - 64);
        const std::uint16_t saved =
            address_add(registers_[017], -7);
        const Word48 old_accumulator = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ memory_[saved].raw());
        remainder_ = old_accumulator;
        select_alu_group(rau_logical);
        memory_[saved] = accumulator_;
        registers_[005] = address_add(registers_[005], 010);
        registers_[004] = address_add(registers_[004], 1);
    }
    return p16412();
}

std::uint16_t Machine::p16412()
{
    accumulator_ = memory_[address_add(registers_[003], 1)];
    select_alu_group(rau_logical);
    registers_[015] = 016413;
    return 016421;
}

std::uint16_t Machine::p16413()
{
    if (registers_[002] != 0) {
        const Word48 old_accumulator = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[001], 074473)].raw());
        remainder_ = old_accumulator;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (!accumulator_condition()) {
            return address_add(registers_[001], 074135);
        }
    }
    return p16415();
}

std::uint16_t Machine::p16415()
{
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074474)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        return address_add(registers_[001], 074136);
    }
    registers_[015] = 016406;
    return address_add(registers_[001], 074224);
}

std::uint16_t Machine::p16416()
{
    registers_[015] = 016406;
    return address_add(registers_[001], 074224);
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

std::uint16_t Machine::p16467()
{
    // 16467..16470 preserves the incoming accumulator and caller, loads the
    // record head, and enters descriptor advancement at the original link.
    its(015);
    xts(registers_[003]);
    registers_[016] = 016555;
    registers_[015] = 016471;
    return 021443;
}

std::uint16_t Machine::p16471()
{
    accumulator_ = memory_[address_add(registers_[001], 074502)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 074275)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    stx(address_add(registers_[001], 074275));
    sti(015);

    accumulator_ = memory_[address_add(registers_[001], 074274)];
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074323)].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        return registers_[015];
    }

    memory_[address_add(registers_[001], 074275)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 074273)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 074274)] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p16505_begin_record_shift()
{
    // 16505..16506: preserve the incoming accumulator and caller link on the
    // hardware stack, then call the already translated record-shift entry.
    // Its 16463/16467 continuations eventually return at 16507.
    its(015);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[015] = 016507;
    return p16457_shift_record();
}

std::uint16_t Machine::p16507_resume_record_shift()
{
    // 16507..16510: restore the caller link and incoming accumulator, balance
    // r17, and select the original r1-relative continuation at 74216.
    hardware_pop_acc();
    sti(015);
    return address_add(registers_[001], 074216);
}

std::uint16_t Machine::p16605()
{
    registers_[007] = registers_[015];
    registers_[015] = 016606;
    return 016505;
}

std::uint16_t Machine::p16606()
{
    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[address_add(registers_[001], 074510)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[registers_[003]] = accumulator_;
    return registers_[007];
}

std::uint16_t Machine::p17243_scan()
{
    // 17243..17252: reject empty or non-ten-word descriptors, then scan the
    // r13-selected portion of the table based at 03677 for the value at r3+2.
    accumulator_ = memory_[address_add(registers_[003], 2)];
    select_alu_group(rau_logical);
    // UZA copies the tested accumulator into RMR on both outcomes.
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        registers_[016] = 1;
        return registers_[015];
    }

    accumulator_ = memory_[address_add(registers_[003], 4)];
    select_alu_group(rau_logical);
    shift_accumulator(43);
    registers_[016] = accumulator_.address();
    registers_[016] = address_add(registers_[016], -012);
    if (registers_[016] != 0) {
        registers_[016] = 1;
        return registers_[015];
    }

    for (;;) {
        accumulator_ = memory_[address_add(03677, registers_[013])];
        select_alu_group(rau_logical);
        const Word48 old_accumulator = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[003], 2)].raw());
        remainder_ = old_accumulator;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            return registers_[015];
        }
        if (registers_[013] == 0) {
            break;
        }
        registers_[013] = address_add(registers_[013], 1);
    }

    registers_[016] = 1;
    return registers_[015];
}

std::uint16_t Machine::p17242()
{
    registers_[013] = 077750;
    return p17243_scan();
}

std::uint16_t Machine::p17253()
{
    registers_[013] = 077760;
    return p17243_scan();
}

std::uint16_t Machine::p17254_shared()
{
    // WTC 1(r16); UTM -1(r11) derives the next table index from the
    // descriptor's second word.  Nonzero indices are the common leaf path.
    registers_[011] = address_add(
        memory_[address_add(registers_[016], 1)].address(), -1);
    if (registers_[011] != 0) {
        accumulator_ = Word48(registers_[011]);
        select_alu_group(rau_logical);
        memory_[address_add(registers_[016], 1)] = accumulator_;
        const std::uint16_t base = memory_[registers_[016]].address();
        accumulator_ = memory_[address_add(base, registers_[011])];
        select_alu_group(rau_logical);
        return registers_[015];
    }
    return p17260();
}

std::uint16_t Machine::p17260()
{
    registers_[010] = 017254;
    const std::uint16_t base = memory_[registers_[016]].address();
    accumulator_ = memory_[base] & memory_[017343];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = Word48(registers_[016]);
        select_alu_group(rau_logical);
        registers_[016] = 012002;
        return 03014;
    }

    its(015);
    its(016);
    xts(registers_[016]);
    registers_[016] = accumulator_.address();
    accumulator_ = memory_[017350];
    select_alu_group(rau_logical);
    registers_[015] = 017266;
    return 03506;
}

std::uint16_t Machine::p17266()
{
    registers_[010] = 017254;
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(016);
    sti(015);
    memory_[registers_[016]] = accumulator_;
    accumulator_ = memory_[017344];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 1)] = accumulator_;
    const std::uint16_t base = memory_[registers_[016]].address();
    accumulator_ = memory_[address_add(base, 017)];
    select_alu_group(rau_logical);
    return registers_[015];
}

std::uint16_t Machine::p17275_shared()
{
    registers_[011] =
        address_add(memory_[address_add(registers_[016], 1)].address(), -020);
    if (registers_[011] == 0) {
        return p17302();
    }

    const std::uint16_t base = memory_[registers_[016]].address();
    memory_[address_add(base, address_add(registers_[011], 020))] =
        accumulator_;
    registers_[011] = address_add(registers_[011], 021);
    its(011);
    stx(address_add(registers_[016], 1));
    return registers_[015];
}

std::uint16_t Machine::p17302()
{
    registers_[010] = 017254;
    memory_[017351] = accumulator_;
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(016);
    hardware_push_acc();
    registers_[016] = 020;
    registers_[015] = 017306;
    return 05430;
}

std::uint16_t Machine::p17306()
{
    registers_[010] = 017254;
    registers_[014] = registers_[016];
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(016);
    registers_[015] = accumulator_.address();

    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    const Word48 old_accumulator = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[017347].raw());
    remainder_ = old_accumulator;
    select_alu_group(rau_logical);
    memory_[registers_[014]] = accumulator_;

    accumulator_ = Word48(registers_[014]);
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    accumulator_ = memory_[017345];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 1)] = accumulator_;

    accumulator_ = memory_[017351];
    select_alu_group(rau_logical);
    xts(0);
    stx(017351);
    const std::uint16_t base = memory_[registers_[016]].address();
    memory_[address_add(base, 1)] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p17337()
{
    registers_[016] = 017353;
    return p17254_shared();
}

std::uint16_t Machine::p17340()
{
    registers_[016] = 017353;
    return p17275_shared();
}

std::uint16_t Machine::p17341()
{
    registers_[016] = 017355;
    return p17254_shared();
}

std::uint16_t Machine::p17342()
{
    registers_[016] = 017355;
    return p17275_shared();
}

std::uint16_t Machine::p17762()
{
    // Preserve the value and caller in the original two-word hardware-stack
    // frame, then request the two-word object completed at 17764.
    its(015);
    hardware_push_acc();
    registers_[016] = 2;
    registers_[015] = 017764;
    return 05430;
}

std::uint16_t Machine::p17764()
{
    // NTR 3 precedes the load in the original sequence. XTA immediately
    // selects the logical group.
    alu_mode_ = 3;
    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    stx(registers_[016]);
    sti(015);

    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    const Word48 allocated = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^
                          memory_[address_add(registers_[001], 0205)].raw());
    remainder_ = allocated;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[002], 1)] = accumulator_;
    registers_[002] = accumulator_.address();

    accumulator_ = Word48(registers_[003]);
    select_alu_group(rau_logical);
    const Word48 owner = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0205)].raw());
    remainder_ = owner;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 1)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 0235)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 0164)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[address_add(registers_[001], 0235)] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p17774()
{
    // The empty-link case leaves all machine data untouched and joins the
    // caller's restoration block at 17756.
    if (registers_[003] == 0) {
        return 017756;
    }

    accumulator_ = memory_[registers_[003]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 0166)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0204)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        const std::uint16_t continuation =
            address_add(registers_[001], 0156);
        return continuation == 020002 ? p20002() : continuation;
    }

    registers_[002] = registers_[003];
    accumulator_ = memory_[address_add(registers_[003], 1)];
    select_alu_group(rau_logical);
    registers_[003] = accumulator_.address();

    const std::uint16_t counter =
        address_add(registers_[001], 0235);
    accumulator_ = memory_[counter];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 0164)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[counter] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p20002()
{
    const std::uint16_t counter =
        address_add(registers_[001], 0235);

    for (;;) {
        accumulator_ = memory_[counter];
        select_alu_group(rau_logical);
        // WTC (r3) applies the current record word's low address to the
        // following ATX 0 without otherwise changing the ALU state.
        const std::uint16_t stamp_address =
            memory_[registers_[003]].address();
        memory_[stamp_address] = accumulator_;

        registers_[002] = registers_[003];
        accumulator_ = memory_[address_add(registers_[003], 1)];
        select_alu_group(rau_logical);
        registers_[003] = accumulator_.address();

        accumulator_ = memory_[registers_[003]];
        select_alu_group(rau_logical);
        accumulator_ = accumulator_
            & memory_[address_add(registers_[001], 0166)];
        remainder_ = Word48();
        select_alu_group(rau_logical);
        const Word48 masked = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[001], 0204)].raw());
        remainder_ = masked;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() != 0) {
            return registers_[015];
        }
        const std::uint16_t continuation =
            address_add(registers_[001], 0156);
        if (continuation != 020002) {
            return continuation;
        }
    }
}

std::uint16_t Machine::p20170_input_primary()
{
    // 20170..20174 checks terminal availability and whether the packed input
    // descriptor must first emit its 0377 terminator through 25356.
    registers_[010] = 020170;
    accumulator_ = memory_[020377];
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        return 020321;
    }

    accumulator_ = memory_[025417];
    select_alu_group(rau_logical);
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[020361].raw());
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        return 020177;
    }

    accumulator_ = Word48(registers_[015]);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[015] = 020175;
    return 025356;
}

std::uint16_t Machine::p20175_resume_input_primary()
{
    registers_[010] = 020170;
    hardware_pop_acc();
    registers_[015] = accumulator_.address();
    return 020177;
}

std::uint16_t Machine::p20177_continue_input_primary()
{
    // A nonzero continuation state performs the readiness Э71 at 20200.
    // Keeping that extracode as an instruction boundary preserves resumable
    // host input semantics in Machine::step().
    accumulator_ = memory_[020362];
    select_alu_group(rau_logical);
    return accumulator_.raw() == 0 ? 020202 : 020200;
}

std::uint16_t Machine::p20201_resume_input_status()
{
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[020365].raw());
    select_alu_group(rau_logical);
    return accumulator_.raw() == 0 ? 020715 : 020202;
}

std::uint16_t Machine::p20202_prepare_input_transfer()
{
    // 20202..20205 builds the indexed Э71 input control word. Execution
    // stops immediately before the potentially blocking extracode at 20205.
    accumulator_ = memory_[020372];
    select_alu_group(rau_logical);
    memory_[020400] = accumulator_;
    accumulator_ = memory_[020374];
    select_alu_group(rau_logical);
    shift_accumulator(-37);
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[020363].raw());
    select_alu_group(rau_logical);
    memory_[020364] = accumulator_;
    return 020205;
}

std::uint16_t Machine::p20206_resume_input_transfer()
{
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[020362] = accumulator_;
    return 020207;
}

std::uint16_t Machine::p20210_finish_input_primary()
{
    // 20210..20223 decodes the readiness word and returns on the traced
    // terminal path. Alternate Э63/Э64 cases remain exact fallback entries.
    accumulator_ = pack_bits(accumulator_, memory_[020365]);
    remainder_ = Word48();
    select_alu_group(rau_logical);
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[020322].raw());
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        return 020226;
    }
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[020323].raw());
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        return 020715;
    }

    accumulator_ = memory_[020337];
    select_alu_group(rau_logical);
    memory_[020366] = accumulator_;
    accumulator_ = memory_[020373];
    select_alu_group(rau_logical);
    if (accumulator_.raw() != 0) {
        return 020214;
    }

    accumulator_ = memory_[020400] & memory_[020325];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[020340].raw());
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        return 020232;
    }

    accumulator_ = Word48(
        memory_[020374].raw() ^ memory_[020326].raw());
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[020375];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    accumulator_ = accumulator_ & memory_[020326];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    return accumulator_.raw() == 0 ? registers_[015] : 020223;
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

std::uint16_t Machine::p20263()
{
    registers_[010] = 020170;
    registers_[011] = 025415;
    emulate_e71(0);
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 0200)] = accumulator_;
    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 0144)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 0206)] = accumulator_;
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        accumulator_ = memory_[address_add(registers_[010], 0165)];
        select_alu_group(rau_logical);
        memory_[registers_[011]] = accumulator_;
        accumulator_ = memory_[address_add(registers_[010], 0167)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[011], 1)] = accumulator_;
        accumulator_ = memory_[address_add(registers_[010], 0163)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[010], 0173)] = accumulator_;
    } else {
        accumulator_ = memory_[address_add(registers_[010], 0166)];
        select_alu_group(rau_logical);
        memory_[registers_[011]] = accumulator_;
        accumulator_ = memory_[address_add(registers_[010], 0170)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[011], 1)] = accumulator_;
        accumulator_ = memory_[address_add(registers_[010], 0164)];
        select_alu_group(rau_logical);
        memory_[address_add(registers_[010], 0173)] = accumulator_;
    }
    accumulator_ = memory_[address_add(registers_[010], 0171)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[011], 2)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[010], 0200)];
    select_alu_group(rau_logical);
    shift_accumulator(24);
    hardware_push_acc();
    shift_accumulator(-24);
    registers_[017] = address_add(registers_[017], -1);
    const Word48 low_part = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[017]].raw());
    remainder_ = low_part;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 0175)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[010], 0200)];
    select_alu_group(rau_logical);
    shift_accumulator(24);
    memory_[address_add(registers_[010], 0201)] = accumulator_;
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        unsigned count = 0;
        for (std::uint64_t bits = accumulator_.raw(); bits != 0;
             bits &= bits - 1) {
            ++count;
        }
        accumulator_ = cyclic_add(Word48(count), memory_[0]);
        remainder_ = Word48();
        select_alu_group(rau_logical);
        const Word48 counted = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw()
            ^ memory_[address_add(registers_[010], 0136)].raw());
        remainder_ = counted;
        select_alu_group(rau_logical);
        remainder_ = accumulator_;
        if (accumulator_.raw() == 0) {
            accumulator_ = memory_[address_add(registers_[010], 0201)];
            select_alu_group(rau_logical);
            const Word48 high_part = accumulator_;
            accumulator_ = Word48(
                accumulator_.raw()
                ^ memory_[address_add(registers_[010], 0144)].raw());
            remainder_ = high_part;
            select_alu_group(rau_logical);
            accumulator_ = accumulator_
                & memory_[address_add(registers_[010], 0200)];
            remainder_ = Word48();
            select_alu_group(rau_logical);
            remainder_ = accumulator_;
            if (accumulator_.raw() == 0) {
                accumulator_ = memory_[address_add(registers_[010], 0200)];
                select_alu_group(rau_logical);
                if (accumulator_.raw() != 0) {
                    unsigned width = 32;
                    unsigned index = 0;
                    std::uint64_t bits = accumulator_.raw();
                    do {
                        const std::uint64_t high = bits >> width;
                        if (high != 0) {
                            index += width;
                            bits = high;
                        }
                    } while ((width >>= 1) != 0);
                    const unsigned position = 48 - index;
                    shift_accumulator(48 - static_cast<int>(position));
                    accumulator_ = cyclic_add(Word48(position), memory_[0]);
                } else {
                    remainder_ = Word48();
                    accumulator_ = memory_[0];
                }
                select_alu_group(rau_logical);
                memory_[address_add(registers_[010], 0207)] = accumulator_;
            }
        }
    }

    accumulator_ = memory_[address_add(registers_[010], 0207)];
    select_alu_group(rau_logical);
    shift_accumulator(-12);
    memory_[address_add(registers_[010], 0200)] = accumulator_;
    Word48 before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0173)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 0173)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[010], 0200)];
    select_alu_group(rau_logical);
    before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 0162)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 0177)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[010], 0147)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 0176)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[010], 0145)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 0202)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[010], 0137)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 0222)] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p20144()
{
    registers_[010] = 020144;
    accumulator_ = memory_[address_add(registers_[010], 017)];
    select_alu_group(rau_logical);
    registers_[016] = 0103;
    accumulator_ = memory_[address_add(registers_[010], 020)];
    select_alu_group(rau_logical);
    registers_[016] = 0102;
    return 020564;
}

std::uint16_t Machine::p20150()
{
    registers_[010] = 020144;
    memory_[address_add(registers_[010], 023)] = accumulator_;
    registers_[015] = 020152;
    return 020564;
}

std::uint16_t Machine::p20152()
{
    registers_[010] = 020144;
    registers_[016] = 0101;
    select_alu_group(rau_logical);
    registers_[014] = accumulator_.address();
    const Word48 before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 021)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    return accumulator_.raw() == 0 ? p20161() : p20155();
}

std::uint16_t Machine::p20155()
{
    accumulator_ = memory_[address_add(registers_[010], 017)];
    select_alu_group(rau_logical);
    registers_[016] = 0103;
    accumulator_ = memory_[address_add(registers_[010], 020)];
    select_alu_group(rau_logical);
    registers_[016] = 013000;
    modifier_add(016, 014);
    accumulator_ = memory_[address_add(registers_[010], 023)];
    select_alu_group(rau_logical);
    return 03014;
}

std::uint16_t Machine::p20161()
{
    registers_[017] = address_add(memory_[017010].address(), 1);
    return p20155();
}

std::uint16_t Machine::p20456()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(001);
    hardware_push_acc();
    registers_[001] = 020456;
    registers_[016] = 010;
    accumulator_ = Word48(local_jiffies_since_midnight());
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0104)] = accumulator_;
    registers_[015] = 020462;
    return 025641;
}

std::uint16_t Machine::p20462()
{
    const Word48 formatted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 041)].raw());
    remainder_ = formatted;
    select_alu_group(rau_logical);
    stx(address_add(registers_[001], 054));
    memory_[address_add(registers_[001], 053)] = accumulator_;
    registers_[016] = 020554;
    alu_mode_ = 003;
    accumulator_ = memory_[address_add(registers_[001], 0104)];
    select_alu_group(rau_logical);
    arithmetic_add(memory_[address_add(registers_[001], 042)],
                   false, true);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) == 0) {
        registers_[016] = 020556;
        arithmetic_add(memory_[address_add(registers_[001], 043)],
                       false, true);
        remainder_ = accumulator_;
        if ((accumulator_.raw() & bit41) == 0) {
            registers_[016] = 020560;
        }
    }
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 055)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[016], 1)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 056)] = accumulator_;
    registers_[016] = 020526;
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ = memory_[registers_[017]];
    select_alu_group(rau_logical);
    sti(001);
    registers_[015] = accumulator_.address();
    return 020674;
}

std::uint16_t Machine::p20475()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(001);
    hardware_push_acc();
    registers_[001] = 020456;
    registers_[016] = 010;
    accumulator_ = Word48(local_jiffies_since_midnight());
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0105)] = accumulator_;
    registers_[015] = 020501;
    return 025641;
}

std::uint16_t Machine::p20501()
{
    Word48 before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 044)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    stx(address_add(registers_[001], 062));
    memory_[address_add(registers_[001], 061)] = accumulator_;

    accumulator_ = memory_[address_add(registers_[001], 0105)];
    select_alu_group(rau_logical);
    alu_mode_ = 007;
    arithmetic_add(memory_[address_add(registers_[001], 0104)],
                   false, true);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit41) != 0) {
        arithmetic_add(memory_[address_add(registers_[001], 045)],
                       false, false);
    }
    registers_[015] = 020506;
    return 025641;
}

std::uint16_t Machine::p20506()
{
    const Word48 before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 046)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    stx(address_add(registers_[001], 066));
    memory_[address_add(registers_[001], 065)] = accumulator_;

    registers_[016] = 04;
    accumulator_ = Word48(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - execution_started_at_)
            .count()
        / 20);
    select_alu_group(rau_logical);
    registers_[015] = 020511;
    return 025641;
}

std::uint16_t Machine::p20511()
{
    const Word48 before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 047)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    stx(address_add(registers_[001], 071));
    memory_[address_add(registers_[001], 070)] = accumulator_;
    registers_[016] = 020536;
    registers_[015] = 020514;
    return 020674;
}

std::uint16_t Machine::p20514()
{
    registers_[016] = 020550;
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(001);
    registers_[015] = accumulator_.address();
    return 020674;
}

std::uint16_t Machine::p20660()
{
    accumulator_ = memory_[017];
    select_alu_group(rau_logical);
    registers_[016] = accumulator_.address();
    accumulator_ = Word48(registers_[016]);
    select_alu_group(rau_logical);
    memory_[05502] = accumulator_;
    alu_mode_ = 003;
    reverse_subtract(memory_[017010]);
    shift_accumulator(-24);
    memory_[registers_[016]] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p20667()
{
    registers_[015] = 020670;
    return 016341;
}

std::uint16_t Machine::p21107()
{
    registers_[010] = 021075;
    accumulator_ = memory_[address_add(registers_[010], 027)];
    select_alu_group(rau_logical);
    registers_[014] = accumulator_.address();
    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 023)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    its(016);
    registers_[017] = address_add(registers_[017], -1);
    Word48 before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[017]].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 027)] = accumulator_;
    accumulator_ = Word48(registers_[014]);
    select_alu_group(rau_logical);
    before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 025)].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[016], 1)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[014], 1)];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 024)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    its(016);
    shift_accumulator(-24);
    registers_[017] = address_add(registers_[017], -1);
    before = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[registers_[017]].raw());
    remainder_ = before;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[014], 1)] = accumulator_;
    return registers_[015];
}

std::uint16_t Machine::p20673_return()
{
    return registers_[015];
}

std::uint16_t Machine::p20674()
{
    registers_[010] = 020674;
    registers_[014] = registers_[016];
    accumulator_ = memory_[020377];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        registers_[016] = address_add(registers_[010], 016);
        select_alu_group(rau_logical);
        return registers_[015];
    }

    shift_accumulator(-12);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 015)].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 020)] = accumulator_;
    emulate_e71(address_add(registers_[010], 014));
    select_alu_group(rau_logical);
    emulate_e71(address_add(registers_[010], 020));
    select_alu_group(rau_logical);
    emulate_e71(address_add(registers_[010], 014));
    select_alu_group(rau_logical);

    accumulator_ = memory_[020375];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 013)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        registers_[016] = address_add(registers_[010], 016);
        select_alu_group(rau_logical);
    }
    return registers_[015];
}

std::uint16_t Machine::p25223()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    xts(0);
    xts(0);
    registers_[015] = 025225;
    return 016151;
}

std::uint16_t Machine::p25225()
{
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        xts(0);
        registers_[015] = 025225;
        return 016151;
    }
    return p25226();
}

std::uint16_t Machine::p25226()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    registers_[015] = 025226;
    remainder_ = accumulator_;
    if (accumulator_condition()) {
        return 016145;
    }
    registers_[017] = address_add(registers_[017], -1);
    return memory_[registers_[017]].address();
}

std::uint16_t Machine::p21251_extract_character()
{
    // 21251..21252 preserves the caller link and enters TOKEN_SOURCE.
    accumulator_ = Word48(registers_[015]);
    memory_[registers_[017]] = accumulator_;
    registers_[017] = address_add(registers_[017], 1);
    registers_[015] = 021253;
    return 025370;
}

std::uint16_t Machine::p21253_resume_character_extract()
{
    // XTS -2 / STI r15 restores the TOKEN_SOURCE result after recovering the
    // original caller, then releases 21251's saved-link word.
    hardware_push_acc();
    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    sti(015);
    registers_[017] = address_add(registers_[017], -1);
    return 021274;
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

    const int shift = static_cast<int>((descriptor.raw() >> 41) & 0177) - 64;
    shift_accumulator(shift);
    remainder_ = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[registers_[014]].raw());
    select_alu_group(rau_logical);

    xts(021457);
    shift_accumulator(shift);
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ = accumulator_ & memory_[registers_[017]];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    accumulator_ = Word48(accumulator_.raw() ^ memory_[registers_[014]].raw());
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
    const std::uint16_t character_address = address_add(registers_[017], -1);
    xts(character_address);
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
    return registers_[015];
}

std::uint16_t Machine::p25356_emit_end_character()
{
    registers_[010] = 025346;
    accumulator_ = memory_[025407];
    select_alu_group(rau_logical);
    return registers_[010];
}

std::uint16_t Machine::p25370_begin_token_source()
{
    // 25370..25372 saves its input and caller in a two-word frame. A clear
    // 25420 state requests a refill through INPUT_PRIMARY.
    registers_[010] = 025346;
    its(015);
    xts(025420);
    if (accumulator_.raw() != 0) {
        return 025376;
    }
    registers_[015] = 025373;
    return 020170;
}

std::uint16_t Machine::p25373_resume_token_source()
{
    // 25373..25376 installs the packed input descriptor and requests one
    // byte from the already translated BUFFER_CHAR routine.
    registers_[010] = 025346;
    accumulator_ = memory_[025411];
    select_alu_group(rau_logical);
    memory_[025413] = accumulator_;
    accumulator_ = memory_[025406];
    select_alu_group(rau_logical);
    memory_[025420] = accumulator_;
    return 025376;
}

std::uint16_t Machine::p25376_fetch_token_character()
{
    registers_[016] = 025413;
    registers_[015] = 025377;
    return 021431;
}

std::uint16_t Machine::p25377_finish_token_source()
{
    // 25377..25405 retains the byte in the frame, handles the 0377 refill
    // marker, and advances the packed-input count before the common return.
    registers_[010] = 025346;
    memory_[address_add(registers_[017], -2)] = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[025407].raw());
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        memory_[025420] = accumulator_;
        memory_[025414] = accumulator_;
        return 025364;
    }

    accumulator_ = memory_[025414];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(accumulator_, memory_[025406]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    memory_[025414] = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[025415].raw());
    select_alu_group(rau_logical);
    if (accumulator_.raw() == 0) {
        memory_[025420] = accumulator_;
        memory_[025414] = accumulator_;
    }
    return 025364;
}

std::uint16_t Machine::p25427()
{
    accumulator_ = Word48(registers_[015]);
    select_alu_group(rau_logical);
    its(001);
    hardware_push_acc();
    registers_[001] = 025427;

    accumulator_ = memory_[address_add(registers_[001], 064)];
    select_alu_group(rau_logical);
    memory_[017570] = accumulator_;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0101)] = accumulator_;
    return p25434();
}

std::uint16_t Machine::p25434()
{
    accumulator_ = memory_[017567];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 065)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[017567] = accumulator_;
    return p25437();
}

std::uint16_t Machine::p25437()
{
    accumulator_ = memory_[03637];
    select_alu_group(rau_logical);
    shift_accumulator(41);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 072)].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p25450();
    }

    const std::uint16_t descriptor =
        address_add(memory_[03641].address(), -1);
    accumulator_ = memory_[descriptor];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 066)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 073)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p25451();
    }

    registers_[015] = 025445;
    return 017472;
}

std::uint16_t Machine::p25445()
{
    registers_[015] = 025446;
    return 017070;
}

std::uint16_t Machine::p25446()
{
    accumulator_ = memory_[address_add(registers_[001], 0101)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() == 0) {
        return p25434();
    }
    return p25437();
}

std::uint16_t Machine::p25450()
{
    registers_[016] = 02022;
    return 03014;
}

std::uint16_t Machine::p25451()
{
    accumulator_ = memory_[03637];
    select_alu_group(rau_logical);
    const Word48 descriptor = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 075)].raw());
    remainder_ = descriptor;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p25460();
    }

    registers_[016] = 017567;
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 065)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 074)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    registers_[015] = 025437;
    return 017075;
}

std::uint16_t Machine::p25457()
{
    registers_[015] = 025437;
    return 017070;
}

std::uint16_t Machine::p25460()
{
    accumulator_ = memory_[03637];
    select_alu_group(rau_logical);
    const Word48 descriptor = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 076)].raw());
    remainder_ = descriptor;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p25471();
    }

    registers_[015] = 025463;
    return 06343;
}

std::uint16_t Machine::p25463()
{
    memory_[address_add(registers_[001], 0102)] = accumulator_;
    accumulator_ = memory_[address_add(registers_[001], 0102)];
    select_alu_group(rau_logical);
    shift_accumulator(41);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 067)].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p25470();
    }

    accumulator_ = memory_[address_add(registers_[001], 0102)];
    select_alu_group(rau_logical);
    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 070)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        return p25470();
    }

    accumulator_ = cyclic_add(
        accumulator_, memory_[address_add(registers_[001], 071)]);
    remainder_ = Word48();
    select_alu_group(rau_multiplicative);
    remainder_ = accumulator_;
    if ((accumulator_.raw() & bit48) != 0) {
        return p25507();
    }
    return p25470();
}

std::uint16_t Machine::p25470()
{
    registers_[016] = 02021;
    return 03014;
}

std::uint16_t Machine::p25471()
{
    accumulator_ = memory_[address_add(registers_[001], 0101)];
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p25502();
    }

    accumulator_ = memory_[03637];
    select_alu_group(rau_logical);
    const Word48 descriptor = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 077)].raw());
    remainder_ = descriptor;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p25500();
    }

    accumulator_ = memory_[address_add(registers_[001], 064)];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0101)] = accumulator_;
    return p25475();
}

std::uint16_t Machine::p25475()
{
    registers_[016] = 017567;
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 065)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    return p25457();
}

std::uint16_t Machine::p25500()
{
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(001);
    registers_[015] = accumulator_.address();
    return registers_[015];
}

std::uint16_t Machine::p25502()
{
    accumulator_ = memory_[03637];
    select_alu_group(rau_logical);
    const Word48 descriptor = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[001], 0100)].raw());
    remainder_ = descriptor;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return p25506();
    }

    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[001], 0101)] = accumulator_;
    return p25475();
}

std::uint16_t Machine::p25506()
{
    registers_[016] = 02024;
    return 03014;
}

std::uint16_t Machine::p25507()
{
    registers_[016] = 017567;
    accumulator_ = memory_[registers_[016]];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[001], 065)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    xts(address_add(registers_[001], 0102));
    shift_accumulator(-43);

    const Word48 shifted = accumulator_;
    hardware_pop_acc();
    accumulator_ = Word48(
        shifted.raw() ^ accumulator_.raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    memory_[registers_[016]] = accumulator_;
    return p25457();
}

std::uint16_t Machine::p25556()
{
    registers_[010] = 025556;
    accumulator_ = memory_[0];
    select_alu_group(rau_logical);
    memory_[017566] = accumulator_;
    memory_[017570] = accumulator_;

    accumulator_ = memory_[03637];
    select_alu_group(rau_logical);
    memory_[address_add(registers_[010], 016)] = accumulator_;
    shift_accumulator(41);
    const Word48 shifted = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 014)].raw());
    remainder_ = shifted;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        registers_[016] = 02022;
        return 03014;
    }

    const std::uint16_t descriptor =
        address_add(memory_[03641].address(), -1);
    accumulator_ = memory_[descriptor];
    select_alu_group(rau_logical);
    accumulator_ = accumulator_
        & memory_[address_add(registers_[010], 013)];
    remainder_ = Word48();
    select_alu_group(rau_logical);
    const Word48 masked = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[010], 015)].raw());
    remainder_ = masked;
    select_alu_group(rau_logical);
    remainder_ = accumulator_;
    if (accumulator_.raw() != 0) {
        return 017472;
    }
    registers_[016] = 02023;
    return 03014;
}

std::uint16_t Machine::p25641()
{
    registers_[010] = 025641;
    registers_[017] = address_add(registers_[017], 1);
    its(015);
    hardware_push_acc();
    alu_mode_ = 007;
    accumulator_ = memory_[address_add(registers_[017], -2)];
    select_alu_group(rau_logical);
    multiply(memory_[025671]);
    multiply(memory_[025670]);
    memory_[025673] = accumulator_;
    yta(0);
    multiply(memory_[025666]);
    registers_[015] = 025647;
    return 025660;
}

std::uint16_t Machine::p25647()
{
    const Word48 first_digit = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[025665].raw());
    remainder_ = first_digit;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[017], -2)] = accumulator_;

    accumulator_ = memory_[025673];
    select_alu_group(rau_logical);
    multiply(memory_[025670]);
    memory_[025673] = accumulator_;
    yta(0);
    multiply(memory_[025666]);
    registers_[015] = 025653;
    return 025660;
}

std::uint16_t Machine::p25653()
{
    shift_accumulator(24);
    memory_[address_add(registers_[017], -3)] = accumulator_;
    accumulator_ = memory_[025673];
    select_alu_group(rau_logical);
    registers_[015] = 025655;
    return 025660;
}

std::uint16_t Machine::p25655()
{
    const Word48 last_digit = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw()
        ^ memory_[address_add(registers_[017], -3)].raw());
    remainder_ = last_digit;
    select_alu_group(rau_logical);
    memory_[address_add(registers_[017], -3)] = accumulator_;
    hardware_pop_acc();
    select_alu_group(rau_logical);
    sti(015);
    return registers_[015];
}

std::uint16_t Machine::p25660()
{
    multiply(memory_[025672]);
    memory_[025674] = accumulator_;
    yta(0);
    multiply(memory_[025667]);
    shift_accumulator(-32);
    xts(025674);
    shift_accumulator(-40);

    const Word48 shifted_product = accumulator_;
    hardware_pop_acc();
    accumulator_ = Word48(
        shifted_product.raw() ^ accumulator_.raw());
    remainder_ = shifted_product;
    select_alu_group(rau_logical);

    const Word48 packed = accumulator_;
    accumulator_ = Word48(
        accumulator_.raw() ^ memory_[025665].raw());
    remainder_ = packed;
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
            // 03222 uses long-address ATX (-1 through r13), not the short
            // STX stack instruction. Both preceding XTS values remain for
            // the matching restoration loop at 03235.
            memory_[address_add(registers_[013], -1)] = accumulator_;
            accumulator_ = memory_[03271];
            select_alu_group(rau_logical);
            memory_[registers_[013]] = accumulator_;

            accumulator_ = memory_[registers_[011]];
            accumulator_ =
                Word48(accumulator_.raw() & memory_[03266].raw());
            remainder_ = Word48();
            select_alu_group(rau_logical);
            remainder_ = accumulator_;
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
    registers_[010] = 020110;
    its(002);
    its(015);
    its(016);

    accumulator_ =
        Word48(accumulator_.raw() ^ memory_[020142].raw());
    accumulator_ = cyclic_add(accumulator_, memory_[020143]);
    registers_[002] = accumulator_.address();

    its(017);
    registers_[017] = address_add(registers_[017], -1);
    accumulator_ =
        cyclic_add(accumulator_, memory_[registers_[017]]);
    memory_[020141] = accumulator_;

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

    const std::uint16_t activation_end = memory_[020141].address();
    registers_[017] = address_add(activation_end, -5);
    return registers_[015];
}

} // namespace poplan
