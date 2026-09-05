#ifndef POPLAN_MACHINE_HPP
#define POPLAN_MACHINE_HPP

#include "poplan/word48.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iosfwd>
#include <stdexcept>
#include <vector>

namespace poplan {

class MachineError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class E71InputRequired : public MachineError {
public:
    using MachineError::MachineError;
};

enum class ExecutionStatus {
    running,
    halted,
    input_required,
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

    Word48 &remainder() { return remainder_; }
    const Word48 &remainder() const { return remainder_; }

    std::uint8_t &alu_mode() { return alu_mode_; }
    std::uint8_t alu_mode() const { return alu_mode_; }

    std::uint16_t &reg(std::size_t index) { return registers_.at(index); }
    std::uint16_t reg(std::size_t index) const { return registers_.at(index); }

    Word48 &memory(std::uint16_t address) { return memory_.at(address); }
    Word48 memory(std::uint16_t address) const { return memory_.at(address); }

    bool &console_available() { return console_available_; }
    bool console_available() const { return console_available_; }

    void queue_console_input(std::vector<std::uint8_t> line);
    const std::vector<std::uint8_t> &console_output() const
    {
        return console_output_;
    }
    void clear_console_output() { console_output_.clear(); }

    // In-memory emulation of extracode 071. The control word/program is read
    // from BESM memory, and terminal data remains in GOST/KOI-7 byte form.
    void emulate_e71(std::uint16_t control_address);

    // Raw BESM execution used to run the extracted POPLAN image. Image words
    // are six-byte big-endian values mapped at addresses 00000..35777.
    void load_image(std::istream &input);
    void start(std::uint16_t address, bool right_half = false);
    void boot_static_image();
    ExecutionStatus step();
    std::uint16_t program_counter() const { return program_counter_; }
    bool right_half() const { return right_half_; }
    std::uint64_t instruction_count() const { return instruction_count_; }
    std::uint64_t translated_routine_count() const
    {
        return translated_routine_count_;
    }
    void set_translated_routines_enabled(bool enabled)
    {
        translated_routines_enabled_ = enabled;
    }
    bool translated_routines_enabled() const
    {
        return translated_routines_enabled_;
    }
    void disable_translated_routine(std::uint16_t address)
    {
        disabled_translated_routines_.push_back(address & 077777);
    }

    // Literal translations of POPLAN entries 03275 and 03277.
    void p03275_push_acc();
    void p03277_pop_acc();
    std::uint16_t p03301();
    std::uint16_t p03303_store_stack_top();
    std::uint16_t p03314();
    std::uint16_t p03330();
    std::uint16_t p03374();
    std::uint16_t p03516();
    std::uint16_t p03506();
    std::uint16_t p03531();
    std::uint16_t p03536();
    std::uint16_t p03544(std::uint16_t entry);
    std::uint16_t p03631();
    std::uint16_t p03632();
    std::uint16_t p03702();
    std::uint16_t p03716();
    std::uint16_t p03724();
    std::uint16_t p03736();
    std::uint16_t p04074();
    std::uint16_t p04161();
    std::uint16_t p04214(std::uint16_t entry);
    std::uint16_t p05160();

    // Short indirect-load helpers used by the evaluator and record code.
    std::uint16_t p05207();
    std::uint16_t p05211();
    std::uint16_t p05213();
    std::uint16_t p05215();
    std::uint16_t p05221();
    std::uint16_t p05230();
    std::uint16_t p05405();
    std::uint16_t p05410();

    // Numeric helper reached by the generated quine evaluator loop.
    std::uint16_t p03413_numeric_update();
    std::uint16_t p05430();
    std::uint16_t p05433();
    std::uint16_t p05434();
    std::uint16_t p05435();
    std::uint16_t p05436();
    std::uint16_t p05440();
    std::uint16_t p05441();
    std::uint16_t p05447();

    // Decode performed by the first part of EVAL_DISPATCH at 02750.
    FunctionDescriptor p02750_decode_function() const;
    std::uint16_t p02750_dispatch();
    std::uint16_t p02764();
    std::uint16_t p02767();
    std::uint16_t p02770();
    std::uint16_t p01004();
    std::uint16_t p01107();
    std::uint16_t p01167();

    // Trace-confirmed compiler/evaluator entries.  The neutral address names
    // are intentional until the original dictionary establishes stronger
    // semantics for these blocks.
    std::uint16_t p06134();
    std::uint16_t p06343();
    std::uint16_t p06424();
    std::uint16_t p06526();
    std::uint16_t p06712();
    std::uint16_t p06733();
    std::uint16_t p06740();
    std::uint16_t p06744();
    std::uint16_t p06750();
    std::uint16_t p07673();
    std::uint16_t p10232();
    std::uint16_t p11464();
    std::uint16_t p11500();
    std::uint16_t p11524();
    std::uint16_t p13007();
    std::uint16_t p13017();
    std::uint16_t p13047();
    std::uint16_t p13063();
    std::uint16_t p13072();
    std::uint16_t p13111();
    std::uint16_t p13121();
    std::uint16_t p13130();
    std::uint16_t p13216();
    std::uint16_t p13217();
    std::uint16_t p16005();
    std::uint16_t p16151();
    std::uint16_t p16254();
    std::uint16_t p16341();
    std::uint16_t p16477();
    std::uint16_t p16742();
    std::uint16_t p06623();
    std::uint16_t p06631();
    std::uint16_t p06637();
    std::uint16_t p06645();
    std::uint16_t p06650();
    std::uint16_t p04322();
    std::uint16_t p04426();
    std::uint16_t p04447();
    std::uint16_t p04467();
    std::uint16_t p04507();
    std::uint16_t p04536();
    std::uint16_t p04665();
    std::uint16_t p04675();
    std::uint16_t p04721();
    std::uint16_t p04740();
    std::uint16_t p05007();
    std::uint16_t p17013();
    std::uint16_t p17021();
    std::uint16_t p17045();
    std::uint16_t p17070();
    std::uint16_t p17337();
    std::uint16_t p17340();
    std::uint16_t p17341();
    std::uint16_t p17342();
    std::uint16_t p17417();
    std::uint16_t p17472();
    std::uint16_t p17571();
    std::uint16_t p17602();
    std::uint16_t p17614();
    std::uint16_t p17624();
    std::uint16_t p17762();
    std::uint16_t p17774();
    std::uint16_t p20077();
    std::uint16_t p20144();
    std::uint16_t p21464();

    // Literal translations of the ordinary-function call cluster.
    // The return value is the next BESM-6 instruction address.
    std::uint16_t p03206_prepare_ordinary_call();
    std::uint16_t p03235_bind_environment();
    std::uint16_t p03261_enter_function();
    std::uint16_t p03337();
    std::uint16_t p03340();
    std::uint16_t p03341();
    std::uint16_t p03342();

    // Literal translations of the activation/argument-transfer entries.
    std::uint16_t p20110_transfer_arguments();
    std::uint16_t p20124_build_activation();
    std::uint16_t p20170_input_primary();
    std::uint16_t p20175_resume_input_primary();
    std::uint16_t p20177_continue_input_primary();
    std::uint16_t p20201_resume_input_status();
    std::uint16_t p20202_prepare_input_transfer();
    std::uint16_t p20206_resume_input_transfer();
    std::uint16_t p20210_finish_input_primary();

    // Literal translations of the diagnostic call path. 03014 packages the
    // source object and tagged error code; 03051 unpacks them for 03057,
    // whose entry block preserves context and starts diagnostic formatting.
    std::uint16_t p03014_dispatch_error();
    std::uint16_t p03051_unpack_error();
    std::uint16_t p03057_begin_error_format();
    std::uint16_t p03072_resume_error_format();

    // Primitive-runtime entries reached by the first diagnostic-format call.
    std::uint16_t p07472();
    std::uint16_t p07475_cuchin();
    std::uint16_t p07533();
    std::uint16_t p11536();
    std::uint16_t p11541_match_tagged_value();
    std::uint16_t p11673_begin_generated_update();
    std::uint16_t p11675_continue_generated_update();
    std::uint16_t p11701_match_generated_value();
    std::uint16_t p11703_update_generated_value();
    std::uint16_t p11710_finish_generated_update();
    std::uint16_t p11717_begin_generated_binding();
    std::uint16_t p11720_finish_generated_binding();
    std::uint16_t p11726_begin_generated_rebinding();
    std::uint16_t p11727_continue_generated_rebinding();
    std::uint16_t p11731_finish_generated_rebinding();
    std::uint16_t p11755_begin_generated_binding();
    std::uint16_t p11756_begin_generated_rebinding();
    std::uint16_t p11757_enter_generated_rebinding();
    std::uint16_t p07773_prstri();
    std::uint16_t p12674_prreal();
    std::uint16_t p13207();
    std::uint16_t p16313_begin_character_sequence();
    std::uint16_t p16321_dispatch_character();
    std::uint16_t p16325_continue_character_sequence();
    std::uint16_t p16346();
    std::uint16_t p15765_dispatch_special_function();
    std::uint16_t p16406();
    std::uint16_t p16421_lookup_tagged_byte();
    std::uint16_t p16457_shift_record();
    std::uint16_t p16463_begin_record_evaluation();
    std::uint16_t p16465_store_record_evaluation();
    std::uint16_t p16466_return_record_evaluation();
    std::uint16_t p16467();
    std::uint16_t p16531_continue_record_shift();
    std::uint16_t p16532_push_record_head();
    std::uint16_t p16533_select_record_path();
    std::uint16_t p16537_push_record_index();
    std::uint16_t p16541_evaluate_record_index();
    std::uint16_t p16542_pop_record_index();
    std::uint16_t p16543_save_record_index();
    std::uint16_t p16544_push_record_index_again();
    std::uint16_t p16546_push_saved_record_value();
    std::uint16_t p16547_evaluate_saved_record_value();
    std::uint16_t p16550_continue_record_loop();
    std::uint16_t p16552_finish_empty_record_loop();
    std::uint16_t p16505_begin_record_shift();
    std::uint16_t p16507_resume_record_shift();
    std::uint16_t p16511();
    std::uint16_t p16605();
    std::uint16_t p16643();
    std::uint16_t p16645();
    std::uint16_t p16651();
    std::uint16_t p16653();
    std::uint16_t p16657();
    std::uint16_t p16661();
    std::uint16_t p16663();
    std::uint16_t p17242();
    std::uint16_t p17253();
    std::uint16_t p20245_begin_input_continue();
    std::uint16_t p20252_query_console();
    std::uint16_t p20253_resume_input_continue_status();
    std::uint16_t p20256_transfer_console();
    std::uint16_t p20257_finish_input_continue();
    std::uint16_t p20263();
    std::uint16_t p20456();
    std::uint16_t p20475();
    std::uint16_t p20660();
    std::uint16_t p20667();
    std::uint16_t p20673_return();
    std::uint16_t p20674();
    std::uint16_t p21107();
    std::uint16_t p21141();
    std::uint16_t p21146();
    std::uint16_t p21151();
    std::uint16_t p21152();
    std::uint16_t p21153();
    std::uint16_t p21155();
    std::uint16_t p21156();
    std::uint16_t p21157();
    std::uint16_t p21162();
    std::uint16_t p21163();
    std::uint16_t p21164();
    std::uint16_t p21167();
    std::uint16_t p21170();
    std::uint16_t p21255_begin_character_input();
    std::uint16_t p21251_extract_character();
    std::uint16_t p21253_resume_character_extract();
    std::uint16_t p21260_forward_converted_character();
    std::uint16_t p21261_return_character();
    std::uint16_t p21264_convert_character();
    std::uint16_t p21274_decode_character();
    std::uint16_t p21275_encode_character();
    std::uint16_t p21431_buffer_char();
    std::uint16_t p21443_advance_descriptor();
    std::uint16_t p25346_begin_character_output();
    std::uint16_t p25223();
    std::uint16_t p25350_continue_character_output();
    std::uint16_t p25361_resume_character_output();
    std::uint16_t p25364_return_character_output();
    std::uint16_t p25356_emit_end_character();
    std::uint16_t p25370_begin_token_source();
    std::uint16_t p25373_resume_token_source();
    std::uint16_t p25376_fetch_token_character();
    std::uint16_t p25377_finish_token_source();
    std::uint16_t p25421();
    std::uint16_t p25424();
    std::uint16_t p25427();
    std::uint16_t p25556();
    std::uint16_t p25641();
    std::uint16_t p25660();

private:
    static std::uint16_t address_add(std::uint16_t value, int delta)
    {
        return static_cast<std::uint16_t>((value + delta) & 077777);
    }

    std::uint16_t register_value(std::size_t index) const
    {
        return index == 0 ? 0 : registers_[index];
    }

    void set_register(std::size_t index, std::uint16_t value)
    {
        if (index != 0) {
            registers_[index] = value;
        }
    }

    static Word48 cyclic_add(Word48 left, Word48 right);
    static Word48 logical_shift(Word48 value, int count);
    static Word48 pack_bits(Word48 value, Word48 mask);
    static Word48 unpack_bits(Word48 value, Word48 mask);
    void shift_accumulator(int count);
    void select_alu_group(std::uint8_t group);
    bool accumulator_condition() const;
    void normalize_and_round(std::int64_t mantissa, int exponent,
                             std::uint64_t low, bool round);
    void multiply(Word48 value);
    void arithmetic_add(Word48 value, bool negate_accumulator,
                        bool negate_value);
    void divide(Word48 value);
    void elementary_function(std::uint16_t function);
    void add_exponent(int delta);
    void change_sign(bool negate);
    void yta(int exponent_delta);
    void reverse_subtract(Word48 value);
    void modifier_add(std::size_t destination, std::size_t source);
    void hardware_push_acc();
    void hardware_pop_acc();
    void its(std::size_t index);
    void xts(std::uint16_t address);
    void sti(std::size_t index);
    void stx(std::uint16_t address);
    bool native_character_output_active() const;
    void append_native_output_byte(std::uint8_t character);
    void output_native_character(std::uint8_t character);
    std::uint16_t p06727_finish_arithmetic();
    std::uint16_t p01122_shared();
    std::uint16_t p01011();
    std::uint16_t p01012();
    std::uint16_t p01016();
    std::uint16_t p01023();
    std::uint16_t p01024();
    std::uint16_t p01025();
    std::uint16_t p01026();
    std::uint16_t p01140();
    std::uint16_t p01151();
    std::uint16_t p01154_finish();
    std::uint16_t p01160_finish();
    std::uint16_t p03336();
    std::uint16_t p03375();
    std::uint16_t p03376();
    std::uint16_t p03532();
    std::uint16_t p03534();
    std::uint16_t p05240();
    std::uint16_t p05250();
    std::uint16_t p05252();
    std::uint16_t p05254();
    std::uint16_t p05255();
    std::uint16_t p05261();
    std::uint16_t p05266();
    std::uint16_t p05273();
    std::uint16_t p05314();
    std::uint16_t p05316();
    std::uint16_t p05326();
    std::uint16_t p05334();
    std::uint16_t p05335();
    std::uint16_t p05336();
    std::uint16_t p05337();
    std::uint16_t p05341();
    std::uint16_t p07473();
    std::uint16_t p07536();
    std::uint16_t p11525();
    std::uint16_t p11526();
    std::uint16_t p11527();
    std::uint16_t p11530();
    std::uint16_t p13010();
    std::uint16_t p13013();
    std::uint16_t p13014();
    std::uint16_t p13015();
    std::uint16_t p13016();
    std::uint16_t p13031();
    std::uint16_t p13041();
    std::uint16_t p13043();
    std::uint16_t p13044();
    std::uint16_t p13046();
    std::uint16_t p13057();
    std::uint16_t p13062();
    std::uint16_t p13113();
    std::uint16_t p13115();
    std::uint16_t p13117();
    std::uint16_t p13172();
    std::uint16_t p16153();
    std::uint16_t p16157();
    std::uint16_t p16161();
    std::uint16_t p16166();
    std::uint16_t p16171();
    std::uint16_t p16201();
    std::uint16_t p16207();
    std::uint16_t p16210();
    std::uint16_t p16215();
    std::uint16_t p16216();
    std::uint16_t p16222();
    std::uint16_t p16412();
    std::uint16_t p16413();
    std::uint16_t p16415();
    std::uint16_t p16416();
    std::uint16_t p16471();
    std::uint16_t p16022();
    std::uint16_t p16026();
    std::uint16_t p20150();
    std::uint16_t p20152();
    std::uint16_t p20155();
    std::uint16_t p20161();
    std::uint16_t p20462();
    std::uint16_t p20501();
    std::uint16_t p20506();
    std::uint16_t p20511();
    std::uint16_t p20514();
    std::uint16_t p25225();
    std::uint16_t p25226();
    std::uint16_t p17575();
    std::uint16_t p17632();
    std::uint16_t p17633();
    std::uint16_t p17636();
    std::uint16_t p17646();
    std::uint16_t p17654();
    std::uint16_t p17655();
    std::uint16_t p17700();
    std::uint16_t p17717();
    std::uint16_t p17722();
    std::uint16_t p17726();
    std::uint16_t p17730();
    std::uint16_t p17735();
    std::uint16_t p17740();
    std::uint16_t p17744();
    std::uint16_t p17747();
    std::uint16_t p17753();
    std::uint16_t p17756();
    std::uint16_t p25434();
    std::uint16_t p25437();
    std::uint16_t p25445();
    std::uint16_t p25446();
    std::uint16_t p25450();
    std::uint16_t p25451();
    std::uint16_t p25457();
    std::uint16_t p25460();
    std::uint16_t p25463();
    std::uint16_t p25470();
    std::uint16_t p25471();
    std::uint16_t p25475();
    std::uint16_t p25500();
    std::uint16_t p25502();
    std::uint16_t p25506();
    std::uint16_t p25507();
    std::uint16_t p25647();
    std::uint16_t p25653();
    std::uint16_t p25655();
    std::uint16_t p03704();
    std::uint16_t p03706();
    std::uint16_t p03707();
    std::uint16_t p03710();
    std::uint16_t p03725();
    std::uint16_t p03726();
    std::uint16_t p03741();
    std::uint16_t p03742();
    std::uint16_t p03743();
    std::uint16_t p03750();
    std::uint16_t p03751();
    std::uint16_t p03754();
    std::uint16_t p03757();
    std::uint16_t p03761();
    std::uint16_t p04076();
    std::uint16_t p04100();
    std::uint16_t p04101();
    std::uint16_t p04102();
    std::uint16_t p04103();
    std::uint16_t p04164();
    std::uint16_t p04166();
    std::uint16_t p04167();
    std::uint16_t p04173();
    std::uint16_t p04175();
    std::uint16_t p04177();
    std::uint16_t p04200();
    std::uint16_t p04201();
    std::uint16_t p04202();
    std::uint16_t p04210();
    std::uint16_t p04212();
    std::uint16_t p04213();
    std::uint16_t p04330();
    std::uint16_t p04334();
    std::uint16_t p04335();
    std::uint16_t p04343();
    std::uint16_t p04350();
    std::uint16_t p04351();
    std::uint16_t p04352();
    std::uint16_t p04353();
    std::uint16_t p04354();
    std::uint16_t p04455();
    std::uint16_t p04471();
    std::uint16_t p04503();
    std::uint16_t p04504();
    std::uint16_t p04513();
    std::uint16_t p04541();
    std::uint16_t p04544();
    std::uint16_t p04561();
    std::uint16_t p04571();
    std::uint16_t p04572();
    std::uint16_t p04576();
    std::uint16_t p04577();
    std::uint16_t p04600();
    std::uint16_t p04601();
    std::uint16_t p04602();
    std::uint16_t p04605();
    std::uint16_t p04607();
    std::uint16_t p04610();
    std::uint16_t p04611();
    std::uint16_t p04612();
    std::uint16_t p04614();
    std::uint16_t p04615();
    std::uint16_t p04623();
    std::uint16_t p04667();
    std::uint16_t p04670();
    std::uint16_t p04673();
    std::uint16_t p04677();
    std::uint16_t p04702();
    std::uint16_t p04706();
    std::uint16_t p04707();
    std::uint16_t p04710();
    std::uint16_t p04711();
    std::uint16_t p04715();
    std::uint16_t p04717();
    std::uint16_t p04722();
    std::uint16_t p04725();
    std::uint16_t p04726();
    std::uint16_t p04730();
    std::uint16_t p04731();
    std::uint16_t p04734();
    std::uint16_t p04736();
    std::uint16_t p04737();
    std::uint16_t p04742();
    std::uint16_t p04746();
    std::uint16_t p04750();
    std::uint16_t p04751();
    std::uint16_t p04753();
    std::uint16_t p04755();
    std::uint16_t p04756();
    std::uint16_t p05014();
    std::uint16_t p05017();
    std::uint16_t p05021();
    std::uint16_t p05022();
    std::uint16_t p05026();
    std::uint16_t p05030();
    std::uint16_t p05034();
    std::uint16_t p05035();
    std::uint16_t p05040();
    std::uint16_t p05045();
    std::uint16_t p05052_dispatch_generated_instruction(
        std::uint16_t entry);
    std::uint16_t p05124_store_generated_instruction();
    std::uint16_t p05125_advance_generated_instruction();
    std::uint16_t p05126_load_generated_instruction();
    std::uint16_t p05127_mask_generated_instruction();
    std::uint16_t p05130_finish_generated_instruction();
    std::uint16_t p05131_pack_generated_instruction();
    std::uint16_t p05135_add_generated_instruction();
    std::uint16_t p05143_begin_empty_generated_instruction();
    std::uint16_t p05147_finish_empty_generated_instruction();
    std::uint16_t p05151_restore_generated_instruction();
    std::uint16_t p05163();
    std::uint16_t p05217();
    std::uint16_t p05414();
    std::uint16_t p05416();
    std::uint16_t p05422();
    std::uint16_t p05425();
    std::uint16_t p05427();
    std::uint16_t p06734_error();
    std::uint16_t p06735_add();
    std::uint16_t p06741_error();
    std::uint16_t p06742_subtract();
    std::uint16_t p06745_error();
    std::uint16_t p06746_multiply();
    std::uint16_t p06751_error();
    std::uint16_t p06752_divide();
    std::uint16_t p06141();
    std::uint16_t p06346();
    std::uint16_t p06345();
    std::uint16_t p06352();
    std::uint16_t p06354();
    std::uint16_t p06360();
    std::uint16_t p06363();
    std::uint16_t p06367();
    std::uint16_t p06372();
    std::uint16_t p06374();
    std::uint16_t p06375();
    std::uint16_t p06401();
    std::uint16_t p06403();
    std::uint16_t p06435();
    std::uint16_t p06530();
    std::uint16_t p06534();
    std::uint16_t p06536();
    std::uint16_t p06545();
    std::uint16_t p06556();
    std::uint16_t p06560();
    std::uint16_t p06561();
    std::uint16_t p07703();
    std::uint16_t p07704();
    std::uint16_t p07706();
    std::uint16_t p07707();
    std::uint16_t p07710();
    std::uint16_t p07711();
    std::uint16_t p07712();
    std::uint16_t p07714();
    std::uint16_t p07715();
    std::uint16_t p07716();
    std::uint16_t p07720();
    std::uint16_t p07722();
    std::uint16_t p07724();
    std::uint16_t p07725();
    std::uint16_t p07726();
    std::uint16_t p07727();
    std::uint16_t p07730();
    std::uint16_t p07734();
    std::uint16_t p07737();
    std::uint16_t p07741();
    std::uint16_t p07742();
    std::uint16_t p07746();
    std::uint16_t p07751();
    std::uint16_t p07752();
    std::uint16_t p11471();
    std::uint16_t p11474();
    std::uint16_t p13066();
    std::uint16_t p13071();
    std::uint16_t p13076();
    std::uint16_t p13125();
    std::uint16_t p13127();
    std::uint16_t p16347();
    std::uint16_t p16350();
    std::uint16_t p16351_dispatch_lookup_result(std::uint16_t entry);
    std::uint16_t p16367_initialize_record_tables();
    std::uint16_t p16370_clear_record_table();
    std::uint16_t p16372_begin_record_shift();
    std::uint16_t p16373_push_record_head();
    std::uint16_t p16374_push_record_value();
    std::uint16_t p16375_push_alternate_record_value();
    std::uint16_t p16376();
    std::uint16_t p16402_select_nonempty_record();
    std::uint16_t p16403_select_empty_record();
    std::uint16_t p16404_prepare_record_evaluation();
    std::uint16_t p16405_enter_record_evaluation();
    std::uint16_t p16417();
    std::uint16_t p16420();
    std::uint16_t p16502();
    std::uint16_t p16503();
    std::uint16_t p16606();
    std::uint16_t p16744();
    std::uint16_t p06623_generated_compare(std::uint16_t link,
                                           std::uint16_t selector,
                                           bool reverse_subtraction,
                                           bool exchange_results);
    std::uint16_t p06657();
    std::uint16_t p06660();
    std::uint16_t p06661();
    std::uint16_t p17015();
    std::uint16_t p17023();
    std::uint16_t p17047();
    std::uint16_t p17072();
    std::uint16_t p17254_shared();
    std::uint16_t p17260();
    std::uint16_t p17266();
    std::uint16_t p17275_shared();
    std::uint16_t p17302();
    std::uint16_t p17306();
    std::uint16_t p17423();
    std::uint16_t p17424();
    std::uint16_t p17425();
    std::uint16_t p17426();
    std::uint16_t p17430();
    std::uint16_t p17432();
    std::uint16_t p17435();
    std::uint16_t p17436();
    std::uint16_t p17440();
    std::uint16_t p17442();
    std::uint16_t p17451();
    std::uint16_t p17452();
    std::uint16_t p17453();
    std::uint16_t p17454();
    std::uint16_t p17461();
    std::uint16_t p17504();
    std::uint16_t p17505();
    std::uint16_t p17514();
    std::uint16_t p17516();
    std::uint16_t p17517();
    std::uint16_t p17520();
    std::uint16_t p17521();
    std::uint16_t p17522();
    std::uint16_t p17523();
    std::uint16_t p17526();
    std::uint16_t p17531();
    std::uint16_t p17535();
    std::uint16_t p17536();
    std::uint16_t p17543();
    std::uint16_t p17546();
    std::uint16_t p17547();
    std::uint16_t p17606();
    std::uint16_t p17764();
    std::uint16_t p20002();
    std::uint16_t p20101();
    std::uint16_t p21473();
    std::uint16_t p21476();
    std::uint16_t p21501();
    std::uint16_t p21502();
    std::uint16_t p21510();
    std::uint16_t p21511();
    std::uint16_t p21516();
    std::uint16_t p17243_scan();
    std::uint8_t memory_byte(std::uint16_t address,
                             std::size_t byte_index) const;
    void set_memory_byte(std::uint16_t address, std::size_t byte_index,
                         std::uint8_t value);
    bool dispatch_translated_routine();

    Word48 accumulator_;
    Word48 remainder_;
    std::uint8_t alu_mode_ = 0;
    std::array<std::uint16_t, 020> registers_{};
    std::array<Word48, core_words> memory_{};
    bool console_available_ = true;
    std::deque<std::vector<std::uint8_t>> console_input_;
    std::vector<std::uint8_t> console_output_;
    std::uint16_t program_counter_ = 1;
    bool right_half_ = false;
    std::uint16_t instruction_modifier_ = 0;
    std::uint64_t instruction_count_ = 0;
    std::uint64_t translated_routine_count_ = 0;
    std::chrono::steady_clock::time_point execution_started_at_ =
        std::chrono::steady_clock::now();
    bool translated_routines_enabled_ = true;
    std::vector<std::uint16_t> disabled_translated_routines_;
};

} // namespace poplan

#endif
