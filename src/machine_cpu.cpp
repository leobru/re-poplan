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
    semantic_halted_ = false;
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
    if (translated_routines_enabled_) {
        try {
            if (dispatch_translated_routine()) {
                ++instruction_count_;
                return semantic_halted_
                    ? ExecutionStatus::halted : ExecutionStatus::running;
            }
        } catch (const E71InputRequired &) {
            // A blocking translated I/O routine remains at its left-half
            // entry and is retried as a semantic unit after input arrives.
            ++instruction_count_;
            ++translated_routine_count_;
            return ExecutionStatus::input_required;
        }
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
                           register_value(instruction.reg));
    };
    const auto stack_operand = [&]() {
        if (instruction.address == 0 && instruction.reg == 017) {
            registers_[017] = address_add(registers_[017], -1);
        }
        return memory_[effective_address()];
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
        set_register(target, accumulator_.address());
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
        set_register(target, value);
        select_alu_group(rau_logical);
        break;
    }
    case 042:
        accumulator_ = Word48(register_value(effective_address() & 017));
        select_alu_group(rau_logical);
        break;
    case 043:
        hardware_push_acc();
        accumulator_ = Word48(register_value(effective_address() & 017));
        select_alu_group(rau_logical);
        break;
    case 044:
        set_register(instruction.address & 017,
                     register_value(instruction.reg));
        break;
    case 045: {
        const std::size_t target = instruction.address & 017;
        set_register(target, address_add(
            register_value(target), register_value(instruction.reg)));
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
                if (address <= 6) {
                    elementary_function(address);
                } else if (address == 0200) {
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
        set_register(instruction.reg, instruction.address);
        break;
    case 0250:
        set_register(instruction.reg, effective_address());
        break;
    case 0260:
        remainder_ = accumulator_;
        if (!accumulator_condition()) {
            branch(effective_address());
        }
        break;
    case 0270:
        remainder_ = accumulator_;
        if (accumulator_condition()) {
            branch(effective_address());
        }
        break;
    case 0300:
        branch(effective_address());
        break;
    case 0310:
        set_register(instruction.reg, next_word);
        branch(instruction.address);
        break;
    case 0330:
        ++instruction_count_;
        return ExecutionStatus::halted;
    case 0340:
    case 0360:
        if (register_value(instruction.reg) == 0) {
            branch(instruction.address);
        }
        break;
    case 0350:
        if (register_value(instruction.reg) != 0) {
            branch(instruction.address);
        }
        break;
    case 0370:
        if (register_value(instruction.reg) != 0) {
            set_register(instruction.reg, address_add(
                register_value(instruction.reg), 1));
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

bool Machine::execute_generated_routine(std::uint16_t &continuation)
{
    // Generated POP-2 routines are straight-line call glue. Preflight the
    // current words so an unfamiliar generated instruction retains the
    // ordinary per-instruction fallback without partially changing state.
    std::uint16_t scan_pc = program_counter_;
    bool scan_right = false;
    bool terminated = false;
    for (std::size_t halves = 0; halves != core_words * 2; ++halves) {
        if (scan_pc < 036000) {
            return false;
        }
        const std::uint64_t word = memory_[scan_pc].raw();
        const std::uint32_t half = static_cast<std::uint32_t>(
            scan_right ? word & 077777777ULL : word >> 24);
        const std::uint16_t opcode = decode_instruction(half).opcode;
        switch (opcode) {
        case 001:  // STX
        case 003:  // XTS
        case 0220: // UTC
        case 0240: // VTM
            break;
        case 0300: // UJ
        case 0310: // VJM
            terminated = true;
            break;
        default:
            return false;
        }
        if (terminated) {
            break;
        }
        if (scan_right) {
            scan_pc = address_add(scan_pc, 1);
            scan_right = false;
        } else {
            scan_right = true;
        }
    }
    if (!terminated) {
        return false;
    }

    std::uint16_t pc = program_counter_;
    bool right = false;
    std::uint16_t modifier = instruction_modifier_;
    for (;;) {
        const std::uint64_t word = memory_[pc].raw();
        const std::uint32_t half = static_cast<std::uint32_t>(
            right ? word & 077777777ULL : word >> 24);
        Instruction instruction = decode_instruction(half);
        const std::uint16_t next_word = address_add(pc, 1);

        if (right) {
            pc = next_word;
            right = false;
        } else {
            right = true;
        }
        instruction.address = address_add(instruction.address, modifier);
        modifier = 0;

        const auto effective_address = [&]() {
            return address_add(
                instruction.address, register_value(instruction.reg));
        };
        switch (instruction.opcode) {
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
        case 0220:
            modifier = effective_address();
            break;
        case 0240:
            set_register(instruction.reg, instruction.address);
            break;
        case 0300:
            continuation = effective_address();
            instruction_modifier_ = 0;
            return true;
        case 0310:
            set_register(instruction.reg, next_word);
            continuation = instruction.address;
            instruction_modifier_ = 0;
            return true;
        default:
            // The preflight above guarantees this is unreachable unless a
            // generated STX rewrites a later instruction in the same call.
            throw MachineError(
                "generated POP-2 routine rewrote itself while executing");
        }
    }
}

bool Machine::dispatch_translated_routine()
{
    // BESM transfers always enter the left instruction of a word. A right
    // instruction is reached only by sequential execution of the left one,
    // so it can never be a semantic routine-dispatch entry.
    if (right_half_) {
        return false;
    }
    for (const std::uint16_t address : disabled_translated_routines_) {
        if (program_counter_ == address) {
            return false;
        }
    }

    std::uint16_t continuation = 0;
    if (program_counter_ >= 036000) {
        if (!execute_generated_routine(continuation)) {
            return false;
        }
        program_counter_ = continuation;
        right_half_ = false;
        instruction_modifier_ = 0;
        ++translated_routine_count_;
        return true;
    }

    switch (program_counter_) {
    case 01000: continuation = p01000(); break;
    case 01002: continuation = p01002(); break;
    case 01004: continuation = p01004(); break;
    case 01006: continuation = p01006(); break;
    case 01011: continuation = p01011(); break;
    case 01012: continuation = p01012(); break;
    case 01016: continuation = p01016(); break;
    case 01023: continuation = p01023(); break;
    case 01024: continuation = p01024(); break;
    case 01025: continuation = p01025(); break;
    case 01026: continuation = p01026(); break;
    case 01107: continuation = p01107(); break;
    case 01140: continuation = p01140(); break;
    case 01151: continuation = p01151(); break;
    case 01167: continuation = p01167(); break;
    case 02750: continuation = p02750_dispatch(); break;
    case 02764: continuation = p02764(); break;
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
    case 03305: continuation = p03305(); break;
    case 03310: continuation = p03310(); break;
    case 03311: continuation = p03311(); break;
    case 03314: continuation = p03314(); break;
    case 03330: continuation = p03330(); break;
    case 03336: continuation = p03336(); break;
    case 03337: continuation = p03337(); break;
    case 03371: continuation = p03371(); break;
    case 03372: continuation = p03372(); break;
    case 03477: continuation = p03477(); break;
    case 03424: continuation = p03424(); break;
    case 03425: continuation = p03425(); break;
    case 03426: continuation = p03426(); break;
    case 03435: continuation = p03435(); break;
    case 03436: continuation = p03436(); break;
    case 03442: continuation = p03442(); break;
    case 03443: continuation = p03443(); break;
    case 03447: continuation = p03447(); break;
    case 03340: continuation = p03340(); break;
    case 03341: continuation = p03341(); break;
    case 03342: continuation = p03342(); break;
    case 03374: continuation = p03374(); break;
    case 03375: continuation = p03375(); break;
    case 03376: continuation = p03376(); break;
    case 03413: continuation = p03413_numeric_update(); break;
    case 03461: continuation = p03461(); break;
    case 03466: continuation = p03466(); break;
    case 03467: continuation = p03467(); break;
    case 03473: continuation = p03473(); break;
    case 03474: continuation = p03474(); break;
    case 03475: continuation = p03475(); break;
    case 03506: continuation = p03506(); break;
    case 03516: continuation = p03516(); break;
    case 03521: continuation = p03521(); break;
    case 03524: continuation = p03524(); break;
    case 03526: continuation = p03526(); break;
    case 03530: continuation = p03530(); break;
    case 03531: continuation = p03531(); break;
    case 03532: continuation = p03532(); break;
    case 03534: continuation = p03534(); break;
    case 03536: continuation = p03536(); break;
    case 03542: continuation = p03542(); break;
    case 03544:
    case 03545:
    case 03546:
    case 03547:
    case 03550:
    case 03554:
    case 03555:
    case 03557:
    case 03561:
    case 03562:
    case 03564:
    case 03570:
    case 03573:
    case 03575:
    case 03577:
    case 03602:
    case 03603:
    case 03606:
    case 03610:
    case 03611:
    case 03612:
    case 03614:
    case 03615:
    case 03617:
    case 03622:
    case 03623:
    case 03624:
    case 03625:
    case 03627:
        continuation = p03544(program_counter_);
        break;
    case 03631: continuation = p03631(); break;
    case 03632: continuation = p03632(); break;
    case 03702: continuation = p03702(); break;
    case 03704: continuation = p03704(); break;
    case 03706: continuation = p03706(); break;
    case 03707: continuation = p03707(); break;
    case 03710: continuation = p03710(); break;
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
    case 03712: continuation = p03712(); break;
    case 03714: continuation = p03714(); break;
    case 03762: continuation = p03762(); break;
    case 03763: continuation = p03763(); break;
    case 03765: continuation = p03765(); break;
    case 03770: continuation = p03770(); break;
    case 03773: continuation = p03773(); break;
    case 03774: continuation = p03774(); break;
    case 03775: continuation = p03775(); break;
    case 03777: continuation = p03777(); break;
    case 04001:
    case 04002:
    case 04003:
    case 04004:
    case 04006:
    case 04007:
    case 04010:
    case 04012:
    case 04016:
    case 04020:
    case 04024:
    case 04026:
    case 04032:
    case 04040:
    case 04041:
    case 04044:
    case 04045:
    case 04050:
    case 04052:
    case 04053:
    case 04054:
    case 04056:
    case 04060:
    case 04061:
    case 04062:
    case 04063:
    case 04070:
    case 04111:
    case 04115:
        continuation = p04001(program_counter_);
        break;
    case 04074: continuation = p04074(); break;
    case 04076: continuation = p04076(); break;
    case 04100: continuation = p04100(); break;
    case 04101: continuation = p04101(); break;
    case 04102: continuation = p04102(); break;
    case 04103: continuation = p04103(); break;
    case 04117: continuation = p04117(); break;
    case 04121: continuation = p04121(); break;
    case 04123: continuation = p04123(); break;
    case 04125: continuation = p04125(); break;
    case 04126: continuation = p04126(); break;
    case 04127: continuation = p04127(); break;
    case 04131: continuation = p04131(); break;
    case 04134: continuation = p04134(); break;
    case 04135: continuation = p04135(); break;
    case 04137: continuation = p04137(); break;
    case 04142: continuation = p04142(); break;
    case 04144: continuation = p04144(); break;
    case 04147: continuation = p04147(); break;
    case 04154: continuation = p04154(); break;
    case 04155: continuation = p04155(); break;
    case 04156: continuation = p04156(); break;
    case 04157: continuation = p04157(); break;
    case 04312: continuation = p04312(); break;
    case 04315: continuation = p04315(); break;
    case 04316: continuation = p04316(); break;
    case 04161: continuation = p04161(); break;
    case 04164: continuation = p04164(); break;
    case 04166: continuation = p04166(); break;
    case 04167: continuation = p04167(); break;
    case 04173: continuation = p04173(); break;
    case 04175: continuation = p04175(); break;
    case 04177: continuation = p04177(); break;
    case 04200: continuation = p04200(); break;
    case 04201: continuation = p04201(); break;
    case 04202: continuation = p04202(); break;
    case 04210: continuation = p04210(); break;
    case 04212: continuation = p04212(); break;
    case 04213: continuation = p04213(); break;
    case 04227: continuation = p04227(); break;
    case 04214:
    case 04220:
    case 04222:
    case 04224:
    case 04226:
    case 04230:
    case 04231:
    case 04235:
    case 04236:
    case 04240:
    case 04243:
    case 04246:
    case 04251:
    case 04253:
    case 04255:
    case 04256:
    case 04257:
    case 04263:
    case 04265:
    case 04270:
    case 04271:
    case 04272:
    case 04274:
    case 04276:
    case 04300:
    case 04303:
    case 04304:
    case 04306:
        continuation = p04214(program_counter_);
        break;
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
    case 04437: continuation = p04437(); break;
    case 04440: continuation = p04440(); break;
    case 04442: continuation = p04442(); break;
    case 04443: continuation = p04443(); break;
    case 04447: continuation = p04447(); break;
    case 04455: continuation = p04455(); break;
    case 04467: continuation = p04467(); break;
    case 04471: continuation = p04471(); break;
    case 04503: continuation = p04503(); break;
    case 04504: continuation = p04504(); break;
    case 04507: continuation = p04507(); break;
    case 04513: continuation = p04513(); break;
    case 04536: continuation = p04536(); break;
    case 04541: continuation = p04541(); break;
    case 04544: continuation = p04544(); break;
    case 04561: continuation = p04561(); break;
    case 04571: continuation = p04571(); break;
    case 04572: continuation = p04572(); break;
    case 04576: continuation = p04576(); break;
    case 04577: continuation = p04577(); break;
    case 04600: continuation = p04600(); break;
    case 04601: continuation = p04601(); break;
    case 04602: continuation = p04602(); break;
    case 04605: continuation = p04605(); break;
    case 04607: continuation = p04607(); break;
    case 04610: continuation = p04610(); break;
    case 04611: continuation = p04611(); break;
    case 04612: continuation = p04612(); break;
    case 04614: continuation = p04614(); break;
    case 04615: continuation = p04615(); break;
    case 04623: continuation = p04623(); break;
    case 04624: continuation = p04624(); break;
    case 04625: continuation = p04625(); break;
    case 04626: continuation = p04626(); break;
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
    case 05007: continuation = p05007(); break;
    case 05014: continuation = p05014(); break;
    case 05017: continuation = p05017(); break;
    case 05021: continuation = p05021(); break;
    case 05022: continuation = p05022(); break;
    case 05026: continuation = p05026(); break;
    case 05030: continuation = p05030(); break;
    case 05034: continuation = p05034(); break;
    case 05035: continuation = p05035(); break;
    case 05040: continuation = p05040(); break;
    case 05045: continuation = p05045(); break;
    case 05052:
    case 05053:
    case 05054:
    case 05055:
    case 05056:
    case 05057:
    case 05060:
    case 05061:
    case 05062:
    case 05063:
    case 05064:
    case 05065:
    case 05066:
    case 05067:
    case 05070:
    case 05071:
    case 05072:
    case 05073:
    case 05076:
        continuation = p05052_dispatch_generated_instruction(
            program_counter_);
        break;
    case 05124: continuation = p05124_store_generated_instruction(); break;
    case 05125: continuation = p05125_advance_generated_instruction(); break;
    case 05126: continuation = p05126_load_generated_instruction(); break;
    case 05127: continuation = p05127_mask_generated_instruction(); break;
    case 05130: continuation = p05130_finish_generated_instruction(); break;
    case 05131: continuation = p05131_pack_generated_instruction(); break;
    case 05135: continuation = p05135_add_generated_instruction(); break;
    case 05143: continuation = p05143_begin_empty_generated_instruction(); break;
    case 05147: continuation = p05147_finish_empty_generated_instruction(); break;
    case 05151: continuation = p05151_restore_generated_instruction(); break;
    case 05160: continuation = p05160(); break;
    case 05163: continuation = p05163(); break;
    case 05207: continuation = p05207(); break;
    case 05211: continuation = p05211(); break;
    case 05213: continuation = p05213(); break;
    case 05215: continuation = p05215(); break;
    case 05217: continuation = p05217(); break;
    case 05221: continuation = p05221(); break;
    case 05230: continuation = p05230(); break;
    case 05240: continuation = p05240(); break;
    case 05250: continuation = p05250(); break;
    case 05252: continuation = p05252(); break;
    case 05254: continuation = p05254(); break;
    case 05255: continuation = p05255(); break;
    case 05261: continuation = p05261(); break;
    case 05266: continuation = p05266(); break;
    case 05273: continuation = p05273(); break;
    case 05314: continuation = p05314(); break;
    case 05316: continuation = p05316(); break;
    case 05326: continuation = p05326(); break;
    case 05334: continuation = p05334(); break;
    case 05335: continuation = p05335(); break;
    case 05336: continuation = p05336(); break;
    case 05337: continuation = p05337(); break;
    case 05341: continuation = p05341(); break;
    case 05405: continuation = p05405(); break;
    case 05410: continuation = p05410(); break;
    case 05414: continuation = p05414(); break;
    case 05416: continuation = p05416(); break;
    case 05422: continuation = p05422(); break;
    case 05425: continuation = p05425(); break;
    case 05427: continuation = p05427(); break;
    case 05430: continuation = p05430(); break;
    case 05433: continuation = p05433(); break;
    case 05434: continuation = p05434(); break;
    case 05435: continuation = p05435(); break;
    case 05436: continuation = p05436(); break;
    case 05440: continuation = p05440(); break;
    case 05441: continuation = p05441(); break;
    case 05447: continuation = p05447(); break;
    case 06134: continuation = p06134(); break;
    case 06141: continuation = p06141(); break;
    case 06343: continuation = p06343(); break;
    case 06346: continuation = p06346(); break;
    case 06354: continuation = p06354(); break;
    case 06372: continuation = p06372(); break;
    case 06374: continuation = p06374(); break;
    case 06401: continuation = p06401(); break;
    case 06424: continuation = p06424(); break;
    case 06435: continuation = p06435(); break;
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
    case 06634: continuation = p06634(); break;
    case 06635: continuation = p06635(); break;
    case 06636: continuation = p06636(); break;
    case 06672:
    case 06673:
    case 06674:
    case 06675:
    case 06676:
    case 06677:
    case 06700:
    case 06702:
    case 06703:
    case 06704:
    case 06706:
    case 06707:
    case 06710:
        continuation = p06672(program_counter_);
        break;
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
    case 06757: continuation = p06757(); break;
    case 06761: continuation = p06761(); break;
    case 06763: continuation = p06763(); break;
    case 06765: continuation = p06765(); break;
    case 06767: continuation = p06767(); break;
    case 06771: continuation = p06771(); break;
    case 07005: continuation = p07005(); break;
    case 07011: continuation = p07011(); break;
    case 07013: continuation = p07013(); break;
    case 07022: continuation = p07022(); break;
    case 07023: continuation = p07023(); break;
    case 07024: continuation = p07024(); break;
    case 07033: continuation = p07033(); break;
    case 07042: continuation = p07042(); break;
    case 07043: continuation = p07043(); break;
    case 07165: continuation = p07165(); break;
    case 07166: continuation = p07166(); break;
    case 07173: continuation = p07173(); break;
    case 07177: continuation = p07177(); break;
    case 07207: continuation = p07207(); break;
    case 07256: continuation = p07256(); break;
    case 07262: continuation = p07262(); break;
    case 07264: continuation = p07264(); break;
    case 07312: continuation = p07312(); break;
    case 07331: continuation = p07331(); break;
    case 07332: continuation = p07332(); break;
    case 07333: continuation = p07333(); break;
    case 07142: continuation = p07142(); break;
    case 07143: continuation = p07143(); break;
    case 06751: continuation = p06751_error(); break;
    case 06752: continuation = p06752_divide(); break;
    case 06410: continuation = p06410(); break;
    case 06414: continuation = p06414(); break;
    case 06415: continuation = p06415(); break;
    case 06417: continuation = p06417(); break;
    case 07472: continuation = p07472(); break;
    case 07473: continuation = p07473(); break;
    case 07475: continuation = p07475_cuchin(); break;
    case 07533: continuation = p07533(); break;
    case 07536: continuation = p07536(); break;
    case 07652: continuation = p07652(); break;
    case 07661: continuation = p07661(); break;
    case 07663: continuation = p07663(); break;
    case 07664: continuation = p07664(); break;
    case 07667: continuation = p07667(); break;
    case 07670: continuation = p07670(); break;
    case 07672: continuation = p07672(); break;
    case 07673: continuation = p07673(); break;
    case 07601: continuation = p07601(); break;
    case 07603: continuation = p07603(); break;
    case 07604: continuation = p07604(); break;
    case 07605: continuation = p07605(); break;
    case 07611: continuation = p07611(); break;
    case 07613: continuation = p07613(); break;
    case 07614: continuation = p07614(); break;
    case 07615: continuation = p07615(); break;
    case 07616: continuation = p07616(); break;
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
    case 07761: continuation = p07761(); break;
    case 07764: continuation = p07764(); break;
    case 07766: continuation = p07766(); break;
    case 07767: continuation = p07767(); break;
    case 07770: continuation = p07770(); break;
    case 07771: continuation = p07771(); break;
    case 07773:
        if (!native_character_output_active()) {
            return false;
        }
        continuation = p07773_prstri();
        break;
    case 010232: continuation = p10232(); break;
    case 010217: continuation = p10217(); break;
    case 010220: continuation = p10220(); break;
    case 010222: continuation = p10222(); break;
    case 010224: continuation = p10224(); break;
    case 010353: continuation = p10353(); break;
    case 010357: continuation = p10357(); break;
    case 010361: continuation = p10361(); break;
    case 010363: continuation = p10363(); break;
    case 010725: continuation = p10725(); break;
    case 010727: continuation = p10727(); break;
    case 010733: continuation = p10733(); break;
    case 010737: continuation = p10737(); break;
    case 010742: continuation = p10742(); break;
    case 010743: continuation = p10743(); break;
    case 010744: continuation = p10744(); break;
    case 010745: continuation = p10745(); break;
    case 010746: continuation = p10746(); break;
    case 010747: continuation = p10747(); break;
    case 010751: continuation = p10751(); break;
    case 010752: continuation = p10752(); break;
    case 010755: continuation = p10755(); break;
    case 010756: continuation = p10756(); break;
    case 011503: continuation = p11503(); break;
    case 011506: continuation = p11506(); break;
    case 011507: continuation = p11507(); break;
    case 011531: continuation = p11531(); break;
    case 011532: continuation = p11532(); break;
    case 011533: continuation = p11533(); break;
    case 011534: continuation = p11534(); break;
    case 011535: continuation = p11535(); break;
    case 011053: continuation = p11053(); break;
    case 011054: continuation = p11054(); break;
    case 011060: continuation = p11060(); break;
    case 06132: continuation = p06132(); break;
    case 06133: continuation = p06133(); break;
    case 011432: continuation = p11432(); break;
    case 011435: continuation = p11435(); break;
    case 011436: continuation = p11436(); break;
    case 011440: continuation = p11440(); break;
    case 011441: continuation = p11441(); break;
    case 011464: continuation = p11464(); break;
    case 012125: continuation = p12125(); break;
    case 012130: continuation = p12130(); break;
    case 012131: continuation = p12131(); break;
    case 012133: continuation = p12133(); break;
    case 012134: continuation = p12134(); break;
    case 012135: continuation = p12135(); break;
    case 012136: continuation = p12136(); break;
    case 012137: continuation = p12137(); break;
    case 012141: continuation = p12141(); break;
    case 012142: continuation = p12142(); break;
    case 012143: continuation = p12143(); break;
    case 012145: continuation = p12145(); break;
    case 012147: continuation = p12147(); break;
    case 012151: continuation = p12151(); break;
    case 012152: continuation = p12152(); break;
    case 012153: continuation = p12153(); break;
    case 012154: continuation = p12154(); break;
    case 012156: continuation = p12156(); break;
    case 012246: continuation = p12246(); break;
    case 012251: continuation = p12251(); break;
    case 012252: continuation = p12252(); break;
    case 012253: continuation = p12253(); break;
    case 012257: continuation = p12257(); break;
    case 012266: continuation = p12266(); break;
    case 012270: continuation = p12270(); break;
    case 012271: continuation = p12271(); break;
    case 011471: continuation = p11471(); break;
    case 011500: continuation = p11500(); break;
    case 011514: continuation = p11514(); break;
    case 011524: continuation = p11524(); break;
    case 011525: continuation = p11525(); break;
    case 011526: continuation = p11526(); break;
    case 011527: continuation = p11527(); break;
    case 011530: continuation = p11530(); break;
    case 011536: continuation = p11536(); break;
    case 011541: continuation = p11541_match_tagged_value(); break;
    case 011647: continuation = p11647(); break;
    case 011651: continuation = p11651(); break;
    case 011665: continuation = p11665(); break;
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
    case 011746: continuation = p11746(); break;
    case 011755: continuation = p11755_begin_generated_binding(); break;
    case 011756: continuation = p11756_begin_generated_rebinding(); break;
    case 011757: continuation = p11757_enter_generated_rebinding(); break;
    case 012036: continuation = p12036(); break;
    case 012042: continuation = p12042(); break;
    case 012043: continuation = p12043(); break;
    case 012674: continuation = p12674_prreal(); break;
    case 013007: continuation = p13007(); break;
    case 013013: continuation = p13013(); break;
    case 013014: continuation = p13014(); break;
    case 013015: continuation = p13015(); break;
    case 013016: continuation = p13016(); break;
    case 013017: continuation = p13017(); break;
    case 013031: continuation = p13031(); break;
    case 013041: continuation = p13041(); break;
    case 013043: continuation = p13043(); break;
    case 013044: continuation = p13044(); break;
    case 013046: continuation = p13046(); break;
    case 013047: continuation = p13047(); break;
    case 013057: continuation = p13057(); break;
    case 013062: continuation = p13062(); break;
    case 013063: continuation = p13063(); break;
    case 013066: continuation = p13066(); break;
    case 013071: continuation = p13071(); break;
    case 013072: continuation = p13072(); break;
    case 013076: continuation = p13076(); break;
    case 013111: continuation = p13111(); break;
    case 013113: continuation = p13113(); break;
    case 013115: continuation = p13115(); break;
    case 013117: continuation = p13117(); break;
    case 013121: continuation = p13121(); break;
    case 013125: continuation = p13125(); break;
    case 013127: continuation = p13127(); break;
    case 013130: continuation = p13130(); break;
    case 013172: continuation = p13172(); break;
    case 013207: continuation = p13207(); break;
    case 013216: continuation = p13216(); break;
    case 013217: continuation = p13217(); break;
    case 013362: continuation = p13362(); break;
    case 013367: continuation = p13367(); break;
    case 013370: continuation = p13370(); break;
    case 013375: continuation = p13375(); break;
    case 013376: continuation = p13376(); break;
    case 013411: continuation = p13411(); break;
    case 013414: continuation = p13414(); break;
    case 013421: continuation = p13421(); break;
    case 013422: continuation = p13422(); break;
    case 013424: continuation = p13424(); break;
    case 013426: continuation = p13426(); break;
    case 013430: continuation = p13430(); break;
    case 013454: continuation = p13454(); break;
    case 013460: continuation = p13460(); break;
    case 013461: continuation = p13461(); break;
    case 013462: continuation = p13462(); break;
    case 013463: continuation = p13463(); break;
    case 013464: continuation = p13464(); break;
    case 013465: continuation = p13465(); break;
    case 013466: continuation = p13466(); break;
    case 013470: continuation = p13470(); break;
    case 013471: continuation = p13471(); break;
    case 013472: continuation = p13472(); break;
    case 013474: continuation = p13474(); break;
    case 013476: continuation = p13476(); break;
    case 013500: continuation = p13500(); break;
    case 013501: continuation = p13501(); break;
    case 013502: continuation = p13502(); break;
    case 015322: continuation = p15322(); break;
    case 015323: continuation = p15323(); break;
    case 015324: continuation = p15324(); break;
    case 015325: continuation = p15325(); break;
    case 015330: continuation = p15330(); break;
    case 015332: continuation = p15332(); break;
    case 015335: continuation = p15335(); break;
    case 015336: continuation = p15336(); break;
    case 015341: continuation = p15341(); break;
    case 015342: continuation = p15342(); break;
    case 015344: continuation = p15344(); break;
    case 015345: continuation = p15345(); break;
    case 015765: continuation = p15765_dispatch_special_function(); break;
    case 016005: continuation = p16005(); break;
    case 016022: continuation = p16022(); break;
    case 016026: continuation = p16026(); break;
    case 016076: continuation = p16076(); break;
    case 016104: continuation = p16104(); break;
    case 016106: continuation = p16106(); break;
    case 016110: continuation = p16110(); break;
    case 016145: continuation = p16145(); break;
    case 016151: continuation = p16151(); break;
    case 016153: continuation = p16153(); break;
    case 016157: continuation = p16157(); break;
    case 016161: continuation = p16161(); break;
    case 016166: continuation = p16166(); break;
    case 016171: continuation = p16171(); break;
    case 016201: continuation = p16201(); break;
    case 016207: continuation = p16207(); break;
    case 016210: continuation = p16210(); break;
    case 016215: continuation = p16215(); break;
    case 016216: continuation = p16216(); break;
    case 016222: continuation = p16222(); break;
    case 016254: continuation = p16254(); break;
    case 016313: continuation = p16313_begin_character_sequence(); break;
    case 016320: continuation = p16320(); break;
    case 016321: continuation = p16321_dispatch_character(); break;
    case 016325: continuation = p16325_continue_character_sequence(); break;
    case 016341: continuation = p16341(); break;
    case 016346: continuation = p16346(); break;
    case 016347: continuation = p16347(); break;
    case 016350: continuation = p16350(); break;
    case 016351:
    case 016352:
    case 016353:
    case 016354:
    case 016355:
    case 016356:
    case 016357:
    case 016360:
    case 016361:
    case 016362:
    case 016363:
    case 016364:
    case 016365:
    case 016366:
        continuation = p16351_dispatch_lookup_result(program_counter_);
        break;
    case 016367: continuation = p16367_initialize_record_tables(); break;
    case 016370: continuation = p16370_clear_record_table(); break;
    case 016372: continuation = p16372_begin_record_shift(); break;
    case 016373: continuation = p16373_push_record_head(); break;
    case 016374: continuation = p16374_push_record_value(); break;
    case 016375: continuation = p16375_push_alternate_record_value(); break;
    case 016376: continuation = p16376(); break;
    case 016402: continuation = p16402_select_nonempty_record(); break;
    case 016403: continuation = p16403_select_empty_record(); break;
    case 016404: continuation = p16404_prepare_record_evaluation(); break;
    case 016405: continuation = p16405_enter_record_evaluation(); break;
    case 016406: continuation = p16406(); break;
    case 016412: continuation = p16412(); break;
    case 016413: continuation = p16413(); break;
    case 016415: continuation = p16415(); break;
    case 016416: continuation = p16416(); break;
    case 016417: continuation = p16417(); break;
    case 016420: continuation = p16420(); break;
    case 016421: continuation = p16421_lookup_tagged_byte(); break;
    case 016457: continuation = p16457_shift_record(); break;
    case 016463: continuation = p16463_begin_record_evaluation(); break;
    case 016465: continuation = p16465_store_record_evaluation(); break;
    case 016466: continuation = p16466_return_record_evaluation(); break;
    case 016467: continuation = p16467(); break;
    case 016471: continuation = p16471(); break;
    case 016477: continuation = p16477(); break;
    case 016502: continuation = p16502(); break;
    case 016503: continuation = p16503(); break;
    case 016513: continuation = p16513(); break;
    case 016517: continuation = p16517(); break;
    case 016522: continuation = p16522(); break;
    case 016524: continuation = p16524(); break;
    case 016515: continuation = p16515(); break;
    case 016530: continuation = p16530(); break;
    case 016505: continuation = p16505_begin_record_shift(); break;
    case 016507: continuation = p16507_resume_record_shift(); break;
    case 016511: continuation = p16511(); break;
    case 016531: continuation = p16531_continue_record_shift(); break;
    case 016532: continuation = p16532_push_record_head(); break;
    case 016533: continuation = p16533_select_record_path(); break;
    case 016537: continuation = p16537_push_record_index(); break;
    case 016541: continuation = p16541_evaluate_record_index(); break;
    case 016542: continuation = p16542_pop_record_index(); break;
    case 016543: continuation = p16543_save_record_index(); break;
    case 016544: continuation = p16544_push_record_index_again(); break;
    case 016546: continuation = p16546_push_saved_record_value(); break;
    case 016547: continuation = p16547_evaluate_saved_record_value(); break;
    case 016550: continuation = p16550_continue_record_loop(); break;
    case 016552: continuation = p16552_finish_empty_record_loop(); break;
    case 016605: continuation = p16605(); break;
    case 016606: continuation = p16606(); break;
    case 016616: continuation = p16616(); break;
    case 016617: continuation = p16617(); break;
    case 016643: continuation = p16643(); break;
    case 016645: continuation = p16645(); break;
    case 016651: continuation = p16651(); break;
    case 016653: continuation = p16653(); break;
    case 016657: continuation = p16657(); break;
    case 016661: continuation = p16661(); break;
    case 016663: continuation = p16663(); break;
    case 016665: continuation = p16665(); break;
    case 016666: continuation = p16666(); break;
    case 016667: continuation = p16667(); break;
    case 016670: continuation = p16670(); break;
    case 016742: continuation = p16742(); break;
    case 016744: continuation = p16744(); break;
    case 016745: continuation = p16745(); break;
    case 016746: continuation = p16746(); break;
    case 016747: continuation = p16747(); break;
    case 017150: continuation = p17150(); break;
    case 017156: continuation = p17156(); break;
    case 017160: continuation = p17160(); break;
    case 017161: continuation = p17161(); break;
    case 017163: continuation = p17163(); break;
    case 017120: continuation = p17120(); break;
    case 017122: continuation = p17122(); break;
    case 017124: continuation = p17124(); break;
    case 017126: continuation = p17126(); break;
    case 017131: continuation = p17131(); break;
    case 017132: continuation = p17132(); break;
    case 017164: continuation = p17164(); break;
    case 017175: continuation = p17175(); break;
    case 017201: continuation = p17201(); break;
    case 017205: continuation = p17205(); break;
    case 017206: continuation = p17206(); break;
    case 017212: continuation = p17212(); break;
    case 017214: continuation = p17214(); break;
    case 017216: continuation = p17216(); break;
    case 017242: continuation = p17242(); break;
    case 017253: continuation = p17253(); break;
    case 017254: continuation = p17254_shared(); break;
    case 017260: continuation = p17260(); break;
    case 017266: continuation = p17266(); break;
    case 017275: continuation = p17275_shared(); break;
    case 017302: continuation = p17302(); break;
    case 017306: continuation = p17306(); break;
    case 017330: continuation = p17330(); break;
    case 017335: continuation = p17335(); break;
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
    case 017450: continuation = p17450(); break;
    case 017451: continuation = p17451(); break;
    case 017452: continuation = p17452(); break;
    case 017453: continuation = p17453(); break;
    case 017454: continuation = p17454(); break;
    case 017461: continuation = p17461(); break;
    case 017472: continuation = p17472(); break;
    case 017504: continuation = p17504(); break;
    case 017505: continuation = p17505(); break;
    case 017514: continuation = p17514(); break;
    case 017516: continuation = p17516(); break;
    case 017517: continuation = p17517(); break;
    case 017520: continuation = p17520(); break;
    case 017521: continuation = p17521(); break;
    case 017522: continuation = p17522(); break;
    case 017523: continuation = p17523(); break;
    case 017526: continuation = p17526(); break;
    case 017531: continuation = p17531(); break;
    case 017535: continuation = p17535(); break;
    case 017536: continuation = p17536(); break;
    case 017543: continuation = p17543(); break;
    case 017546: continuation = p17546(); break;
    case 017547: continuation = p17547(); break;
    case 017571: continuation = p17571(); break;
    case 017575: continuation = p17575(); break;
    case 017602: continuation = p17602(); break;
    case 017606: continuation = p17606(); break;
    case 017614: continuation = p17614(); break;
    case 017624: continuation = p17624(); break;
    case 017632: continuation = p17632(); break;
    case 017633: continuation = p17633(); break;
    case 017636: continuation = p17636(); break;
    case 017646: continuation = p17646(); break;
    case 017654: continuation = p17654(); break;
    case 017655: continuation = p17655(); break;
    case 017700: continuation = p17700(); break;
    case 017717: continuation = p17717(); break;
    case 017722: continuation = p17722(); break;
    case 017726: continuation = p17726(); break;
    case 017730: continuation = p17730(); break;
    case 017735: continuation = p17735(); break;
    case 017740: continuation = p17740(); break;
    case 017744: continuation = p17744(); break;
    case 017747: continuation = p17747(); break;
    case 017753: continuation = p17753(); break;
    case 017756: continuation = p17756(); break;
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
    case 017054: continuation = p17054(); break;
    case 017056: continuation = p17056(); break;
    case 017070: continuation = p17070(); break;
    case 017075: continuation = p17075(); break;
    case 017072: continuation = p17072(); break;
    case 020073: continuation = p20073(); break;
    case 020075: continuation = p20075(); break;
    case 020077: continuation = p20077(); break;
    case 020101: continuation = p20101(); break;
    case 020110: continuation = p20110_transfer_arguments(); break;
    case 020124: continuation = p20124_build_activation(); break;
    case 020144: continuation = p20144(); break;
    case 020150: continuation = p20150(); break;
    case 020152: continuation = p20152(); break;
    case 020155: continuation = p20155(); break;
    case 020161: continuation = p20161(); break;
    case 020170: continuation = p20170_input_primary(); break;
    case 020175: continuation = p20175_resume_input_primary(); break;
    case 020177: continuation = p20177_continue_input_primary(); break;
    case 020200: continuation = p20200_query_input_status(); break;
    case 020201: continuation = p20201_resume_input_status(); break;
    case 020202: continuation = p20202_prepare_input_transfer(); break;
    case 020205: continuation = p20205_transfer_input(); break;
    case 020206: continuation = p20206_resume_input_transfer(); break;
    case 020207: continuation = p20207_query_input_status(); break;
    case 020210: continuation = p20210_finish_input_primary(); break;
    case 020245: continuation = p20245_begin_input_continue(); break;
    case 020252: continuation = p20252_query_console(); break;
    case 020253: continuation = p20253_resume_input_continue_status(); break;
    case 020256: continuation = p20256_transfer_console(); break;
    case 020257: continuation = p20257_finish_input_continue(); break;
    case 020715: continuation = p20715(); break;
    case 020717: continuation = p20717(); break;
    case 020721: continuation = p20721(); break;
    case 020722: continuation = p20722(); break;
    case 020723: continuation = p20723(); break;
    case 020263: continuation = p20263(); break;
    case 020456: continuation = p20456(); break;
    case 020462: continuation = p20462(); break;
    case 020475: continuation = p20475(); break;
    case 020501: continuation = p20501(); break;
    case 020506: continuation = p20506(); break;
    case 020511: continuation = p20511(); break;
    case 020514: continuation = p20514(); break;
    case 020564: continuation = p20564(); break;
    case 020566: continuation = p20566(); break;
    case 020570: continuation = p20570(); break;
    case 020660: continuation = p20660(); break;
    case 020667: continuation = p20667(); break;
    case 020673: continuation = p20673_return(); break;
    case 020674: continuation = p20674(); break;
    case 021075: continuation = p21075(); break;
    case 021107: continuation = p21107(); break;
    case 021125: continuation = p21125(); break;
    case 021131: continuation = p21131(); break;
    case 021134: continuation = p21134(); break;
    case 021135: continuation = p21135(); break;
    case 021141: continuation = p21141(); break;
    case 021146: continuation = p21146(); break;
    case 021151: continuation = p21151(); break;
    case 021152: continuation = p21152(); break;
    case 021153: continuation = p21153(); break;
    case 021155: continuation = p21155(); break;
    case 021156: continuation = p21156(); break;
    case 021157: continuation = p21157(); break;
    case 021162: continuation = p21162(); break;
    case 021163: continuation = p21163(); break;
    case 021164: continuation = p21164(); break;
    case 021167: continuation = p21167(); break;
    case 021170: continuation = p21170(); break;
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
    case 021631: continuation = p21631(); break;
    case 021634: continuation = p21634(); break;
    case 021636: continuation = p21636(); break;
    case 025223: continuation = p25223(); break;
    case 025225: continuation = p25225(); break;
    case 025226: continuation = p25226(); break;
    case 025346: continuation = p25346_begin_character_output(); break;
    case 025350: continuation = p25350_continue_character_output(); break;
    case 025356: continuation = p25356_emit_end_character(); break;
    case 025361: continuation = p25361_resume_character_output(); break;
    case 025364: continuation = p25364_return_character_output(); break;
    case 025370: continuation = p25370_begin_token_source(); break;
    case 025373: continuation = p25373_resume_token_source(); break;
    case 025376: continuation = p25376_fetch_token_character(); break;
    case 025377: continuation = p25377_finish_token_source(); break;
    case 025421: continuation = p25421(); break;
    case 025424: continuation = p25424(); break;
    case 025427: continuation = p25427(); break;
    case 025434: continuation = p25434(); break;
    case 025437: continuation = p25437(); break;
    case 025445: continuation = p25445(); break;
    case 025446: continuation = p25446(); break;
    case 025730: continuation = p25730(); break;
    case 025734: continuation = p25734(); break;
    case 025736: continuation = p25736(); break;
    case 025737: continuation = p25737(); break;
    case 025741: continuation = p25741(); break;
    case 025742: continuation = p25742(); break;
    case 025745: continuation = p25745(); break;
    case 025747: continuation = p25747(); break;
    case 025750: continuation = p25750(); break;
    case 025450: continuation = p25450(); break;
    case 025451: continuation = p25451(); break;
    case 025457: continuation = p25457(); break;
    case 025460: continuation = p25460(); break;
    case 025463: continuation = p25463(); break;
    case 025470: continuation = p25470(); break;
    case 025471: continuation = p25471(); break;
    case 025475: continuation = p25475(); break;
    case 025500: continuation = p25500(); break;
    case 025502: continuation = p25502(); break;
    case 025506: continuation = p25506(); break;
    case 025507: continuation = p25507(); break;
    case 025532: continuation = p25532(); break;
    case 025543: continuation = p25543(); break;
    case 025544: continuation = p25544(); break;
    case 025556: continuation = p25556(); break;
    case 025640: continuation = p25640(); break;
    case 025641: continuation = p25641(); break;
    case 025647: continuation = p25647(); break;
    case 025653: continuation = p25653(); break;
    case 025655: continuation = p25655(); break;
    case 025660: continuation = p25660(); break;
    case 032535: continuation = p32535(); break;
    case 032540: continuation = p32540(); break;
    case 032545: continuation = p32545(); break;
    case 032546: continuation = p32546(); break;
    case 032553: continuation = p32553(); break;
    case 032556: continuation = p32556(); break;
    case 032562: continuation = p32562(); break;
    case 032564: continuation = p32564(); break;
    case 032565: continuation = p32565(); break;
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
