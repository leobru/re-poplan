#include "poplan/machine.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main()
{
    using poplan::FunctionDescriptor;
    using poplan::Machine;
    using poplan::MachineError;
    using poplan::Word48;

    require(Word48(077777777777777777ULL).raw() == Word48::mask,
            "Word48 masks values to 48 bits");

    Machine machine;
    machine.reg(06) = 070000;
    machine.accumulator() = Word48(06400000000000001ULL);
    machine.p03275_push_acc();
    require(machine.reg(06) == 067777, "03275 decrements r6");
    require(machine.memory(067777) == Word48(06400000000000001ULL),
            "03275 stores the accumulator");

    machine.accumulator() = Word48();
    machine.p03277_pop_acc();
    require(machine.accumulator() == Word48(06400000000000001ULL),
            "03277 loads the POP stack top");
    require(machine.reg(06) == 070000, "03277 increments r6");

    machine.accumulator() = Word48(06606563700065620ULL);
    const FunctionDescriptor closure = machine.p02750_decode_function();
    require(closure.environment == 065637,
            "02750 extracts the closure environment");
    require(closure.entry == 065620, "02750 extracts the code entry");
    require(!closure.special, "ordinary 660 function has no special flag");

    const FunctionDescriptor special =
        FunctionDescriptor::decode(Word48(06640000000000000ULL));
    require(special.special, "664 function takes the 15765 special path");

    bool rejected = false;
    try {
        FunctionDescriptor::decode(Word48(06400000000000001ULL));
    } catch (const MachineError &) {
        rejected = true;
    }
    require(rejected, "02750 rejects a non-function value");

    const auto install_dispatch_constants = [](Machine &target) {
        target.memory(03006) = Word48(0000000000077777ULL);
        target.memory(03007) = Word48(07700000000000000ULL);
        target.memory(03010) = Word48(00040000000000000ULL);
        target.memory(03012) = Word48(06600000000000000ULL);
    };

    const auto install_character_converter = [](Machine &target) {
        target.memory(021276) = Word48(0377);
        target.memory(021277) = Word48(04000000000000060ULL);
        target.memory(021300) = Word48(050);
        target.memory(021427) = Word48(04002525252525253ULL);

        // Only the trace-backed words needed by the focused mappings below.
        target.memory(021301) = Word48(01403046214632065ULL);
        target.memory(021305) = Word48(02722504211614074ULL);
        target.memory(021355) = Word48(02745713627577536ULL);
        target.memory(021361) = Word48(02745701726656136ULL);
        target.memory(021363) = Word48(00620501502607014ULL);
        target.memory(021364) = Word48(00000040200602005ULL);
    };

    const auto install_character_output = [](Machine &target) {
        target.memory(021457) = Word48(0377);
        target.memory(021460) = Word48(07377777777777777ULL);
        target.memory(021462) = Word48(00400000000000000ULL);
        target.memory(021463) = Word48(05400000000000000ULL);
        target.memory(025406) = Word48(1);
        target.memory(025407) = Word48(0377);
        target.memory(025410) =
            Word48(01400000000020440ULL);
        target.memory(025416) = Word48(0117);
        target.memory(025417) = target.memory(025410);
    };

    Machine dispatch;
    install_dispatch_constants(dispatch);
    dispatch.accumulator() = Word48(06606562700065576ULL);
    require(dispatch.p02750_dispatch() == 03206,
            "02750 sends an ordinary closure to 03206");
    require(dispatch.memory(03272) == Word48(06606562700065576ULL),
            "02750 saves the complete function descriptor");
    require(dispatch.memory(03273) == Word48(065627),
            "02750 extracts the closure environment");

    Machine direct_dispatch;
    install_dispatch_constants(direct_dispatch);
    direct_dispatch.accumulator() = Word48(06600000000007667ULL);
    require(direct_dispatch.p02750_dispatch() == 03261,
            "02750 sends an environment-free function to 03261");

    Machine special_dispatch;
    install_dispatch_constants(special_dispatch);
    special_dispatch.accumulator() = Word48(06640000000015765ULL);
    require(special_dispatch.p02750_dispatch() == 015765,
            "02750 sends a 664 function to the special path");

    Machine invalid_dispatch;
    install_dispatch_constants(invalid_dispatch);
    invalid_dispatch.accumulator() = Word48(06400000000000001ULL);
    require(invalid_dispatch.p02750_dispatch() == 03014,
            "02750 routes a non-function to the diagnostic dispatcher");
    require(invalid_dispatch.reg(016) == 012010,
            "02750 selects original diagnostic 12010");
    require(invalid_dispatch.accumulator()
                == Word48(06400000000000001ULL),
            "02750 restores the rejected value for the diagnostic");

    // Syntax error from tests/inputs/syntax-error.pop2. The trace reaches
    // 03014 from 03615 with r16=04020 and source object 7200000000016750.
    Machine syntax_error;
    install_dispatch_constants(syntax_error);
    syntax_error.memory(03153) = Word48(0000000000010000ULL);
    syntax_error.memory(03154) = Word48(1);
    syntax_error.memory(03155) = Word48(06400000000000000ULL);
    syntax_error.memory(03160) = Word48(2);
    syntax_error.memory(03164) = Word48(06400000000000136ULL);
    syntax_error.memory(01633) =
        Word48(06600000000003051ULL);
    syntax_error.memory(01567) =
        Word48(06600000000007475ULL);
    syntax_error.memory(016553) =
        Word48(07200000000016750ULL);
    syntax_error.reg(006) = 070000;
    syntax_error.reg(002) = 03536;
    syntax_error.reg(016) = 04020;
    syntax_error.reg(017) = 066012;
    syntax_error.reg(015) = 03615;
    syntax_error.accumulator() = Word48(03615);

    require(syntax_error.p03014_dispatch_error() == 03261,
            "03014 dispatches the syntax diagnostic function");
    require(syntax_error.memory(03173)
                == Word48(07200000000016750ULL),
            "03014 replaces the compiler origin with the source object");
    require(syntax_error.memory(03174) == Word48(04020)
                && syntax_error.memory(03176)
                    == Word48(07200000000016750ULL)
                && syntax_error.memory(03204) == Word48(04020),
            "03014 preserves the traced diagnostic scratch state");
    require(syntax_error.memory(067777)
                == Word48(07200000000016750ULL)
                && syntax_error.memory(067776)
                    == Word48(06400000000004020ULL),
            "03014 pushes the source object and tagged error code");

    require(syntax_error.p03261_enter_function() == 03051,
            "03261 enters the original diagnostic descriptor");
    require(syntax_error.p03051_unpack_error() == 03057,
            "03051 tail-calls the original formatter entry");
    require(syntax_error.reg(016) == 04020,
            "03051 extracts error code 04020 into r16");
    require(syntax_error.accumulator()
                == Word48(07200000000016750ULL),
            "03051 restores the offending source object");
    require(syntax_error.reg(006) == 070000,
            "03051 consumes both diagnostic POP arguments");
    require(syntax_error.reg(017) == 066014,
            "03051 balances its temporary hardware-stack word");

    require(syntax_error.p03057_begin_error_format() == 03261,
            "03057 dispatches the first formatter primitive");
    require(syntax_error.reg(001) == 03014
                && syntax_error.reg(002) == 01200,
            "03057 establishes its original bases");
    require(syntax_error.memory(03200) == Word48(2),
            "03057 installs traced formatting phase 2");
    require(syntax_error.memory(067777)
                == Word48(06400000000000136ULL),
            "03057 passes tagged character 0136 to CUCHIN");
    require(syntax_error.reg(006) == 067777,
            "03057 leaves one CUCHIN argument on the POP stack");
    require(syntax_error.reg(017) == 066022,
            "03057 preserves six diagnostic words on the hardware stack");
    require(syntax_error.memory(066014) == Word48(03235),
            "03057 saves the formatter return");
    require(syntax_error.memory(066015) == Word48(03014),
            "03057 saves the diagnostic base");
    require(syntax_error.memory(066016) == Word48(03536),
            "03057 saves the caller's r2");
    require(syntax_error.memory(066017)
                == Word48(07200000000016750ULL),
            "03057 saves the source object beneath the formatting call");
    require(syntax_error.memory(066020) == Word48(04020),
            "03057 saves scratch error code 03174");
    require(syntax_error.memory(066021) == Word48(04020),
            "03057 saves scratch error code 03204");

    require(syntax_error.p03261_enter_function() == 07475,
            "03261 enters the traced CUCHIN descriptor");
    syntax_error.memory(07510) = Word48(07740000000000000ULL);
    syntax_error.memory(07511) = Word48(06400000000000136ULL);
    syntax_error.memory(07512) = Word48(012);
    require(syntax_error.p07475_cuchin() == 021255,
            "07475 maps diagnostic character 0136 to the input wrapper");
    require(syntax_error.memory(07513)
                == Word48(06400000000000136ULL),
            "07475 preserves CUCHIN's original argument");
    require(syntax_error.accumulator() == Word48(012)
                && syntax_error.reg(006) == 070000,
            "07475 consumes the argument and selects code 0012");

    require(syntax_error.p21255_begin_character_input() == 021275,
            "21255 calls the original character conversion entry");
    require(syntax_error.memory(021263) == Word48(012),
            "21255 saves the selected character code");
    require(syntax_error.memory(066024) == Word48(03235)
                && syntax_error.reg(017) == 066025,
            "21255 preserves the environment-binding return on r17");
    require(syntax_error.accumulator() == Word48(012)
                && syntax_error.reg(015) == 021260,
            "21255 enters 21275 with the traced accumulator and link");

    // Traced output conversion for the first diagnostic heading character.
    Machine character_output;
    install_character_converter(character_output);
    character_output.accumulator() =
        Word48(06400000000000052ULL);
    character_output.remainder() = Word48(0777);
    character_output.alu_mode() = 077;
    character_output.reg(015) = 021260;
    character_output.reg(017) = 066025;
    require(character_output.p21275_encode_character() == 021260,
            "21275 returns the converted heading character through r15");
    require(character_output.accumulator() == Word48(031),
            "21275 maps traced output character 052 to 031");
    require(character_output.memory(021430) == Word48(052),
            "21264 stores the masked source character at 21430");
    require(character_output.reg(014) == 021363
                && character_output.reg(013) == 050,
            "21264 selects word 21363 and its leading byte");
    require(character_output.reg(010) == 021264
                && character_output.reg(016) == 021354
                && character_output.reg(015) == 021260,
            "21264 preserves the traced converter register state");
    require(character_output.remainder() == Word48()
                && character_output.alu_mode() == 007
                && character_output.reg(017) == 066025,
            "21264 finishes in traced logical mode with zero RMR");

    struct CharacterMapping {
        std::uint16_t source;
        std::uint16_t converted;
    };
    const CharacterMapping output_mappings[] = {
        {012, 0377},
        {040, 017},
        {060, 000},
    };
    for (const CharacterMapping mapping : output_mappings) {
        Machine converter;
        install_character_converter(converter);
        converter.accumulator() = Word48(
            06400000000000000ULL | mapping.source);
        converter.reg(015) = 01234;
        require(converter.p21275_encode_character() == 01234
                    && converter.accumulator()
                        == Word48(mapping.converted),
                "21275 reproduces an additional traced output mapping");
    }

    const CharacterMapping input_mappings[] = {
        {031, 052},
        {001, 061},
    };
    for (const CharacterMapping mapping : input_mappings) {
        Machine converter;
        install_character_converter(converter);
        converter.accumulator() = Word48(mapping.source);
        converter.reg(015) = 05670;
        require(converter.p21274_decode_character() == 05670
                    && converter.accumulator()
                        == Word48(mapping.converted),
                "21274 reproduces a traced input mapping");
    }

    Machine character_return;
    install_character_converter(character_return);
    character_return.accumulator() =
        Word48(06400000000000052ULL);
    character_return.reg(015) = 03235;
    character_return.reg(017) = 066024;
    require(character_return.p21255_begin_character_input() == 021275,
            "21255 starts the output conversion return bracket");
    require(character_return.p21275_encode_character() == 021260,
            "21275 returns to the forwarding entry");
    require(character_return.p21260_forward_converted_character()
                == 025346
                && character_return.reg(015) == 021261,
            "21260 transfers to output entry 25346 with link 21261");
    require(character_return.p21261_return_character() == 03235,
            "21261 restores the saved environment-binding link");
    require(character_return.accumulator() == Word48(03235)
                && character_return.reg(015) == 03235
                && character_return.reg(017) == 066024,
            "21261 balances r17 while returning through 03235");

    // Complete trace-backed output of 052 -> 031 through 25346 and 21443.
    Machine buffered_character;
    install_character_converter(buffered_character);
    install_character_output(buffered_character);
    buffered_character.accumulator() =
        Word48(06400000000000052ULL);
    buffered_character.reg(015) = 03235;
    buffered_character.reg(017) = 066023;
    require(buffered_character.p21255_begin_character_input() == 021275
                && buffered_character.p21275_encode_character() == 021260
                && buffered_character.p21260_forward_converted_character()
                    == 025346,
            "21255..21260 reaches the translated output entry");
    require(buffered_character.p25346_begin_character_output() == 021443,
            "25346 enters the packed-descriptor helper");
    require(buffered_character.accumulator() == Word48(031)
                && buffered_character.memory(066024) == Word48(031)
                && buffered_character.memory(066025) == Word48(021261)
                && buffered_character.reg(015) == 025350
                && buffered_character.reg(016) == 025417
                && buffered_character.reg(017) == 066026,
            "25346 preserves the converted byte and both return links");
    require(buffered_character.p21443_advance_descriptor() == 025350,
            "21443 returns to the output continuation");
    require(buffered_character.memory(020440)
                == Word48(00620000000000000ULL)
                && buffered_character.memory(025417)
                    == Word48(02000000000020440ULL),
            "21443 packs 031 and advances descriptor 140 to 200");
    require(buffered_character.reg(014) == 020440
                && buffered_character.reg(017) == 066026,
            "21443 preserves its packed-word address and balances r17");
    require(buffered_character.p25350_continue_character_output()
                == 021261,
            "25350 returns an ordinary converted character to 21261");
    require(buffered_character.memory(025412) == Word48(1)
                && buffered_character.accumulator() == Word48(031)
                && buffered_character.reg(015) == 021261
                && buffered_character.reg(017) == 066024,
            "25350 counts the byte and releases its two-word frame");
    require(buffered_character.p21261_return_character() == 03235
                && buffered_character.reg(017) == 066023,
            "the complete output bracket restores 03235 and r17");

    // The first diagnostic conversion, 012 -> 377, takes the traced 20245
    // continuation branch and resumes at 25361.
    Machine terminated_output;
    install_character_converter(terminated_output);
    install_character_output(terminated_output);
    terminated_output.accumulator() =
        Word48(06400000000000012ULL);
    terminated_output.reg(015) = 03235;
    terminated_output.reg(017) = 066023;
    terminated_output.p21255_begin_character_input();
    terminated_output.p21275_encode_character();
    terminated_output.p21260_forward_converted_character();
    terminated_output.p25346_begin_character_output();
    terminated_output.p21443_advance_descriptor();
    require(terminated_output.p25350_continue_character_output()
                == 020245
                && terminated_output.reg(015) == 025361,
            "25350 calls boundary 20245 for converted character 0377");
    require(terminated_output.accumulator() == Word48()
                && terminated_output.remainder() == Word48()
                && terminated_output.memory(020440)
                    == Word48(07760000000000000ULL)
                && terminated_output.memory(025412) == Word48(1)
                && terminated_output.reg(017) == 066026,
            "the 0377 branch retains its frame across 20245");
    require(terminated_output.p25361_resume_character_output() == 021261,
            "25361 resumes the 0377 output call after 20245");
    require(terminated_output.memory(025417)
                == Word48(01400000000020440ULL)
                && terminated_output.memory(025412) == Word48()
                && terminated_output.accumulator() == Word48(0377)
                && terminated_output.reg(017) == 066024,
            "25361 resets descriptor and count before releasing the frame");
    require(terminated_output.p21261_return_character() == 03235
                && terminated_output.reg(017) == 066023,
            "the resumed 0377 path restores the original caller");

    Machine descriptor_wrap;
    install_character_output(descriptor_wrap);
    descriptor_wrap.reg(015) = 07654;
    descriptor_wrap.reg(016) = 025417;
    descriptor_wrap.reg(017) = 060000;
    descriptor_wrap.memory(025417) =
        Word48(04000000000020440ULL);
    descriptor_wrap.memory(020440) =
        Word48(0123456701234500ULL);
    descriptor_wrap.accumulator() = Word48(017);
    require(descriptor_wrap.p21443_advance_descriptor() == 07654,
            "21443 returns through r15 after its sixth byte");
    require(descriptor_wrap.memory(020440)
                == Word48(0123456701234417ULL)
                && descriptor_wrap.memory(025417)
                    == Word48(01400000000020441ULL)
                && descriptor_wrap.reg(014) == 0
                && descriptor_wrap.reg(017) == 060000,
            "21443 replaces the low byte and wraps to the next packed word");

    Machine output_limit;
    install_character_converter(output_limit);
    install_character_output(output_limit);
    output_limit.memory(025412) = Word48(0116);
    output_limit.accumulator() =
        Word48(06400000000000052ULL);
    output_limit.reg(015) = 03235;
    output_limit.reg(017) = 066023;
    output_limit.p21255_begin_character_input();
    output_limit.p21275_encode_character();
    output_limit.p21260_forward_converted_character();
    output_limit.p25346_begin_character_output();
    output_limit.p21443_advance_descriptor();
    require(output_limit.p25350_continue_character_output() == 025346
                && output_limit.accumulator() == Word48(0377)
                && output_limit.reg(015) == 021261
                && output_limit.reg(017) == 066024,
            "25355 injects 0377 after configured count 0117");
    output_limit.p25346_begin_character_output();
    output_limit.p21443_advance_descriptor();
    require(output_limit.p25350_continue_character_output() == 020245
                && output_limit.reg(015) == 025361
                && output_limit.memory(025412) == Word48(0120),
            "the injected 0377 reaches the same 20245 boundary");

    // Snapshot at 03072 after CUCHIN and BIND_ENVIRONMENT return. This closes
    // the first formatter call and enters the packed-character sequence.
    Machine formatter_return;
    formatter_return.reg(001) = 03014;
    formatter_return.reg(002) = 01200;
    formatter_return.reg(017) = 066022;
    formatter_return.memory(066017) =
        Word48(07200000000016750ULL);
    formatter_return.memory(066020) = Word48(04020);
    formatter_return.memory(066021) = Word48(04020);
    formatter_return.memory(01517) =
        Word48(06600000000007475ULL);
    formatter_return.accumulator() =
        Word48(06600000000003051ULL);

    require(formatter_return.p03072_resume_error_format() == 016313,
            "03072 starts the diagnostic character sequence");
    require(formatter_return.reg(017) == 066017,
            "03072 releases the three temporary formatter words");
    require(formatter_return.memory(03200) == Word48()
                && formatter_return.memory(03204) == Word48(04020)
                && formatter_return.memory(03174) == Word48(04020)
                && formatter_return.memory(03173)
                    == Word48(07200000000016750ULL),
            "03072 restores the saved diagnostic state");
    require(formatter_return.memory(01567)
                == Word48(06600000000007475ULL),
            "03072 reinstalls the runtime character primitive");
    require(formatter_return.reg(016) == 03162
                && formatter_return.reg(014) == 013
                && formatter_return.reg(015) == 03101,
            "03072 selects the traced heading and length");

    formatter_return.memory(016336) =
        Word48(06400000000000000ULL);
    formatter_return.memory(021457) = Word48(0377);
    formatter_return.memory(021460) =
        Word48(07377777777777777ULL);
    formatter_return.memory(021461) =
        Word48(03000000000000001ULL);
    formatter_return.memory(03162) =
        Word48(01242505212447573ULL);
    formatter_return.reg(006) = 070000;
    require(formatter_return.p16313_begin_character_sequence() == 021431,
            "16313 enters the original packed-text character routine");
    require(formatter_return.reg(001) == 016313
                && formatter_return.reg(002) == 013
                && formatter_return.reg(016) == 016340
                && formatter_return.reg(015) == 016321,
            "16313 establishes its traced output-loop registers");
    require(formatter_return.memory(016340)
                == Word48(06400000000003162ULL),
            "16313 constructs the tagged heading cursor");
    require(formatter_return.reg(017) == 066023,
            "16313 preserves four caller words on the hardware stack");
    require(formatter_return.memory(066017) == Word48()
                && formatter_return.memory(066020) == Word48(03101)
                && formatter_return.memory(066021) == Word48(03014)
                && formatter_return.memory(066022) == Word48(01200),
            "16313 reproduces the traced text-output save area");

    require(formatter_return.p21431_buffer_char() == 016321,
            "21431 returns the first diagnostic heading character");
    require(formatter_return.accumulator() == Word48(052),
            "21431 extracts traced character 052");
    require(formatter_return.memory(016340)
                == Word48(06000000000003162ULL),
            "21431 advances the packed-text cursor by one character");
    require(formatter_return.reg(014) == 03162
                && formatter_return.reg(017) == 066023,
            "21431 preserves its source pointer and balances r17");

    Machine cursor_wrap;
    cursor_wrap.reg(014) = 0777;
    cursor_wrap.reg(015) = 01234;
    cursor_wrap.reg(016) = 01000;
    cursor_wrap.reg(017) = 05000;
    cursor_wrap.memory(01000) =
        Word48(04200000000003162ULL);
    cursor_wrap.memory(03162) =
        Word48(01242505212447573ULL);
    cursor_wrap.memory(021457) = Word48(0377);
    cursor_wrap.memory(021460) =
        Word48(07377777777777777ULL);
    cursor_wrap.memory(021461) =
        Word48(03000000000000001ULL);
    require(cursor_wrap.p21431_buffer_char() == 01234,
            "21431 returns through r15 when the cursor wraps");
    require(cursor_wrap.memory(01000)
                == Word48(06600000000003163ULL),
            "21431 advances to the next packed-text word after eight bytes");
    require(cursor_wrap.reg(014) == 0
                && cursor_wrap.reg(017) == 05000,
            "21431 takes the original cursor-wrap branch and balances r17");

    formatter_return.memory(016335) =
        Word48(06400000000000000ULL);
    formatter_return.memory(01567) =
        Word48(06600000000007475ULL);
    install_dispatch_constants(formatter_return);
    require(formatter_return.p16321_dispatch_character() == 03261,
            "16321 dispatches the tagged heading character");
    require(formatter_return.memory(016337)
                == Word48(06400000000000052ULL)
                && formatter_return.memory(067777)
                    == Word48(06400000000000052ULL),
            "16321 preserves and pushes tagged character 052");

    require(formatter_return.p03261_enter_function() == 07475,
            "16321 enters CUCHIN through ordinary POP dispatch");
    formatter_return.memory(07510) =
        Word48(07740000000000000ULL);
    formatter_return.memory(07511) =
        Word48(06400000000000136ULL);
    require(formatter_return.p07475_cuchin() == 021255,
            "07475 accepts an ordinary tagged heading character");
    require(formatter_return.accumulator()
                == Word48(06400000000000052ULL)
                && formatter_return.reg(006) == 070000,
            "07475 forwards character 052 without changing its tag");

    Machine sequence_continue;
    sequence_continue.reg(001) = 016313;
    sequence_continue.reg(002) = 013;
    sequence_continue.reg(015) = 016325;
    sequence_continue.reg(017) = 066023;
    sequence_continue.memory(066017) = Word48();
    sequence_continue.memory(066020) = Word48(03101);
    sequence_continue.memory(066021) = Word48(03014);
    sequence_continue.memory(066022) = Word48(01200);
    require(sequence_continue.p16325_continue_character_sequence()
                == 016320,
            "16325 continues a nonfinal counted character sequence");
    require(sequence_continue.reg(002) == 012
                && sequence_continue.reg(017) == 066023,
            "16325 decrements the count without disturbing the save area");

    sequence_continue.reg(002) = 1;
    require(sequence_continue.p16325_continue_character_sequence()
                == 03101,
            "16325 returns to the diagnostic formatter after the final byte");
    require(sequence_continue.reg(002) == 01200
                && sequence_continue.reg(001) == 03014
                && sequence_continue.reg(015) == 03101,
            "16331 restores the character-sequence caller's registers");
    require(sequence_continue.reg(017) == 066017
                && sequence_continue.accumulator() == Word48(),
            "16331 releases the four-word save area and restores accumulator");

    // 03261 and the zero-environment path through 03235 are a matched
    // entry/return pair in the original evaluator.
    Machine direct;
    direct.reg(017) = 04000;
    direct.reg(015) = 01234;
    direct.memory(03274) = Word48(06600000000007773ULL);
    direct.memory(03272) = Word48(06600000000007667ULL);
    require(direct.p03261_enter_function() == 07667,
            "03261 dispatches to the descriptor entry");
    require(direct.reg(015) == 03235,
            "03261 installs the environment-binding return");
    require(direct.reg(017) == 04002,
            "03261 saves descriptor and link on the hardware stack");
    require(direct.p03235_bind_environment() == 01234,
            "03235 restores the generated-code link");
    require(direct.reg(017) == 04000,
            "03235 releases the 03261 save area");
    require(direct.memory(03274) == Word48(06600000000007773ULL),
            "03235 restores the previous current function");

    // The same zero-environment fast path through the full 03206 entry.
    Machine ordinary;
    ordinary.reg(017) = 04100;
    ordinary.reg(015) = 02345;
    ordinary.memory(03274) = Word48(06600000000007773ULL);
    ordinary.memory(03272) = Word48(06600000000007555ULL);
    ordinary.memory(03273) = Word48();
    require(ordinary.p03206_prepare_ordinary_call() == 07555,
            "03206 dispatches an ordinary zero-environment function");
    require(ordinary.p03235_bind_environment() == 02345,
            "03206/03235 restore the original link");
    require(ordinary.reg(017) == 04100,
            "03206/03235 balance the hardware stack");

    // Reduced arity-3 Man-or-Boy baseline from the traced generated object.
    // The B descriptor always selects entry 65576 through environment 65627,
    // whose +3 record has a zero address. No older K is rebound at 65763.
    Machine man_or_boy;
    const Word48 b_descriptor(06606562700065576ULL);
    man_or_boy.memory(03272) = b_descriptor;
    man_or_boy.memory(03273) = Word48(065627);
    man_or_boy.memory(03274) = Word48(06600000000007667ULL);
    man_or_boy.memory(065632) = Word48(07040000000000000ULL);
    man_or_boy.memory(065763) = Word48(06400000000000002ULL);
    man_or_boy.reg(017) = 04200;

    const auto invoke_traced_b = [&man_or_boy, b_descriptor]() {
        man_or_boy.memory(03272) = b_descriptor;
        man_or_boy.reg(015) = 03456;
        require(man_or_boy.p03206_prepare_ordinary_call() == 065576,
                "03206 repeatedly dispatches the traced B descriptor");
        require(man_or_boy.p03235_bind_environment() == 03456,
                "03235 returns from traced B without an environment walk");
    };

    invoke_traced_b();
    require(man_or_boy.memory(065763)
                == Word48(06400000000000002ULL),
            "outer B dispatch leaves traced K=2 in the shared slot");
    man_or_boy.memory(065763) = Word48(06400000000000001ULL);
    invoke_traced_b();
    require(man_or_boy.memory(065763)
                == Word48(06400000000000001ULL),
            "inner B dispatch sees the same shared K slot");
    man_or_boy.memory(065763) = Word48(06400000000000000ULL);
    invoke_traced_b();
    require(man_or_boy.memory(065763)
                == Word48(06400000000000000ULL),
            "third B dispatch fails to restore outer K=1");
    require(man_or_boy.reg(017) == 04200,
            "repeated traced B dispatches balance the hardware stack");

    // Synthetic one-slot environment exercising the literal 03242..03256
    // rebinding sequence.
    Machine binding;
    binding.memory(03267) = Word48(0000000100000000ULL);
    binding.memory(03270) = Word48(0177777777777777ULL);
    binding.memory(03274) = Word48(06600100000005000ULL);
    binding.memory(01003) = Word48(02000);
    binding.memory(02000) = Word48(2);
    binding.memory(02001) = Word48(03000);
    binding.memory(02777) = Word48(07700000000012345ULL);
    binding.memory(04000) = Word48(06600000000007773ULL);
    binding.memory(04001) = Word48(01234);
    binding.memory(04002) = Word48(06500000000000000ULL);
    binding.memory(04003) = Word48(04567);
    binding.reg(017) = 04004;
    require(binding.p03235_bind_environment() == 01234,
            "03235 returns through the saved link after binding");
    const Word48 rebound(
        (07700000000012345ULL & 0177777777777777ULL)
        ^ (static_cast<std::uint64_t>(04567) << 24));
    require(binding.memory(02777) == rebound,
            "03235 combines the saved environment field with the slot word");
    require(binding.memory(03000) == Word48(06500000000000000ULL),
            "03235 restores the captured value");
    require(binding.reg(017) == 04000,
            "03235 consumes one capture pair and its saved return pair");

    // Trace snapshot at 20124 from generated entry 65576 (one argument).
    Machine activation;
    activation.memory(20142) = Word48(077777);
    activation.memory(20143) = Word48(1);
    activation.reg(017) = 066013;
    activation.reg(016) = 1;
    activation.reg(015) = 065576;
    activation.reg(002) = 01001;
    activation.reg(006) = 070000;
    activation.accumulator() = Word48(07100000000065741ULL);
    require(activation.p20124_build_activation() == 065576,
            "20124 returns to generated entry 65576");
    require(activation.reg(017) == 066012,
            "20124 reproduces the traced one-argument activation pointer");
    require(activation.reg(006) == 067777,
            "20124 pushes one actual value on the POP stack");
    require(activation.memory(067777)
                == Word48(07100000000065741ULL),
            "20124 transfers the traced actual value");
    require(activation.reg(002) == 01001,
            "20124 restores r2");
    require(activation.accumulator() == Word48(01001),
            "20124 leaves the restored r2 value in the accumulator");
    require(activation.memory(20141) == Word48(0166017),
            "20124 reproduces the traced activation-end scratch word");

    Machine no_arguments;
    no_arguments.reg(015) = 04567;
    no_arguments.reg(016) = 0;
    no_arguments.reg(017) = 05000;
    no_arguments.accumulator() = Word48(012345);
    require(no_arguments.p20110_transfer_arguments() == 04567,
            "20110 computes an immediate return when r16 is zero");
    require(no_arguments.reg(017) == 05000
                && no_arguments.accumulator() == Word48(012345),
            "20110 zero-argument return leaves machine state intact");

    Machine one_argument;
    one_argument.reg(006) = 067777;
    one_argument.reg(015) = 05670;
    one_argument.reg(016) = 1;
    one_argument.reg(017) = 05100;
    one_argument.memory(067777) = Word48(06400000000000007ULL);
    require(one_argument.p20110_transfer_arguments() == 05670,
            "20110 returns through r15 for one argument");
    require(one_argument.reg(016) == 0
                && one_argument.reg(017) == 05101,
            "20110 takes the original one-argument POP_ACC path");
    require(one_argument.reg(006) == 070000
                && one_argument.accumulator()
                    == Word48(06400000000000007ULL),
            "20110 pops the sole argument into the accumulator");

    // Three arguments exercise both 20124's transfer loop and 20110's
    // activation layout.
    Machine arguments;
    arguments.memory(20142) = Word48(077777);
    arguments.memory(20143) = Word48(1);
    arguments.reg(017) = 066013;
    arguments.reg(016) = 3;
    arguments.reg(015) = 05555;
    arguments.reg(002) = 01111;
    arguments.reg(006) = 070000;
    arguments.memory(066011) = Word48(06400000000000001ULL);
    arguments.memory(066012) = Word48(06400000000000002ULL);
    arguments.accumulator() = Word48(06400000000000003ULL);
    arguments.p20124_build_activation();
    require(arguments.reg(017) == 066010,
            "20124 reserves three words below the activation pointer");
    require(arguments.reg(006) == 067775,
            "20124 pushes three actual values");

    arguments.reg(016) = 3;
    arguments.reg(015) = 04444;
    arguments.reg(003) = 02222;
    arguments.reg(002) = 03333;
    require(arguments.p20110_transfer_arguments() == 04444,
            "20110 returns through the generated-code link");
    require(arguments.reg(017) == 066013,
            "20110 advances over the three activation arguments");
    require(arguments.reg(006) == 070000,
            "20110 consumes all three POP arguments");
    require(arguments.memory(066011)
                == Word48(06400000000000001ULL),
            "20110 stores argument one first in the activation");
    require(arguments.memory(066012)
                == Word48(06400000000000002ULL),
            "20110 stores argument two second in the activation");
    require(arguments.memory(066013)
                == Word48(06400000000000003ULL),
            "20110 stores argument three last in the activation");
    require(arguments.reg(002) == 03333 && arguments.reg(003) == 02222,
            "20110 restores its scratch index registers");
    require(arguments.accumulator()
                == Word48(06400000000000003ULL),
            "20110 leaves the last activation argument in the accumulator");
}
