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

#include <chrono>
#include <ctime>
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

std::uint64_t jiffies_since_midnight()
{
    using namespace std::chrono;

    const system_clock::time_point now = system_clock::now();
    const system_clock::time_point whole_second =
        time_point_cast<seconds>(now);
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
    execution_started_at_ = std::chrono::steady_clock::now();
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
    case 074: {
        const std::uint16_t address = effective_address();
        registers_[016] = address;
        select_alu_group(rau_logical);
        if (address != 0) {
            throw MachineError(
                "E74 nonzero exit operation is unsupported");
        }
        ++instruction_count_;
        return ExecutionStatus::halted;
    }
    case 050:
    case 053:
    case 063:
    case 064:
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
                    accumulator_ = Word48(jiffies_since_midnight());
                }
                break;
            case 063:
                if (address == 04) {
                    const auto elapsed =
                        std::chrono::steady_clock::now()
                        - execution_started_at_;
                    accumulator_ = Word48(
                        std::chrono::duration_cast<
                            std::chrono::milliseconds>(elapsed).count()
                        / 20);
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
            case 064:
                // POPLAN only uses this formatted-output extracode as an
                // optional reporting path; the C++ console path may ignore it.
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
    for (const std::uint16_t address : disabled_translated_routines_) {
        if (program_counter_ == address) {
            return false;
        }
    }

    std::uint16_t continuation = 0;
    switch (program_counter_) {
    case 01107: continuation = p01107(); break;
    case 01140: continuation = p01140(); break;
    case 01151: continuation = p01151(); break;
    case 01167: continuation = p01167(); break;
    case 02750: continuation = p02750_dispatch(); break;
    case 02767: continuation = p02767(); break;
    case 02770: continuation = p02770(); break;
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
    case 03301: continuation = p03301(); break;
    case 03303: continuation = p03303_store_stack_top(); break;
    case 03413: continuation = p03413_numeric_update(); break;
    case 03516: continuation = p03516(); break;
    case 03536: continuation = p03536(); break;
    case 03716: continuation = p03716(); break;
    case 03724: continuation = p03724(); break;
    case 03725: continuation = p03725(); break;
    case 03726: continuation = p03726(); break;
    case 03736: continuation = p03736(); break;
    case 03741: continuation = p03741(); break;
    case 03742: continuation = p03742(); break;
    case 03743: continuation = p03743(); break;
    case 03750: continuation = p03750(); break;
    case 03751: continuation = p03751(); break;
    case 03754: continuation = p03754(); break;
    case 03757: continuation = p03757(); break;
    case 03761: continuation = p03761(); break;
    case 04074: continuation = p04074(); break;
    case 04076: continuation = p04076(); break;
    case 04100: continuation = p04100(); break;
    case 04101: continuation = p04101(); break;
    case 04102: continuation = p04102(); break;
    case 04103: continuation = p04103(); break;
    case 04322: continuation = p04322(); break;
    case 04330: continuation = p04330(); break;
    case 04334: continuation = p04334(); break;
    case 04335: continuation = p04335(); break;
    case 04343: continuation = p04343(); break;
    case 04350: continuation = p04350(); break;
    case 04351: continuation = p04351(); break;
    case 04352: continuation = p04352(); break;
    case 04353: continuation = p04353(); break;
    case 04354: continuation = p04354(); break;
    case 04426: continuation = p04426(); break;
    case 04447: continuation = p04447(); break;
    case 04455: continuation = p04455(); break;
    case 04467: continuation = p04467(); break;
    case 04471: continuation = p04471(); break;
    case 04503: continuation = p04503(); break;
    case 04504: continuation = p04504(); break;
    case 04665: continuation = p04665(); break;
    case 04667: continuation = p04667(); break;
    case 04670: continuation = p04670(); break;
    case 04673: continuation = p04673(); break;
    case 04675: continuation = p04675(); break;
    case 04677: continuation = p04677(); break;
    case 04702: continuation = p04702(); break;
    case 04706: continuation = p04706(); break;
    case 04707: continuation = p04707(); break;
    case 04710: continuation = p04710(); break;
    case 04711: continuation = p04711(); break;
    case 04715: continuation = p04715(); break;
    case 04717: continuation = p04717(); break;
    case 04721: continuation = p04721(); break;
    case 04722: continuation = p04722(); break;
    case 04725: continuation = p04725(); break;
    case 04726: continuation = p04726(); break;
    case 04730: continuation = p04730(); break;
    case 04731: continuation = p04731(); break;
    case 04734: continuation = p04734(); break;
    case 04736: continuation = p04736(); break;
    case 04737: continuation = p04737(); break;
    case 04740: continuation = p04740(); break;
    case 04742: continuation = p04742(); break;
    case 04746: continuation = p04746(); break;
    case 04750: continuation = p04750(); break;
    case 04751: continuation = p04751(); break;
    case 04753: continuation = p04753(); break;
    case 04755: continuation = p04755(); break;
    case 04756: continuation = p04756(); break;
    case 05207: continuation = p05207(); break;
    case 05211: continuation = p05211(); break;
    case 05215: continuation = p05215(); break;
    case 05221: continuation = p05221(); break;
    case 05430: continuation = p05430(); break;
    case 05433: continuation = p05433(); break;
    case 05434: continuation = p05434(); break;
    case 05435: continuation = p05435(); break;
    case 05436: continuation = p05436(); break;
    case 05440: continuation = p05440(); break;
    case 05441: continuation = p05441(); break;
    case 05447: continuation = p05447(); break;
    case 06343: continuation = p06343(); break;
    case 06346: continuation = p06346(); break;
    case 06354: continuation = p06354(); break;
    case 06372: continuation = p06372(); break;
    case 06374: continuation = p06374(); break;
    case 06401: continuation = p06401(); break;
    case 06526: continuation = p06526(); break;
    case 06534: continuation = p06534(); break;
    case 06536: continuation = p06536(); break;
    case 06545: continuation = p06545(); break;
    case 06556: continuation = p06556(); break;
    case 06560: continuation = p06560(); break;
    case 06561: continuation = p06561(); break;
    case 06623: continuation = p06623(); break;
    case 06631: continuation = p06631(); break;
    case 06637: continuation = p06637(); break;
    case 06645: continuation = p06645(); break;
    case 06650: continuation = p06650(); break;
    case 06657: continuation = p06657(); break;
    case 06660: continuation = p06660(); break;
    case 06661: continuation = p06661(); break;
    case 06712: continuation = p06712(); break;
    case 06733: continuation = p06733(); break;
    case 06734: continuation = p06734_error(); break;
    case 06735: continuation = p06735_add(); break;
    case 06740: continuation = p06740(); break;
    case 06741: continuation = p06741_error(); break;
    case 06742: continuation = p06742_subtract(); break;
    case 06744: continuation = p06744(); break;
    case 06745: continuation = p06745_error(); break;
    case 06746: continuation = p06746_multiply(); break;
    case 06750: continuation = p06750(); break;
    case 06751: continuation = p06751_error(); break;
    case 06752: continuation = p06752_divide(); break;
    case 07475: continuation = p07475_cuchin(); break;
    case 07673: continuation = p07673(); break;
    case 07704: continuation = p07704(); break;
    case 07706: continuation = p07706(); break;
    case 07707: continuation = p07707(); break;
    case 07710: continuation = p07710(); break;
    case 07712: continuation = p07712(); break;
    case 07715: continuation = p07715(); break;
    case 07716: continuation = p07716(); break;
    case 07720: continuation = p07720(); break;
    case 07722: continuation = p07722(); break;
    case 07724: continuation = p07724(); break;
    case 07725: continuation = p07725(); break;
    case 07727: continuation = p07727(); break;
    case 07737: continuation = p07737(); break;
    case 07742: continuation = p07742(); break;
    case 07751: continuation = p07751(); break;
    case 07752: continuation = p07752(); break;
    case 011464: continuation = p11464(); break;
    case 011471: continuation = p11471(); break;
    case 011500: continuation = p11500(); break;
    case 011536: continuation = p11536(); break;
    case 011541: continuation = p11541_match_tagged_value(); break;
    case 011673: continuation = p11673_begin_generated_update(); break;
    case 011675: continuation = p11675_continue_generated_update(); break;
    case 011701: continuation = p11701_match_generated_value(); break;
    case 011703: continuation = p11703_update_generated_value(); break;
    case 011710: continuation = p11710_finish_generated_update(); break;
    case 011717: continuation = p11717_begin_generated_binding(); break;
    case 011720: continuation = p11720_finish_generated_binding(); break;
    case 011726: continuation = p11726_begin_generated_rebinding(); break;
    case 011727: continuation = p11727_continue_generated_rebinding(); break;
    case 011731: continuation = p11731_finish_generated_rebinding(); break;
    case 015765: continuation = p15765_dispatch_special_function(); break;
    case 016254: continuation = p16254(); break;
    case 016313: continuation = p16313_begin_character_sequence(); break;
    case 016321: continuation = p16321_dispatch_character(); break;
    case 016325: continuation = p16325_continue_character_sequence(); break;
    case 016341: continuation = p16341(); break;
    case 016347: continuation = p16347(); break;
    case 016350: continuation = p16350(); break;
    case 016421: continuation = p16421_lookup_tagged_byte(); break;
    case 016457: continuation = p16457_shift_record(); break;
    case 016477: continuation = p16477(); break;
    case 016502: continuation = p16502(); break;
    case 016503: continuation = p16503(); break;
    case 016505: continuation = p16505_begin_record_shift(); break;
    case 016507: continuation = p16507_resume_record_shift(); break;
    case 016742: continuation = p16742(); break;
    case 016744: continuation = p16744(); break;
    case 017242: continuation = p17242(); break;
    case 017253: continuation = p17253(); break;
    case 017254: continuation = p17254_shared(); break;
    case 017260: continuation = p17260(); break;
    case 017266: continuation = p17266(); break;
    case 017275: continuation = p17275_shared(); break;
    case 017302: continuation = p17302(); break;
    case 017306: continuation = p17306(); break;
    case 017337: continuation = p17337(); break;
    case 017340: continuation = p17340(); break;
    case 017341: continuation = p17341(); break;
    case 017342: continuation = p17342(); break;
    case 017417: continuation = p17417(); break;
    case 017423: continuation = p17423(); break;
    case 017424: continuation = p17424(); break;
    case 017425: continuation = p17425(); break;
    case 017426: continuation = p17426(); break;
    case 017430: continuation = p17430(); break;
    case 017432: continuation = p17432(); break;
    case 017435: continuation = p17435(); break;
    case 017436: continuation = p17436(); break;
    case 017440: continuation = p17440(); break;
    case 017442: continuation = p17442(); break;
    case 017451: continuation = p17451(); break;
    case 017452: continuation = p17452(); break;
    case 017453: continuation = p17453(); break;
    case 017454: continuation = p17454(); break;
    case 017461: continuation = p17461(); break;
    case 017762: continuation = p17762(); break;
    case 017764: continuation = p17764(); break;
    case 017774: continuation = p17774(); break;
    case 020002: continuation = p20002(); break;
    case 017013: continuation = p17013(); break;
    case 017015: continuation = p17015(); break;
    case 017021: continuation = p17021(); break;
    case 017023: continuation = p17023(); break;
    case 017045: continuation = p17045(); break;
    case 017047: continuation = p17047(); break;
    case 020077: continuation = p20077(); break;
    case 020101: continuation = p20101(); break;
    case 020110: continuation = p20110_transfer_arguments(); break;
    case 020124: continuation = p20124_build_activation(); break;
    case 020170: continuation = p20170_input_primary(); break;
    case 020175: continuation = p20175_resume_input_primary(); break;
    case 020177: continuation = p20177_continue_input_primary(); break;
    case 020201: continuation = p20201_resume_input_status(); break;
    case 020202: continuation = p20202_prepare_input_transfer(); break;
    case 020206: continuation = p20206_resume_input_transfer(); break;
    case 020210: continuation = p20210_finish_input_primary(); break;
    case 020245: continuation = p20245_begin_input_continue(); break;
    case 020252: continuation = p20252_query_console(); break;
    case 020253: continuation = p20253_resume_input_continue_status(); break;
    case 020256: continuation = p20256_transfer_console(); break;
    case 020257: continuation = p20257_finish_input_continue(); break;
    case 020673: continuation = p20673_return(); break;
    case 021251: continuation = p21251_extract_character(); break;
    case 021253: continuation = p21253_resume_character_extract(); break;
    case 021255: continuation = p21255_begin_character_input(); break;
    case 021260: continuation = p21260_forward_converted_character(); break;
    case 021261: continuation = p21261_return_character(); break;
    case 021264: continuation = p21264_convert_character(); break;
    case 021274: continuation = p21274_decode_character(); break;
    case 021275: continuation = p21275_encode_character(); break;
    case 021431: continuation = p21431_buffer_char(); break;
    case 021443: continuation = p21443_advance_descriptor(); break;
    case 021464: continuation = p21464(); break;
    case 021473: continuation = p21473(); break;
    case 021476: continuation = p21476(); break;
    case 021501: continuation = p21501(); break;
    case 021502: continuation = p21502(); break;
    case 021510: continuation = p21510(); break;
    case 021511: continuation = p21511(); break;
    case 021516: continuation = p21516(); break;
    case 025346: continuation = p25346_begin_character_output(); break;
    case 025350: continuation = p25350_continue_character_output(); break;
    case 025356: continuation = p25356_emit_end_character(); break;
    case 025361: continuation = p25361_resume_character_output(); break;
    case 025364: continuation = p25364_return_character_output(); break;
    case 025370: continuation = p25370_begin_token_source(); break;
    case 025373: continuation = p25373_resume_token_source(); break;
    case 025376: continuation = p25376_fetch_token_character(); break;
    case 025377: continuation = p25377_finish_token_source(); break;
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
