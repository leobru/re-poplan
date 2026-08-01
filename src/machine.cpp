#include "poplan/machine.hpp"

namespace poplan {

bool FunctionDescriptor::is_function(Word48 value)
{
    return (value & tag_mask) == function_tag;
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
