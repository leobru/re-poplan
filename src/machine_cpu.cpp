// BESM-6 instruction execution adapted from the Dubna simulator.
// Copyright (c) 2022-2023 Leonid Broukhis, Serge Vakulenko
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "poplan/machine.hpp"

#include <istream>
#include <sstream>

namespace poplan {

namespace {

constexpr std::uint64_t bit40 = 00010000000000000ULL;
constexpr std::uint64_t bit41 = 00020000000000000ULL;
constexpr std::uint64_t bit48 = 04000000000000000ULL;
constexpr std::uint8_t rau_logical = 004;
constexpr std::uint8_t rau_multiplicative = 010;
constexpr std::uint8_t rau_additive = 020;
constexpr std::uint8_t rau_group_mask = 034;

struct Instruction {
    std::uint8_t reg = 0;
    std::uint16_t opcode = 0;
    std::uint16_t address = 0;
};

Instruction decode_instruction(std::uint32_t half)
{
    Instruction instruction;
    instruction.reg = static_cast<std::uint8_t>((half >> 20) & 017);
    if ((half & (std::uint32_t{1} << 19)) != 0) {
        instruction.opcode = static_cast<std::uint16_t>(
            (half >> 12) & 0370);
        instruction.address = static_cast<std::uint16_t>(half & 077777);
    } else {
        instruction.opcode = static_cast<std::uint16_t>(
            (half >> 12) & 077);
        instruction.address = static_cast<std::uint16_t>(half & 07777);
        if ((half & (std::uint32_t{1} << 18)) != 0) {
            instruction.address |= 070000;
        }
    }
    return instruction;
}

unsigned highest_position(std::uint64_t value)
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

std::string unsupported_instruction(std::uint16_t pc,
                                    std::uint16_t opcode)
{
    std::ostringstream message;
    message << "BESM-6 instruction " << std::oct << opcode
            << " is unsupported at " << pc;
    return message.str();
}

} // namespace

void Machine::load_image(std::istream &input)
{
    constexpr std::size_t image_words = 036000;
    for (std::size_t address = 0; address < image_words; ++address) {
        std::uint64_t value = 0;
        for (unsigned byte = 0; byte < 6; ++byte) {
            const int next = input.get();
            if (next == std::char_traits<char>::eof()) {
                throw MachineError("POPLAN image is shorter than 036000 words");
            }
            value = (value << 8) | static_cast<unsigned char>(next);
        }
        memory_[address] = Word48(value);
    }
    if (input.get() != std::char_traits<char>::eof()) {
        throw MachineError("POPLAN image is longer than 036000 words");
    }
}

void Machine::start(std::uint16_t address, bool right_half)
{
    program_counter_ = address & 077777;
    right_half_ = right_half;
    instruction_modifier_ = 0;
    instruction_count_ = 0;
    translated_routine_count_ = 0;
}

void Machine::boot_static_image()
{
    // The bootstrap leaves the architectural zero word cleared even though
    // address 00000 in the raw zone extraction contains loader data.
    memory_[0] = Word48();
    accumulator_ = Word48(00010150000421216ULL);
    remainder_ = Word48();
    alu_mode_ = 0;
    registers_.fill(0);
    registers_[001] = 00141;
    registers_[010] = 076000;
    registers_[017] = 053401;
    start(01000);
}

ExecutionStatus Machine::step()
{
    if (translated_routines_enabled_ && dispatch_translated_routine()) {
        ++instruction_count_;
        return ExecutionStatus::running;
    }

    const std::uint16_t old_pc = program_counter_;
    const bool old_right = right_half_;
    const std::uint16_t applied_modifier = instruction_modifier_;

    const std::uint64_t word = memory_[program_counter_].raw();
    const std::uint32_t half = static_cast<std::uint32_t>(
        right_half_ ? word & 077777777ULL : word >> 24);
    Instruction instruction = decode_instruction(half);

    const std::uint16_t next_word = address_add(program_counter_, 1);
    if (right_half_) {
        program_counter_ = next_word;
        right_half_ = false;
    } else {
        right_half_ = true;
    }
    instruction_modifier_ = 0;
    instruction.address = address_add(
        instruction.address, applied_modifier);

    const auto effective_address = [&]() {
        return address_add(instruction.address,
                           registers_[instruction.reg]);
    };
    const auto stack_operand = [&]() {
        if (instruction.address == 0 && instruction.reg == 017) {
            registers_[017] = address_add(registers_[017], -1);
        }
        return memory_[effective_address()];
    };
    const auto logical_condition = [&]() {
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
    };
    const auto branch = [&](std::uint16_t address) {
        program_counter_ = address;
        right_half_ = false;
    };

    switch (instruction.opcode) {
    case 000: {
        const std::uint16_t address = effective_address();
        memory_[address] = accumulator_;
        if (instruction.address == 0 && instruction.reg == 017) {
            registers_[017] = address_add(registers_[017], 1);
        }
        break;
    }
    case 001:
        memory_[effective_address()] = accumulator_;
        hardware_pop_acc();
        select_alu_group(rau_logical);
        break;
    case 003:
        hardware_push_acc();
        accumulator_ = memory_[effective_address()];
        select_alu_group(rau_logical);
        break;
    case 004:
        arithmetic_add(stack_operand(), false, false);
        break;
    case 005:
        arithmetic_add(stack_operand(), false, true);
        break;
    case 006:
        arithmetic_add(stack_operand(), true, false);
        break;
    case 007:
        arithmetic_add(stack_operand(), true, true);
        break;
    case 010:
        accumulator_ = stack_operand();
        select_alu_group(rau_logical);
        break;
    case 011:
        accumulator_ = accumulator_ & stack_operand();
        remainder_ = Word48();
        select_alu_group(rau_logical);
        break;
    case 012: {
        const Word48 old_accumulator = accumulator_;
        accumulator_ = Word48(
            accumulator_.raw() ^ stack_operand().raw());
        remainder_ = old_accumulator;
        select_alu_group(rau_logical);
        break;
    }
    case 013:
        accumulator_ = cyclic_add(accumulator_, stack_operand());
        remainder_ = Word48();
        select_alu_group(rau_multiplicative);
        break;
    case 014:
        change_sign((stack_operand().raw() & bit41) != 0);
        break;
    case 015:
        accumulator_ = Word48(
            accumulator_.raw() | stack_operand().raw());
        remainder_ = Word48();
        select_alu_group(rau_logical);
        break;
    case 016:
        divide(stack_operand());
        break;
    case 017:
        multiply(stack_operand());
        break;
    case 020:
        accumulator_ = pack_bits(accumulator_, stack_operand());
        remainder_ = Word48();
        select_alu_group(rau_logical);
        break;
    case 021:
        accumulator_ = unpack_bits(accumulator_, stack_operand());
        remainder_ = Word48();
        select_alu_group(rau_logical);
        break;
    case 022: {
        unsigned count = 0;
        for (std::uint64_t value = accumulator_.raw(); value != 0;
             value &= value - 1) {
            ++count;
        }
        accumulator_ = cyclic_add(Word48(count), stack_operand());
        remainder_ = Word48();
        select_alu_group(rau_logical);
        break;
    }
    case 023:
        if (accumulator_.raw() != 0) {
            const unsigned position = highest_position(accumulator_.raw());
            shift_accumulator(48 - static_cast<int>(position));
            accumulator_ = cyclic_add(
                Word48(position), stack_operand());
        } else {
            remainder_ = Word48();
            accumulator_ = stack_operand();
        }
        select_alu_group(rau_logical);
        break;
    case 024:
        add_exponent(
            static_cast<int>((stack_operand().raw() >> 41) & 0177) - 64);
        select_alu_group(rau_multiplicative);
        break;
    case 025:
        add_exponent(
            64 - static_cast<int>((stack_operand().raw() >> 41) & 0177));
        select_alu_group(rau_multiplicative);
        break;
    case 026: {
        const int count = static_cast<int>(
            (stack_operand().raw() >> 41) & 0177) - 64;
        shift_accumulator(count);
        break;
    }
    case 027:
        alu_mode_ = static_cast<std::uint8_t>(
            (stack_operand().raw() >> 41) & 077);
        break;
    case 030:
        accumulator_ = Word48(
            static_cast<std::uint64_t>(
                alu_mode_ & effective_address() & 0177) << 41);
        select_alu_group(rau_logical);
        break;
    case 031:
        yta(static_cast<int>(effective_address() & 0177) - 64);
        break;
    case 034:
        add_exponent(static_cast<int>(effective_address() & 0177) - 64);
        select_alu_group(rau_multiplicative);
        break;
    case 035:
        add_exponent(64 - static_cast<int>(effective_address() & 0177));
        select_alu_group(rau_multiplicative);
        break;
    case 036:
        shift_accumulator(
            static_cast<int>(effective_address() & 0177) - 64);
        break;
    case 037:
        alu_mode_ = static_cast<std::uint8_t>(effective_address() & 077);
        break;
    case 040: {
        const std::size_t target = effective_address() & 017;
        registers_[target] = accumulator_.address();
        registers_[0] = 0;
        break;
    }
    case 041: {
        const std::size_t target = effective_address() & 017;
        const std::uint16_t value = accumulator_.address();
        if (target != 017) {
            hardware_pop_acc();
        } else {
            accumulator_ = memory_[value];
        }
        registers_[target] = value;
        registers_[0] = 0;
        select_alu_group(rau_logical);
        break;
    }
    case 042:
        accumulator_ = Word48(registers_[effective_address() & 017]);
        select_alu_group(rau_logical);
        break;
    case 043:
        hardware_push_acc();
        accumulator_ = Word48(registers_[effective_address() & 017]);
        select_alu_group(rau_logical);
        break;
    case 044:
        registers_[instruction.address & 017] =
            registers_[instruction.reg];
        registers_[0] = 0;
        break;
    case 045: {
        const std::size_t target = instruction.address & 017;
        registers_[target] = address_add(
            registers_[target], registers_[instruction.reg]);
        registers_[0] = 0;
        break;
    }
    case 050:
    case 053:
    case 067:
    case 070:
    case 071:
    case 072:
    case 075: {
        const std::uint16_t address = effective_address();
        registers_[016] = address;
        try {
            switch (instruction.opcode) {
            case 050:
                if (address == 0200) {
                    accumulator_ = Word48();
                }
                break;
            case 053:
                if (address == 010) {
                    accumulator_ = Word48(0000000006025713ULL);
                }
                break;
            case 071:
                emulate_e71(address);
                break;
            case 067: {
                const std::uint16_t continuation =
                    static_cast<std::uint16_t>(
                        (memory_[address].raw() >> 24) & 077777);
                if (continuation == 0) {
                    throw MachineError(
                        "E67 control word has no continuation");
                }
                branch(continuation);
                break;
            }
            case 070:
            case 072:
                break;
            case 075:
                if (address != 0) {
                    memory_[address] = accumulator_;
                }
                break;
            default:
                break;
            }
        } catch (const E71InputRequired &) {
            program_counter_ = old_pc;
            right_half_ = old_right;
            instruction_modifier_ = applied_modifier;
            return ExecutionStatus::input_required;
        }
        select_alu_group(rau_logical);
        break;
    }
    case 0220:
        instruction_modifier_ = effective_address();
        break;
    case 0230:
        instruction_modifier_ = stack_operand().address();
        break;
    case 0240:
        registers_[instruction.reg] = instruction.address;
        registers_[0] = 0;
        break;
    case 0250:
        registers_[instruction.reg] = effective_address();
        registers_[0] = 0;
        break;
    case 0260:
        remainder_ = accumulator_;
        if (!logical_condition()) {
            branch(effective_address());
        }
        break;
    case 0270:
        remainder_ = accumulator_;
        if (logical_condition()) {
            branch(effective_address());
        }
        break;
    case 0300:
        branch(effective_address());
        break;
    case 0310:
        registers_[instruction.reg] = next_word;
        registers_[0] = 0;
        branch(instruction.address);
        break;
    case 0330:
        ++instruction_count_;
        return ExecutionStatus::halted;
    case 0340:
    case 0360:
        if (registers_[instruction.reg] == 0) {
            branch(instruction.address);
        }
        break;
    case 0350:
        if (registers_[instruction.reg] != 0) {
            branch(instruction.address);
        }
        break;
    case 0370:
        if (registers_[instruction.reg] != 0) {
            registers_[instruction.reg] = address_add(
                registers_[instruction.reg], 1);
            branch(instruction.address);
        }
        break;
    default:
        throw MachineError(unsupported_instruction(
            old_pc, instruction.opcode));
    }

    ++instruction_count_;
    return ExecutionStatus::running;
}

bool Machine::dispatch_translated_routine()
{
    // A translated routine owns its documented left-half entry only. A jump
    // to the right half of the same word is still an ordinary BESM transfer.
    if (right_half_) {
        return false;
    }

    std::uint16_t continuation = 0;
    switch (program_counter_) {
    case 02750: continuation = p02750_dispatch(); break;
    case 03014: continuation = p03014_dispatch_error(); break;
    case 03051: continuation = p03051_unpack_error(); break;
    case 03057: continuation = p03057_begin_error_format(); break;
    case 03072: continuation = p03072_resume_error_format(); break;
    case 03206: continuation = p03206_prepare_ordinary_call(); break;
    case 03235: continuation = p03235_bind_environment(); break;
    case 03261: continuation = p03261_enter_function(); break;
    case 03275:
        p03275_push_acc();
        continuation = registers_[015];
        break;
    case 03277:
        p03277_pop_acc();
        continuation = registers_[015];
        break;
    case 07475: continuation = p07475_cuchin(); break;
    case 11541: continuation = p11541_match_tagged_value(); break;
    case 15765: continuation = p15765_dispatch_special_function(); break;
    case 16313: continuation = p16313_begin_character_sequence(); break;
    case 16321: continuation = p16321_dispatch_character(); break;
    case 16325: continuation = p16325_continue_character_sequence(); break;
    case 16421: continuation = p16421_lookup_tagged_byte(); break;
    case 16457: continuation = p16457_shift_record(); break;
    case 16505: continuation = p16505_begin_record_shift(); break;
    case 16507: continuation = p16507_resume_record_shift(); break;
    case 20110: continuation = p20110_transfer_arguments(); break;
    case 20124: continuation = p20124_build_activation(); break;
    case 20245: continuation = p20245_begin_input_continue(); break;
    case 20252: continuation = p20252_query_console(); break;
    case 20253: continuation = p20253_resume_input_continue_status(); break;
    case 20256: continuation = p20256_transfer_console(); break;
    case 20257: continuation = p20257_finish_input_continue(); break;
    case 21255: continuation = p21255_begin_character_input(); break;
    case 21260: continuation = p21260_forward_converted_character(); break;
    case 21261: continuation = p21261_return_character(); break;
    case 21264: continuation = p21264_convert_character(); break;
    case 21274: continuation = p21274_decode_character(); break;
    case 21275: continuation = p21275_encode_character(); break;
    case 21431: continuation = p21431_buffer_char(); break;
    case 21443: continuation = p21443_advance_descriptor(); break;
    case 25346: continuation = p25346_begin_character_output(); break;
    case 25350: continuation = p25350_continue_character_output(); break;
    case 25361: continuation = p25361_resume_character_output(); break;
    case 25364: continuation = p25364_return_character_output(); break;
    default:
        return false;
    }

    program_counter_ = continuation;
    right_half_ = false;
    instruction_modifier_ = 0;
    ++translated_routine_count_;
    return true;
}

} // namespace poplan
