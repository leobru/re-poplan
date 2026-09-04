#include "poplan/machine.hpp"

#include <ctime>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::uint64_t whole_second_jiffies(std::time_t time)
{
    const std::tm *local = std::localtime(&time);
    require(local != nullptr, "the test can determine local time");
    return static_cast<std::uint64_t>(
        (local->tm_hour * 60 + local->tm_min) * 60 + local->tm_sec) * 50;
}

std::uint64_t circular_distance(std::uint64_t left, std::uint64_t right,
                                std::uint64_t modulus)
{
    const std::uint64_t forward = (left + modulus - right) % modulus;
    const std::uint64_t backward = (right + modulus - left) % modulus;
    return forward < backward ? forward : backward;
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

    // POPLAN's compiler emits code through extracode 075, then branches to
    // the newly written instruction words.
    Machine e75_store;
    constexpr std::uint32_t e75_left = (075U << 12) | 01234U;
    constexpr std::uint32_t stop_right =
        (1U << 19) | (0330U << 12);
    e75_store.memory(01000) = Word48(
        (static_cast<std::uint64_t>(e75_left) << 24) | stop_right);
    e75_store.accumulator() = Word48(07246563567103301ULL);
    e75_store.start(01000);
    require(e75_store.step() == poplan::ExecutionStatus::running,
            "E75 execution continues with the right half");
    require(e75_store.memory(01234)
                == Word48(07246563567103301ULL),
            "E75 stores the accumulator in executable memory");
    require(e75_store.reg(016) == 01234,
            "E75 exposes its effective address in r16");
    require(e75_store.step() == poplan::ExecutionStatus::halted,
            "the synthetic E75 program reaches STOP");

    struct E50Case {
        std::uint16_t address;
        Word48 operand;
        Word48 expected;
    };
    const E50Case elementary_function_cases[] = {
        {0, Word48(), Word48()},
        {0, Word48(04050000000000000ULL), Word48(04050000000000000ULL)},
        {0, Word48(04110000000000000ULL), Word48(04053240474631771ULL)},
        {0, Word48(04150000000000000ULL), Word48(04110000000000000ULL)},
        {1, Word48(04050000000000000ULL), Word48(04015355251074110ULL)},
        {1, Word48(04020000000000000ULL), Word48(04022422526703670ULL)},
        {2, Word48(04050000000000000ULL), Word48(04010512120076650ULL)},
        {3, Word48(04050000000000000ULL), Word48(04014441766521041ULL)},
        {4, Word48(04010000000000000ULL), Word48(04010301244340553ULL)},
        {4, Word48(04030000000000000ULL), Word48(04027476533437225ULL)},
        {5, Word48(04154000000000000ULL), Word48(04056253027750521ULL)},
        {5, Word48(04010000000000000ULL), Word48(04024721572004057ULL)},
        {6, Word48(04050000000000000ULL), Word48(04112677025054242ULL)},
        {6, Word48(04020000000000000ULL), Word48(03753613254330547ULL)},
    };
    for (const auto &[address, operand, expected]
         : elementary_function_cases) {
        const std::uint32_t e50_left = (050U << 12) | address;
        auto machine = std::make_unique<Machine>();
        machine->memory(01000) = Word48(
            (static_cast<std::uint64_t>(e50_left) << 24) | stop_right);
        machine->accumulator() = operand;
        machine->remainder() = Word48(0765432107654321ULL);
        machine->alu_mode() = 020;
        machine->start(01000);
        require(machine->step() == poplan::ExecutionStatus::running,
                "E50 elementary function continues with the right half");
        require(machine->accumulator() == expected,
                "E50 computes the traced BESM-6 elementary function");
        require(machine->remainder() == Word48()
                    && machine->reg(016) == address
                    && machine->alu_mode() == 004,
                "E50 clears RMR and exposes its effective address");
    }

    auto e53_time = std::make_unique<Machine>();
    constexpr std::uint32_t e53_left = (053U << 12) | 010U;
    e53_time->memory(01000) = Word48(
        (static_cast<std::uint64_t>(e53_left) << 24) | stop_right);
    const std::time_t time_before = std::time(nullptr);
    e53_time->start(01000);
    require(e53_time->step() == poplan::ExecutionStatus::running,
            "E53/010 execution continues with the right half");
    const std::time_t time_after = std::time(nullptr);
    constexpr std::uint64_t jiffies_per_day = 24 * 60 * 60 * 50;
    const std::uint64_t actual_jiffies = e53_time->accumulator().raw();
    require(actual_jiffies < jiffies_per_day,
            "E53/010 returns a time within the local day");
    const bool matches_clock =
        circular_distance(actual_jiffies,
                          whole_second_jiffies(time_before),
                          jiffies_per_day) < 50
        || circular_distance(actual_jiffies,
                             whole_second_jiffies(time_after),
                             jiffies_per_day) < 50;
    require(matches_clock,
            "E53/010 returns current local time in 1/50-second jiffies");
    require(e53_time->reg(016) == 010,
            "E53/010 exposes its effective address in r16");

    auto e63_time = std::make_unique<Machine>();
    constexpr std::uint32_t e63_left = (063U << 12) | 04U;
    e63_time->memory(01000) = Word48(
        (static_cast<std::uint64_t>(e63_left) << 24) | stop_right);
    e63_time->accumulator() = Word48(07777777777777777ULL);
    e63_time->start(01000);
    require(e63_time->step() == poplan::ExecutionStatus::running,
            "E63/004 execution continues with the right half");
    require(e63_time->accumulator().raw() < 50,
            "E63/004 returns elapsed execution time in 1/50-second jiffies");
    require(e63_time->reg(016) == 04,
            "E63/004 exposes its effective address in r16");

    auto e64_ignored = std::make_unique<Machine>();
    constexpr std::uint32_t e64_left = (064U << 12) | 07536U;
    e64_ignored->memory(01000) = Word48(
        (static_cast<std::uint64_t>(e64_left) << 24) | stop_right);
    e64_ignored->accumulator() = Word48(07100000000012345ULL);
    e64_ignored->remainder() = Word48(07654);
    e64_ignored->alu_mode() = 020;
    e64_ignored->start(01000);
    require(e64_ignored->step() == poplan::ExecutionStatus::running,
            "E64 execution continues with the right half");
    require(e64_ignored->accumulator()
                == Word48(07100000000012345ULL)
                && e64_ignored->remainder() == Word48(07654)
                && e64_ignored->reg(016) == 07536
                && e64_ignored->alu_mode() == 004,
            "ignored E64 preserves data state and exposes its address");

    auto e74_exit = std::make_unique<Machine>();
    constexpr std::uint32_t e74_left = (074U << 12);
    e74_exit->memory(01000) = Word48(
        static_cast<std::uint64_t>(e74_left) << 24);
    e74_exit->accumulator() = Word48(012345);
    e74_exit->alu_mode() = 020;
    e74_exit->start(01000);
    require(e74_exit->step() == poplan::ExecutionStatus::halted
                && e74_exit->accumulator() == Word48(012345)
                && e74_exit->reg(016) == 0
                && e74_exit->alu_mode() == 004,
            "E74/000 terminates image execution without changing data");

    auto numeric_update = std::make_unique<Machine>();
    numeric_update->accumulator() = Word48(06400000000000006ULL);
    numeric_update->reg(015) = 011710;
    numeric_update->reg(017) = 066064;
    numeric_update->memory(066063) =
        Word48(06400000000000024ULL);
    numeric_update->memory(03453) =
        Word48(06400000000000000ULL);
    numeric_update->memory(03457) =
        Word48(00751003537173474ULL);
    require(numeric_update->p03413_numeric_update() == 011710,
            "03413 returns through the traced r15 link");
    require(numeric_update->accumulator()
                == Word48(06400000000000003ULL)
                && numeric_update->memory(03451)
                    == Word48(06400000000000003ULL),
            "03413 reproduces the traced numeric update");
    require(numeric_update->remainder() == Word48()
                && numeric_update->alu_mode() == 007
                && numeric_update->reg(013) == 03310
                && numeric_update->reg(017) == 066064,
            "03413 preserves the traced arithmetic and stack state");

    auto allocate_two_words = std::make_unique<Machine>();
    allocate_two_words->accumulator() = Word48(2);
    allocate_two_words->reg(015) = 05433;
    allocate_two_words->reg(017) = 066004;
    allocate_two_words->memory(066003) = Word48(2);
    allocate_two_words->memory(06102) = Word48(021);
    allocate_two_words->memory(06103) = Word48(077777);
    allocate_two_words->memory(06104) =
        Word48(07777777770000000ULL);
    allocate_two_words->memory(05502) = Word48(033064);
    allocate_two_words->memory(033064) =
        Word48(0003271400000000ULL);
    allocate_two_words->memory(02213) =
        Word48(06500000000000000ULL);
    require(allocate_two_words->p05447() == 05433,
            "05447 returns through the traced allocator link");
    require(allocate_two_words->reg(016) == 065776
                && allocate_two_words->memory(033064)
                    == Word48(0003271200000000ULL),
            "05447 splits the traced common-list block");
    require(allocate_two_words->memory(065776)
                == Word48(06500000000000000ULL)
                && allocate_two_words->memory(065777)
                    == Word48(06500000000000000ULL),
            "05447 initializes every allocated word");

    auto allocator_return = std::make_unique<Machine>();
    allocator_return->reg(017) = 066004;
    allocator_return->memory(066002) = Word48(016022);
    allocator_return->memory(066003) = Word48(2);
    require(allocator_return->p05436() == 016022
                && allocator_return->reg(017) == 066002,
            "05436 restores the saved caller through stacked WTC addressing");

    auto zero_allocation = std::make_unique<Machine>();
    zero_allocation->reg(015) = 01234;
    zero_allocation->reg(016) = 0;
    zero_allocation->reg(017) = 066004;
    require(zero_allocation->p05430() == 01234
                && zero_allocation->reg(017) == 066004,
            "05430 returns a zero-size allocation without changing the stack");

    auto exhausted_allocator = std::make_unique<Machine>();
    exhausted_allocator->accumulator() = Word48(021);
    exhausted_allocator->reg(015) = 05433;
    exhausted_allocator->reg(017) = 066004;
    exhausted_allocator->memory(066003) = Word48(021);
    exhausted_allocator->memory(06102) = Word48(021);
    exhausted_allocator->memory(05502) = Word48();
    require(exhausted_allocator->p05447() == 05433
                && exhausted_allocator->reg(016) == 0,
            "05447 reports exhaustion when the common free list is empty");

    auto generated_binding = std::make_unique<Machine>();
    generated_binding->accumulator() =
        Word48(02400000000000050ULL);
    generated_binding->reg(010) = 011506;
    generated_binding->reg(015) = 011710;
    generated_binding->reg(017) = 066031;
    generated_binding->memory(066026) =
        Word48(07100000000065741ULL);
    generated_binding->memory(066030) =
        Word48(06400000000000000ULL);
    generated_binding->memory(066025) = Word48(011760);
    generated_binding->memory(065742) =
        Word48(00244312523441524ULL);
    generated_binding->memory(011762) = Word48(0377);
    generated_binding->memory(012000) =
        Word48(06400000000000000ULL);
    require(generated_binding->p11720_finish_generated_binding() == 03275,
            "11720 tail-enters the traced PUSH_ACC bracket");
    require(generated_binding->accumulator()
                == Word48(06400000000000012ULL)
                && generated_binding->reg(016) == 050
                && generated_binding->reg(014) == 065742
                && generated_binding->reg(015) == 03235
                && generated_binding->reg(017) == 066022,
            "11720 reproduces the traced generated character binding");

    auto token_fast_path = std::make_unique<Machine>();
    token_fast_path->accumulator() = Word48(07473);
    token_fast_path->reg(015) = 021253;
    token_fast_path->reg(017) = 066037;
    token_fast_path->memory(025420) = Word48(1);
    require(token_fast_path->p25370_begin_token_source() == 025376,
            "25370 skips refill when packed input is available");
    require(token_fast_path->reg(017) == 066041
                && token_fast_path->memory(066037) == Word48(07473)
                && token_fast_path->memory(066040) == Word48(021253),
            "25370 builds the traced two-word token frame");

    auto character_extract_resume = std::make_unique<Machine>();
    character_extract_resume->accumulator() = Word48(0100);
    character_extract_resume->reg(017) = 066040;
    character_extract_resume->memory(066037) = Word48(07473);
    require(character_extract_resume->p21253_resume_character_extract()
                == 021274,
            "21253 resumes at the input conversion entry");
    require(character_extract_resume->accumulator() == Word48(0100)
                && character_extract_resume->reg(015) == 07473
                && character_extract_resume->reg(017) == 066037,
            "21253 restores its traced caller and character");

    auto input_prepare = std::make_unique<Machine>();
    input_prepare->reg(015) = 025373;
    input_prepare->memory(020377) = Word48(012);
    input_prepare->memory(025417) =
        Word48(01400000000020440ULL);
    input_prepare->memory(020361) =
        Word48(01400000000020440ULL);
    require(input_prepare->p20170_input_primary() == 020177,
            "20170 selects the traced ready descriptor path");

    auto input_refill = std::make_unique<Machine>();
    input_refill->reg(015) = 025373;
    input_refill->reg(017) = 066004;
    input_refill->memory(020377) = Word48(012);
    input_refill->memory(025417) = Word48(1);
    input_refill->memory(020361) = Word48(2);
    require(input_refill->p20170_input_primary() == 025356
                && input_refill->reg(015) == 020175
                && input_refill->reg(017) == 066005
                && input_refill->memory(066004) == Word48(025373),
            "20170 preserves its caller before emitting the refill terminator");

    input_prepare->memory(020362) = Word48();
    require(input_prepare->p20177_continue_input_primary() == 020202,
            "20177 selects a fresh input transfer");
    input_prepare->memory(020372) =
        Word48(00777740000000000ULL);
    input_prepare->memory(020374) = Word48();
    input_prepare->memory(020363) =
        Word48(04034021041120234ULL);
    require(input_prepare->p20202_prepare_input_transfer() == 020205
                && input_prepare->memory(020400)
                    == Word48(00777740000000000ULL)
                && input_prepare->memory(020364)
                    == Word48(04034021041120234ULL),
            "20202 builds the traced blocking E71 control word");

    auto semantic_push = std::make_unique<Machine>();
    semantic_push->reg(006) = 070000;
    semantic_push->reg(015) = 01234;
    semantic_push->accumulator() = Word48(06400000000000001ULL);
    semantic_push->start(03275);
    require(semantic_push->step() == poplan::ExecutionStatus::running,
            "translated routine dispatch is an executable machine step");
    require(semantic_push->program_counter() == 01234
                && !semantic_push->right_half(),
            "translated PUSH_ACC returns through the BESM link");
    require(semantic_push->reg(006) == 067777
                && semantic_push->memory(067777)
                    == Word48(06400000000000001ULL),
            "translated PUSH_ACC replaces its instruction sequence");
    require(semantic_push->translated_routine_count() == 1,
            "the machine records semantic routine dispatches");

    auto high_address_dispatch = std::make_unique<Machine>();
    high_address_dispatch->memory(025407) = Word48(0377);
    high_address_dispatch->start(025356);
    require(high_address_dispatch->step()
                == poplan::ExecutionStatus::running
                && high_address_dispatch->program_counter() == 025346
                && high_address_dispatch->accumulator() == Word48(0377)
                && high_address_dispatch->translated_routine_count() == 1,
            "five-digit octal entries above 10000 dispatch semantically");

    auto right_half_entry = std::make_unique<Machine>();
    right_half_entry->accumulator() = Word48(012345);
    right_half_entry->start(03275, true);
    require(right_half_entry->step() == poplan::ExecutionStatus::running,
            "a right-half entry still executes one BESM instruction");
    require(right_half_entry->translated_routine_count() == 0
                && right_half_entry->program_counter() == 03276,
            "semantic dispatch only owns a routine's left-half entry");

    auto interpreted_push = std::make_unique<Machine>();
    constexpr std::uint32_t push_left =
        (6U << 20) | (1U << 19) | (0250U << 12) | 077777U;
    interpreted_push->memory(03275) = Word48(
        static_cast<std::uint64_t>(push_left) << 24);
    interpreted_push->reg(006) = 070000;
    interpreted_push->start(03275);
    interpreted_push->set_translated_routines_enabled(false);
    require(interpreted_push->step() == poplan::ExecutionStatus::running,
            "instruction fallback remains available for comparison");
    require(interpreted_push->reg(006) == 067777
                && interpreted_push->program_counter() == 03275
                && interpreted_push->right_half(),
            "interpret-only mode executes one half-instruction at a time");
    require(interpreted_push->translated_routine_count() == 0,
            "interpret-only mode bypasses translated routines");

    auto zero_index_load = std::make_unique<Machine>();
    constexpr std::uint32_t zero_index_xta =
        (010U << 12) | 01234U;
    zero_index_load->memory(02000) = Word48(
        static_cast<std::uint64_t>(zero_index_xta) << 24);
    zero_index_load->memory(01234) = Word48(07654321);
    zero_index_load->memory(01246) = Word48(01111111);
    zero_index_load->reg(0) = 012;
    zero_index_load->start(02000);
    zero_index_load->set_translated_routines_enabled(false);
    require(zero_index_load->step() == poplan::ExecutionStatus::running
                && zero_index_load->accumulator() == Word48(07654321)
                && zero_index_load->reg(0) == 012,
            "r0 contributes zero to effective addresses without repair");

    auto zero_destination = std::make_unique<Machine>();
    constexpr std::uint32_t vtm_r0 =
        (1U << 19) | (0240U << 12) | 0777U;
    zero_destination->memory(02000) = Word48(
        static_cast<std::uint64_t>(vtm_r0) << 24);
    zero_destination->reg(0) = 012;
    zero_destination->start(02000);
    zero_destination->set_translated_routines_enabled(false);
    require(zero_destination->step() == poplan::ExecutionStatus::running
                && zero_destination->reg(0) == 012,
            "writes to architectural r0 are ignored without clearing storage");

    auto zero_register_read = std::make_unique<Machine>();
    constexpr std::uint32_t vta_r0 = 042U << 12;
    zero_register_read->memory(02000) = Word48(
        static_cast<std::uint64_t>(vta_r0) << 24);
    zero_register_read->reg(0) = 012;
    zero_register_read->accumulator() = Word48(0777);
    zero_register_read->start(02000);
    zero_register_read->set_translated_routines_enabled(false);
    require(zero_register_read->step() == poplan::ExecutionStatus::running
                && zero_register_read->accumulator() == Word48()
                && zero_register_read->reg(0) == 012,
            "register-source instructions read architectural r0 as zero");

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

    auto stack_value_push = std::make_unique<Machine>();
    stack_value_push->reg(006) = 070000;
    stack_value_push->reg(015) = 04567;
    stack_value_push->reg(016) = 04000;
    stack_value_push->memory(04000) =
        Word48(06400000000000123ULL);
    require(stack_value_push->p03301() == 04567
                && stack_value_push->accumulator()
                    == Word48(06400000000000123ULL)
                && stack_value_push->reg(006) == 067777
                && stack_value_push->memory(067777)
                    == Word48(06400000000000123ULL),
            "03301 loads through r16 and pushes the value on the POP stack");

    auto store_stack_top = std::make_unique<Machine>();
    store_stack_top->accumulator() =
        Word48(07100000000012345ULL);
    store_stack_top->alu_mode() = 020;
    store_stack_top->reg(006) = 067777;
    store_stack_top->reg(015) = 05670;
    store_stack_top->reg(016) = 04100;
    store_stack_top->reg(017) = 066000;
    store_stack_top->memory(067777) =
        Word48(06400000000000456ULL);
    require(store_stack_top->p03303_store_stack_top() == 05670
                && store_stack_top->memory(04100)
                    == Word48(06400000000000456ULL)
                && store_stack_top->accumulator()
                    == Word48(07100000000012345ULL)
                && store_stack_top->reg(006) == 070000
                && store_stack_top->reg(017) == 066000
                && store_stack_top->alu_mode() == 004,
            "03303 stores the POP top while balancing the hardware stack");

    auto indirect_loads = std::make_unique<Machine>();
    indirect_loads->reg(015) = 06701;
    indirect_loads->accumulator() =
        Word48(06400000000042000ULL);
    indirect_loads->memory(042000) =
        Word48(07100000000000007ULL);
    indirect_loads->memory(042001) =
        Word48(07200000000000011ULL);
    require(indirect_loads->p05207() == 06701
                && indirect_loads->reg(016) == 042000
                && indirect_loads->accumulator()
                    == Word48(07100000000000007ULL),
            "05207 loads word zero through the accumulator address");
    indirect_loads->accumulator() =
        Word48(06500000000042000ULL);
    require(indirect_loads->p05211() == 06701
                && indirect_loads->reg(016) == 042000
                && indirect_loads->accumulator()
                    == Word48(07200000000000011ULL),
            "05211 loads word one through the accumulator address");

    auto semantic_indirect_load = std::make_unique<Machine>();
    semantic_indirect_load->reg(015) = 07001;
    semantic_indirect_load->accumulator() = Word48(04300);
    semantic_indirect_load->memory(04301) = Word48(07654);
    semantic_indirect_load->start(05211);
    require(semantic_indirect_load->step()
                == poplan::ExecutionStatus::running
                && semantic_indirect_load->program_counter() == 07001
                && semantic_indirect_load->accumulator() == Word48(07654)
                && semantic_indirect_load->translated_routine_count() == 1,
            "05211 is selected by semantic dispatch");

    auto indirect_evaluator = std::make_unique<Machine>();
    indirect_evaluator->reg(015) = 07000;
    indirect_evaluator->reg(016) = 04000;
    indirect_evaluator->memory(04000) =
        Word48(06601175000011755ULL);
    indirect_evaluator->memory(011752) =
        Word48(0660000000011756ULL);
    indirect_evaluator->memory(03007) =
        Word48(07700000000000000ULL);
    indirect_evaluator->memory(03012) =
        Word48(06600000000000000ULL);
    indirect_evaluator->memory(03011) =
        Word48(06500000000000000ULL);
    indirect_evaluator->start(02770);
    require(indirect_evaluator->step()
                == poplan::ExecutionStatus::running
                && indirect_evaluator->program_counter() == 02750
                && indirect_evaluator->reg(010) == 02745
                && indirect_evaluator->reg(016) == 011750
                && indirect_evaluator->accumulator()
                    == Word48(0660000000011756ULL),
            "02770 validates an indirect function and selects its environment");

    auto frame_index = std::make_unique<Machine>();
    frame_index->reg(015) = 07001;
    frame_index->reg(017) = 066001;
    frame_index->accumulator() = Word48(04000);
    frame_index->memory(066000) = Word48(3);
    frame_index->memory(04003) = Word48(07654321);
    frame_index->start(011500);
    require(frame_index->step() == poplan::ExecutionStatus::running
                && frame_index->program_counter() == 07001
                && frame_index->reg(016) == 04000
                && frame_index->reg(017) == 066000
                && frame_index->accumulator() == Word48(07654321),
            "11500 indexes through frame word -1 and consumes it");

    auto arithmetic_finish = std::make_unique<Machine>();
    arithmetic_finish->reg(010) = 06712;
    arithmetic_finish->reg(011) = 06734;
    arithmetic_finish->reg(013) = 0;
    arithmetic_finish->reg(015) = 063752;
    arithmetic_finish->reg(017) = 066025;
    arithmetic_finish->accumulator() = Word48();
    arithmetic_finish->remainder() =
        Word48(04000000000000000ULL);
    arithmetic_finish->alu_mode() = 022;
    arithmetic_finish->memory(01637) =
        Word48(06400000000000000ULL);
    arithmetic_finish->memory(06755) =
        Word48(0560000000000000ULL);
    arithmetic_finish->memory(06756) =
        Word48(0020000000000000ULL);
    arithmetic_finish->memory(066023) =
        Word48(06400000000000000ULL);
    arithmetic_finish->memory(066024) =
        Word48(04050000000000000ULL);
    arithmetic_finish->start(06735);
    require(arithmetic_finish->step()
                == poplan::ExecutionStatus::running
                && arithmetic_finish->program_counter() == 063752
                && arithmetic_finish->reg(017) == 066023
                && arithmetic_finish->memory(066023)
                    == Word48(06400000000000001ULL)
                && arithmetic_finish->accumulator()
                    == Word48(06400000000000001ULL)
                && arithmetic_finish->remainder()
                    == Word48(03760000000000000ULL)
                && arithmetic_finish->alu_mode() == 007,
            "06735 preserves the traced normalized-add stack transition");

    auto compiler_entry = std::make_unique<Machine>();
    compiler_entry->reg(005) = 05005;
    compiler_entry->reg(007) = 05007;
    compiler_entry->reg(015) = 07002;
    compiler_entry->reg(017) = 066000;
    compiler_entry->accumulator() = Word48(012345);
    compiler_entry->start(06343);
    require(compiler_entry->step() == poplan::ExecutionStatus::running
                && compiler_entry->program_counter() == 06526
                && compiler_entry->reg(005) == 06143
                && compiler_entry->reg(015) == 06346
                && compiler_entry->reg(017) == 066004
                && compiler_entry->memory(066000) == Word48(012345)
                && compiler_entry->memory(066001) == Word48(05005)
                && compiler_entry->memory(066002) == Word48(05007)
                && compiler_entry->memory(066003) == Word48(07002),
            "06343 saves its compiler frame and enters 06526");

    auto compiler_helper = std::make_unique<Machine>();
    compiler_helper->reg(007) = 05007;
    compiler_helper->reg(015) = 07003;
    compiler_helper->reg(017) = 066000;
    compiler_helper->accumulator() = Word48(012345);
    compiler_helper->memory(02117) = Word48(03000);
    compiler_helper->memory(03001) = Word48(07654321);
    compiler_helper->memory(01403) = Word48(01234);
    compiler_helper->start(06526);
    require(compiler_helper->step() == poplan::ExecutionStatus::running
                && compiler_helper->program_counter() == 017045
                && compiler_helper->reg(007) == 01200
                && compiler_helper->reg(015) == 06534
                && compiler_helper->reg(017) == 066004
                && compiler_helper->memory(066000) == Word48(012345)
                && compiler_helper->memory(066001) == Word48(05007)
                && compiler_helper->memory(066002) == Word48(07003),
            "06526 builds its trace-confirmed frame and calls 17045");

    auto compiler_dispatch = std::make_unique<Machine>();
    compiler_dispatch->reg(001) = 05001;
    compiler_dispatch->reg(003) = 05003;
    compiler_dispatch->reg(015) = 07004;
    compiler_dispatch->reg(017) = 066000;
    compiler_dispatch->accumulator() = Word48(07654);
    compiler_dispatch->start(016341);
    require(compiler_dispatch->step() == poplan::ExecutionStatus::running
                && compiler_dispatch->program_counter() == 016457
                && compiler_dispatch->reg(001) == 022261
                && compiler_dispatch->reg(003) == 07004
                && compiler_dispatch->reg(015) == 016347
                && compiler_dispatch->reg(017) == 066007,
            "16341 preserves its compiler registers before 16457");

    auto generated_return = std::make_unique<Machine>();
    generated_return->reg(006) = 05000;
    generated_return->reg(015) = 07005;
    generated_return->reg(016) = 04321;
    generated_return->reg(017) = 066000;
    generated_return->memory(05000) =
        Word48(06400000000000001ULL);
    generated_return->memory(020107) =
        Word48(06400000000000000ULL);
    generated_return->start(020077);
    require(generated_return->step() == poplan::ExecutionStatus::running
                && generated_return->program_counter() == 03277
                && generated_return->reg(015) == 020101,
            "20077 enters the POP-stack return selector");
    require(generated_return->step() == poplan::ExecutionStatus::running
                && generated_return->program_counter() == 020101,
            "20077 resumes after popping its selector");
    require(generated_return->step() == poplan::ExecutionStatus::running
                && generated_return->program_counter() == 07005
                && generated_return->reg(015) == 07005
                && generated_return->reg(017) == 066000
                && generated_return->accumulator() == Word48(04321),
            "20101 restores the saved link and r16 value");

    auto evaluator_classify = std::make_unique<Machine>();
    evaluator_classify->reg(001) = 05001;
    evaluator_classify->reg(015) = 07006;
    evaluator_classify->reg(017) = 066000;
    evaluator_classify->accumulator() =
        Word48(06440000000002044ULL);
    evaluator_classify->memory(021524) =
        Word48(07740000000000000ULL);
    evaluator_classify->memory(021525) =
        Word48(07200000000000000ULL);
    evaluator_classify->memory(021532) =
        Word48(06440000000002044ULL);
    evaluator_classify->start(021464);
    require(evaluator_classify->step() == poplan::ExecutionStatus::running
                && evaluator_classify->program_counter() == 021511
                && evaluator_classify->reg(001) == 021464
                && evaluator_classify->reg(017) == 066003
                && evaluator_classify->memory(021535)
                    == Word48(06440000000002044ULL),
            "21464 classifies the traced direct-return value");
    require(evaluator_classify->step() == poplan::ExecutionStatus::running
                && evaluator_classify->program_counter() == 07006
                && evaluator_classify->reg(001) == 05001
                && evaluator_classify->reg(017) == 066000
                && evaluator_classify->accumulator()
                    == Word48(06440000000002044ULL),
            "21511 restores the evaluator frame and caller link");

    auto replace_address = std::make_unique<Machine>();
    replace_address->reg(013) = 04000;
    replace_address->reg(015) = 07002;
    replace_address->reg(016) = 04100;
    replace_address->remainder() = Word48(07654);
    replace_address->memory(04000) =
        Word48(07100000000000123ULL);
    require(replace_address->p03516() == 07002
                && replace_address->memory(04100)
                    == Word48(07100000000000123ULL)
                && replace_address->memory(04000) == Word48(04100)
                && replace_address->accumulator() == Word48(04100)
                && replace_address->remainder() == Word48(07654)
                && replace_address->alu_mode() == 004,
            "03516 replaces the r13 word and preserves it through r16");

    auto pair_begin = std::make_unique<Machine>();
    pair_begin->accumulator() = Word48(012345);
    pair_begin->reg(015) = 06701;
    pair_begin->reg(017) = 066000;
    pair_begin->memory(05227) =
        Word48(07200000000000000ULL);
    require(pair_begin->p05215() == 05430
                && pair_begin->reg(010) == 05213
                && pair_begin->reg(015) == 05221
                && pair_begin->reg(016) == 2
                && pair_begin->reg(017) == 066003
                && pair_begin->memory(066000) == Word48(012345)
                && pair_begin->memory(066001)
                    == Word48(07200000000000000ULL)
                && pair_begin->memory(066002) == Word48(06701)
                && pair_begin->accumulator() == Word48(06701),
            "05215 builds the traced allocation frame and calls 05430");

    auto pair_finish = std::make_unique<Machine>();
    pair_finish->reg(016) = 044000;
    pair_finish->reg(017) = 066003;
    pair_finish->memory(065777) =
        Word48(06400000000000011ULL);
    pair_finish->memory(066000) =
        Word48(06400000000000022ULL);
    pair_finish->memory(066001) =
        Word48(07200000000000000ULL);
    pair_finish->memory(066002) = Word48(06701);
    require(pair_finish->p05221() == 06701
                && pair_finish->reg(010) == 05213
                && pair_finish->reg(017) == 065777
                && pair_finish->memory(044000)
                    == Word48(06400000000000011ULL)
                && pair_finish->memory(044001)
                    == Word48(06400000000000022ULL)
                && pair_finish->accumulator()
                    == Word48(07200000000044000ULL)
                && pair_finish->remainder() == Word48(044000)
                && pair_finish->alu_mode() == 004,
            "05221 initializes and tags the allocated pair before returning");

    auto tagged_precheck = std::make_unique<Machine>();
    tagged_precheck->reg(015) = 07003;
    tagged_precheck->reg(017) = 066002;
    tagged_precheck->memory(066000) = Word48(0123);
    tagged_precheck->memory(066001) = Word48(0456);
    tagged_precheck->memory(011777) = Word48(0770);
    tagged_precheck->memory(011522) = Word48(0120);
    require(tagged_precheck->p11536() == 011541
                && tagged_precheck->reg(010) == 011506
                && tagged_precheck->accumulator() == Word48(0456)
                && tagged_precheck->remainder() == Word48()
                && tagged_precheck->alu_mode() == 004,
            "11536 passes a matching tagged frame value to 11541");
    tagged_precheck->memory(066000) = Word48(0223);
    require(tagged_precheck->p11536() == 011547
                && tagged_precheck->accumulator() == Word48(0300),
            "11536 retains the original diagnostic boundary on mismatch");

    auto scan_match = std::make_unique<Machine>();
    scan_match->reg(003) = 04200;
    scan_match->reg(015) = 07004;
    scan_match->memory(04202) = Word48(0555);
    scan_match->memory(04204) =
        Word48(std::uint64_t{012} << 43);
    scan_match->memory(03647) = Word48(0444);
    scan_match->memory(03650) = Word48(0555);
    require(scan_match->p17242() == 07004
                && scan_match->reg(013) == 077751
                && scan_match->reg(016) == 0
                && scan_match->accumulator() == Word48()
                && scan_match->remainder() == Word48()
                && scan_match->alu_mode() == 004,
            "17242 finds a value in the longer r13-selected scan range");

    auto scan_miss = std::make_unique<Machine>();
    scan_miss->reg(003) = 04300;
    scan_miss->reg(015) = 07005;
    scan_miss->memory(04302) = Word48(0666);
    scan_miss->memory(04304) =
        Word48(std::uint64_t{012} << 43);
    require(scan_miss->p17253() == 07005
                && scan_miss->reg(013) == 0
                && scan_miss->reg(016) == 1
                && scan_miss->accumulator() == Word48(0666)
                && scan_miss->remainder() == Word48(0666),
            "17253 exhausts the shorter scan range and reports no match");

    auto semantic_scan = std::make_unique<Machine>();
    semantic_scan->reg(003) = 04300;
    semantic_scan->reg(015) = 07006;
    semantic_scan->start(017253);
    require(semantic_scan->step() == poplan::ExecutionStatus::running
                && semantic_scan->program_counter() == 07006
                && semantic_scan->reg(013) == 077760
                && semantic_scan->reg(016) == 1
                && semantic_scan->translated_routine_count() == 1,
            "17253 is selected by semantic dispatch");

    auto semantic_return = std::make_unique<Machine>();
    semantic_return->reg(015) = 07123;
    semantic_return->accumulator() = Word48(012345);
    semantic_return->start(020673);
    require(semantic_return->step() == poplan::ExecutionStatus::running
                && semantic_return->program_counter() == 07123
                && semantic_return->accumulator() == Word48(012345)
                && semantic_return->translated_routine_count() == 1,
            "20673 semantically returns through r15 without changing state");

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

    const auto install_tagged_byte_lookup = [](Machine &target) {
        target.reg(001) = 022261;
        target.memory(016756) =
            Word48(06400000000000000ULL);
        target.memory(016757) = Word48(015);
        target.memory(016760) = Word48(0170);
        target.memory(016761) = Word48(07);
        target.memory(016762) = Word48(077);

        // Trace-backed packed words for inputs 012, 040, and 106.
        target.memory(016440) =
            Word48(01515141515151515ULL);
        target.memory(016443) =
            Word48(00003041512060310ULL);
        target.memory(016447) =
            Word48(01501010101010101ULL);
    };

    const auto install_input_continue = [](Machine &target) {
        target.memory(020322) =
            Word48(02000000000000000ULL);
        target.memory(020323) =
            Word48(04000000000000000ULL);
        target.memory(020326) = Word48(1);
        target.memory(020333) = Word48(2);
        target.memory(020365) =
            Word48(00004000000040000ULL);
        target.memory(020375) = Word48();
        target.memory(020377) = Word48(012);
        target.memory(020336) =
            Word48(0100000077777777ULL);
        target.memory(020367) =
            Word48(04020025041120265ULL);
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

    // Static NEWARR descriptor: 6641223600000000 expands the two values in
    // its counted vector at 12243, then redispatches NEWANY at 12164.
    auto special_function = std::make_unique<Machine>();
    install_dispatch_constants(*special_function);
    special_function->accumulator() =
        Word48(06641223600000000ULL);
    special_function->memory(012241) =
        Word48(06600000000012164ULL);
    special_function->memory(012242) =
        Word48(07040000000012243ULL);
    special_function->memory(012243) =
        Word48(0000000200000003ULL);
    special_function->memory(012244) =
        Word48(06600000000011506ULL);
    special_function->memory(012245) =
        Word48(06601151700011524ULL);
    special_function->reg(001) = 01111;
    special_function->reg(003) = 03333;
    special_function->reg(004) = 04444;
    special_function->reg(006) = 070000;
    special_function->reg(015) = 05555;
    special_function->reg(017) = 04000;
    require(special_function->p02750_dispatch() == 015765,
            "02750 selects special dispatch for static NEWARR");
    require(special_function->p15765_dispatch_special_function() == 03261,
            "15765 redispatches NEWARR's environment +3 descriptor");
    require(special_function->memory(067777)
                == Word48(06600000000011506ULL)
                && special_function->memory(067776)
                    == Word48(06601151700011524ULL)
                && special_function->reg(006) == 067776,
            "15765 pushes the two static NEWARR vector values in order");
    require(special_function->reg(001) == 01111
                && special_function->reg(003) == 03333
                && special_function->reg(004) == 04444
                && special_function->reg(015) == 05555
                && special_function->reg(017) == 04000,
            "15765 restores its saved evaluator registers and hardware stack");
    require(special_function->memory(016004) == Word48()
                && special_function->memory(03272)
                    == Word48(06600000000012164ULL)
                && special_function->memory(03273) == Word48(),
            "15765 clears its scratch word and installs the nested descriptor");

    auto empty_special_function = std::make_unique<Machine>();
    install_dispatch_constants(*empty_special_function);
    empty_special_function->memory(03272) =
        Word48(06640300000000000ULL);
    empty_special_function->memory(03273) = Word48(03000);
    empty_special_function->memory(03003) =
        Word48(06600000000007667ULL);
    empty_special_function->memory(03004) = Word48();
    empty_special_function->reg(006) = 070000;
    empty_special_function->reg(017) = 04100;
    const std::uint16_t empty_special_next =
        empty_special_function->p15765_dispatch_special_function();
    require(empty_special_next == 03261,
            "15765 redispatches an empty special function");
    require(empty_special_function->reg(006) == 070000,
            "15765 pushes no values for a zero vector pointer");
    require(empty_special_function->reg(017) == 04100,
            "15765 balances the empty special-function save area");

    // Trace at 11702: tagged value 025 matches the high field of the object
    // reached through frame word -2, producing additive zero at 11545.
    auto tagged_match = std::make_unique<Machine>();
    tagged_match->reg(010) = 011506;
    tagged_match->reg(015) = 011703;
    tagged_match->reg(016) = 0777;
    tagged_match->reg(017) = 066064;
    tagged_match->alu_mode() = 003;
    tagged_match->memory(012000) =
        Word48(06400000000000000ULL);
    tagged_match->memory(012012) =
        Word48(07777777770000000ULL);
    tagged_match->memory(066062) = Word48(065741);
    tagged_match->memory(065741) =
        Word48(0000002500000005ULL);
    tagged_match->memory(066063) =
        Word48(06400000000000025ULL);
    tagged_match->accumulator() =
        Word48(06400000000000025ULL);
    require(tagged_match->p11541_match_tagged_value() == 011703,
            "11541 returns through r15 for the traced matching value");
    require(tagged_match->accumulator()
                == Word48(06400000000000000ULL),
            "11541 reproduces the traced additive zero word");
    require(tagged_match->reg(016) == 0777
                && tagged_match->reg(017) == 066064,
            "11541 does not change its diagnostic register or frame");

    auto tagged_mismatch = std::make_unique<Machine>();
    tagged_mismatch->reg(010) = 011506;
    tagged_mismatch->reg(015) = 011703;
    tagged_mismatch->reg(017) = 066064;
    tagged_mismatch->alu_mode() = 003;
    tagged_mismatch->memory(012000) =
        Word48(06400000000000000ULL);
    tagged_mismatch->memory(012012) =
        Word48(07777777770000000ULL);
    tagged_mismatch->memory(066062) = Word48(065741);
    tagged_mismatch->memory(065741) =
        Word48(0000002400000005ULL);
    tagged_mismatch->memory(066063) =
        Word48(06400000000000025ULL);
    tagged_mismatch->accumulator() =
        Word48(06400000000000025ULL);
    require(tagged_mismatch->p11541_match_tagged_value() == 03014
                && tagged_mismatch->reg(016) == 010100
                && tagged_mismatch->accumulator()
                    == Word48(06400000000000025ULL),
            "11541 sends a mismatched value to diagnostic 10100");

    // Trace at 16457 with r1=22261 and r3=20670: shift a nonempty three-word
    // record and take continuation 16467; an empty +1 takes 16463 unchanged.
    auto shifted_record = std::make_unique<Machine>();
    shifted_record->reg(001) = 022261;
    shifted_record->reg(003) = 020670;
    shifted_record->memory(020670) = Word48(011);
    shifted_record->memory(020671) =
        Word48(06400000000000012ULL);
    shifted_record->memory(020672) = Word48(077);
    require(shifted_record->p16457_shift_record() == 016467,
            "16457 takes the traced nonempty continuation");
    require(shifted_record->memory(020670)
                == Word48(06400000000000012ULL)
                && shifted_record->memory(020671) == Word48(077)
                && shifted_record->memory(020672) == Word48()
                && shifted_record->accumulator() == Word48(),
            "16457 shifts the record left and clears its final word");

    auto empty_record = std::make_unique<Machine>();
    empty_record->reg(001) = 022261;
    empty_record->reg(003) = 020670;
    empty_record->memory(020670) = Word48(011);
    empty_record->memory(020671) = Word48();
    empty_record->memory(020672) = Word48(077);
    require(empty_record->p16457_shift_record() == 016463
                && empty_record->memory(020670) == Word48(011)
                && empty_record->memory(020672) == Word48(077),
            "16457 leaves an empty record unchanged at continuation 16463");

    // Trace at 16505 wraps 16457 with a balanced two-word save area. The
    // translated entry stops at 16467; the translated 16507 resume models
    // the return after that continuation has completed.
    auto wrapped_record = std::make_unique<Machine>();
    wrapped_record->reg(001) = 022261;
    wrapped_record->reg(003) = 020670;
    wrapped_record->reg(015) = 016406;
    wrapped_record->reg(017) = 066033;
    wrapped_record->accumulator() = Word48(055);
    wrapped_record->memory(020670) = Word48(0106);
    wrapped_record->memory(020671) = Word48(0125);
    wrapped_record->memory(020672) = Word48();
    require(wrapped_record->p16505_begin_record_shift() == 016467,
            "16505 enters the nonempty 16457 continuation");
    require(wrapped_record->reg(015) == 016507
                && wrapped_record->reg(017) == 066035
                && wrapped_record->memory(066033) == Word48(055)
                && wrapped_record->memory(066034) == Word48(016406),
            "16505 preserves the accumulator and caller link");
    require(wrapped_record->memory(020670) == Word48(0125)
                && wrapped_record->memory(020671) == Word48()
                && wrapped_record->memory(020672) == Word48(),
            "16505 retains 16457's traced record shift");
    require(wrapped_record->p16507_resume_record_shift() == 016477,
            "16507 selects the r1-relative continuation 74216");
    require(wrapped_record->reg(015) == 016406
                && wrapped_record->reg(017) == 066033
                && wrapped_record->accumulator() == Word48(055),
            "16507 restores the caller state and balances r17");

    auto continuation_frame = std::make_unique<Machine>();
    continuation_frame->accumulator() = Word48(0123);
    continuation_frame->remainder() = Word48(066);
    continuation_frame->reg(001) = 0111;
    continuation_frame->reg(002) = 0222;
    continuation_frame->reg(015) = 05555;
    continuation_frame->reg(016) = 04444;
    continuation_frame->reg(017) = 04000;
    continuation_frame->memory(04364) = Word48(0777);
    require(continuation_frame->p03536() == 04444,
            "03536 transfers through the continuation installed in r16");
    require(continuation_frame->reg(002) == 03536,
            "03536 installs its frame base in r2");
    require(continuation_frame->reg(017) == 04005,
            "03536 advances r17 over its five-word continuation frame");
    require(continuation_frame->accumulator() == Word48(),
            "03536 leaves the zero word in the accumulator");
    require(continuation_frame->remainder() == Word48(066),
            "03536 preserves RMR while building its continuation frame");
    require(continuation_frame->memory(04000) == Word48(0123),
            "03536 saves the incoming accumulator first");
    require(continuation_frame->memory(04001) == Word48(0222),
            "03536 saves the incoming r2 second");
    require(continuation_frame->memory(04002) == Word48(0111),
            "03536 saves the incoming r1 third");
    require(continuation_frame->memory(04003) == Word48(05555),
            "03536 saves the incoming link fourth");
    require(continuation_frame->memory(04004) == Word48(0777),
            "03536 saves the scratch word fifth");
    require(continuation_frame->memory(04364) == Word48(),
            "03536 clears its scratch word after saving it");

    const auto compare_table_transform = [](
        std::uint16_t entry, bool allocation_path) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        const std::pair<std::uint16_t, Word48> code[] = {
            {03724, Word48(00043001500430015ULL)},
            {03725, Word48(00220000067117337ULL)},
            {03726, Word48(01000010400400001ULL)},
            {03727, Word48(00510777710110641ULL)},
            {03730, Word48(01012064210130105ULL)},
            {03731, Word48(01013062712600176ULL)},
            {03732, Word48(01010010410030636ULL)},
            {03733, Word48(06640372503004447ULL)},
            {03734, Word48(00041001500410015ULL)},
            {03735, Word48(01010010403017340ULL)},
        };
        for (const auto &[address, word] : code) {
            semantic->memory(address) = interpreted->memory(address) = word;
        }
        const auto initialize = [entry, allocation_path](Machine &machine) {
            machine.accumulator() = Word48(02200000000000123ULL);
            machine.remainder() = Word48(077);
            machine.alu_mode() = 031;
            machine.reg(0) = 0;
            machine.reg(002) = 03536;
            machine.reg(015) = entry == 03724 ? 03631 : 03725;
            machine.reg(017) = entry == 03724 ? 04000 : 04002;
            if (entry == 03725) {
                machine.memory(04000) = Word48(02200000000000123ULL);
                machine.memory(04001) = Word48(03631);
            }

            machine.memory(017353) = Word48(05000);
            machine.memory(017354) = Word48(3);
            machine.memory(05002) = Word48(06000);
            machine.memory(05777) = allocation_path
                ? Word48() : Word48(04000000000000000ULL);
            machine.memory(04377) = Word48(07777777777777777ULL);
            machine.memory(04400) = Word48();
            machine.memory(03643) = Word48();
            machine.memory(04365) = Word48();
            machine.memory(04374) = Word48(07100000000000456ULL);
        };
        initialize(*semantic);
        initialize(*interpreted);

        semantic->start(entry);
        require(semantic->step() == poplan::ExecutionStatus::running,
                "03724 semantic path keeps running");
        const std::uint16_t continuation = semantic->program_counter();

        interpreted->start(entry);
        interpreted->disable_translated_routine(03724);
        interpreted->disable_translated_routine(03725);
        interpreted->disable_translated_routine(03726);
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 64;
             ++steps) {
            require(interpreted->step() == poplan::ExecutionStatus::running,
                    "03724 instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "03724 instruction path reaches its semantic continuation");
        require(semantic->accumulator() == interpreted->accumulator()
                    && semantic->remainder() == interpreted->remainder()
                    && semantic->alu_mode() == interpreted->alu_mode(),
                "03724 preserves BESM ALU state");
        for (std::size_t index = 0; index != 020; ++index) {
            require(semantic->reg(index) == interpreted->reg(index),
                    "03724 preserves all modifier registers");
        }
        for (std::size_t address = 0;
             address != Machine::core_words; ++address) {
            require(semantic->memory(static_cast<std::uint16_t>(address))
                        == interpreted->memory(
                            static_cast<std::uint16_t>(address)),
                    "03724 preserves complete BESM memory state");
        }
        return continuation;
    };

    require(compare_table_transform(03724, false) == 03631,
            "03724 completes its common table read/write path");
    require(compare_table_transform(03724, true) == 04447,
            "03724 retains the generated-item allocation boundary");
    require(compare_table_transform(03725, false) == 03631,
            "03725 resumes the table transform after allocation");

    auto classifier_fast = std::make_unique<Machine>();
    classifier_fast->reg(015) = 01234;
    classifier_fast->reg(017) = 04000;
    classifier_fast->memory(03637) = Word48(0123);
    classifier_fast->memory(04421) = Word48(0777);
    classifier_fast->memory(04422) = Word48();
    require(classifier_fast->p04322() == 01234
                && classifier_fast->reg(014) == 03536
                && classifier_fast->reg(016) == 1
                && classifier_fast->reg(017) == 04000,
            "04322 takes its common immediate-return classification");
    require(classifier_fast->accumulator() == Word48(0123)
                && classifier_fast->remainder() == Word48(0123),
            "04322 preserves the common classification branch flag");

    auto classifier_return = std::make_unique<Machine>();
    classifier_return->reg(015) = 02345;
    classifier_return->reg(017) = 04100;
    classifier_return->memory(03637) = Word48(0123);
    classifier_return->memory(03641) = Word48(1);
    classifier_return->memory(03643) = Word48(2);
    classifier_return->memory(04421) = Word48(0777);
    classifier_return->memory(04422) = Word48(0123);
    classifier_return->memory(04423) = Word48();
    classifier_return->memory(04424) = Word48(1);
    classifier_return->memory(04425) = Word48(1);
    require(classifier_return->p04322() == 02345
                && classifier_return->reg(015) == 02345
                && classifier_return->reg(016) == 1
                && classifier_return->reg(017) == 04100,
            "04322 restores its stacked caller after the secondary checks");
    require(classifier_return->accumulator() == Word48(02345)
                && classifier_return->remainder() == Word48(3),
            "04322 retains the final secondary comparison flag");

    auto classifier_nested = std::make_unique<Machine>();
    classifier_nested->reg(015) = 03456;
    classifier_nested->reg(017) = 04200;
    classifier_nested->memory(03637) = Word48(0123);
    classifier_nested->memory(03641) = Word48(03000);
    classifier_nested->memory(03643) = Word48(2);
    classifier_nested->memory(04421) = Word48(0777);
    classifier_nested->memory(04422) = Word48(0123);
    classifier_nested->memory(04423) = Word48();
    classifier_nested->memory(04424) = Word48(2);
    classifier_nested->memory(04425) = Word48(1);
    classifier_nested->memory(04377) = Word48(017);
    classifier_nested->memory(02777) = Word48(077);
    require(classifier_nested->p04322() == 016313
                && classifier_nested->reg(013) == 03000
                && classifier_nested->reg(014) == 020
                && classifier_nested->reg(015) == 04350
                && classifier_nested->reg(016) == 04357,
            "04322 enters the original character-sequence dependency");
    require(classifier_nested->memory(02777) == Word48(060)
                && classifier_nested->memory(04200) == Word48(03456)
                && classifier_nested->reg(017) == 04201,
            "04322 updates the selected slot while retaining its caller");

    auto classifier_continuations = std::make_unique<Machine>();
    classifier_continuations->reg(003) = 03000;
    classifier_continuations->memory(03000) = Word48(06500000000000000ULL);
    classifier_continuations->start(04350);
    require(classifier_continuations->step()
                == poplan::ExecutionStatus::running
                && classifier_continuations->program_counter() == 03275
                && classifier_continuations->reg(015) == 04351,
            "04350 forwards the selected record through PUSH_ACC");
    classifier_continuations->start(04351);
    classifier_continuations->step();
    require(classifier_continuations->program_counter() == 02764
                && classifier_continuations->reg(015) == 04352
                && classifier_continuations->reg(016) == 07667,
            "04351 enters the first original 02764 dependency");
    classifier_continuations->reg(007) = 02000;
    classifier_continuations->memory(03007) = Word48(07100000000000001ULL);
    classifier_continuations->start(04352);
    classifier_continuations->step();
    require(classifier_continuations->program_counter() == 03275
                && classifier_continuations->reg(015) == 04353
                && classifier_continuations->accumulator()
                    == Word48(07100000000000001ULL),
            "04352 forwards the r7-relative value through PUSH_ACC");
    classifier_continuations->start(04353);
    classifier_continuations->step();
    require(classifier_continuations->program_counter() == 02764
                && classifier_continuations->reg(015) == 04354
                && classifier_continuations->reg(016) == 07601,
            "04353 enters the second original 02764 dependency");
    classifier_continuations->reg(017) = 04301;
    classifier_continuations->memory(04300) = Word48(04567);
    classifier_continuations->start(04354);
    classifier_continuations->step();
    require(classifier_continuations->program_counter() == 04567
                && classifier_continuations->reg(015) == 04567
                && classifier_continuations->reg(016) == 0
                && classifier_continuations->reg(017) == 04300,
            "04354 restores the original classifier caller and stack");

    auto compiler_wrapper = std::make_unique<Machine>();
    compiler_wrapper->accumulator() = Word48(077);
    compiler_wrapper->reg(015) = 01234;
    compiler_wrapper->reg(017) = 04100;
    require(compiler_wrapper->p04467() == 06343
                && compiler_wrapper->reg(015) == 04471
                && compiler_wrapper->reg(017) == 04101,
            "04467 enters the translated 06343 cluster with link 04471");
    require(compiler_wrapper->accumulator() == Word48(01234)
                && compiler_wrapper->memory(04100) == Word48(01234),
            "04467 preserves its incoming link on the hardware stack");

    auto allocator_wrapper = std::make_unique<Machine>();
    allocator_wrapper->accumulator() = Word48(0123);
    allocator_wrapper->reg(015) = 02345;
    allocator_wrapper->reg(017) = 04401;
    allocator_wrapper->memory(04400) = Word48();
    allocator_wrapper->memory(04446) = Word48(077777);
    require(allocator_wrapper->p04447() == 05215
                && allocator_wrapper->reg(013) == 04426
                && allocator_wrapper->reg(015) == 04455
                && allocator_wrapper->reg(017) == 04402,
            "04447 prepares its argument and enters the two-word allocator");
    require(allocator_wrapper->memory(04505) == Word48(0123)
                && allocator_wrapper->memory(04506) == Word48()
                && allocator_wrapper->memory(04400) == Word48(02345)
                && allocator_wrapper->memory(04401) == Word48(0123)
                && allocator_wrapper->accumulator() == Word48(),
            "04447 preserves its value, caller link, and computed argument");

    auto allocator_resume = std::make_unique<Machine>();
    allocator_resume->accumulator() = Word48(06600000000003000ULL);
    allocator_resume->reg(007) = 02000;
    allocator_resume->reg(017) = 04501;
    allocator_resume->memory(04500) = Word48(03456);
    allocator_resume->memory(03645) = Word48(7);
    allocator_resume->memory(04463) = Word48(0777);
    allocator_resume->memory(04464) = Word48(03000);
    allocator_resume->memory(04530) = Word48(1);
    allocator_resume->start(04455);
    require(allocator_resume->step() == poplan::ExecutionStatus::running
                && allocator_resume->program_counter() == 03456
                && allocator_resume->reg(017) == 04500,
            "04455 returns through the caller restored from r17");
    require(allocator_resume->memory(03001)
                == Word48(06600000000003000ULL)
                && allocator_resume->memory(04464)
                    == Word48(06600000000003000ULL),
            "04455 installs the allocated pair in both generated slots");
    require(allocator_resume->memory(03645) == Word48(010)
                && allocator_resume->memory(03163) == Word48(0777)
                && allocator_resume->accumulator() == Word48(0777),
            "04455 advances its counter and executes the generated store");

    auto compiler_resume_nonzero = std::make_unique<Machine>();
    compiler_resume_nonzero->accumulator() = Word48(0671);
    compiler_resume_nonzero->reg(003) = 03000;
    compiler_resume_nonzero->reg(016) = 04567;
    compiler_resume_nonzero->reg(017) = 04201;
    compiler_resume_nonzero->memory(04200) = Word48(02345);
    compiler_resume_nonzero->memory(04532) = Word48(0777);
    compiler_resume_nonzero->memory(04533) = Word48();
    compiler_resume_nonzero->start(04471);
    require(compiler_resume_nonzero->step()
                == poplan::ExecutionStatus::running
                && compiler_resume_nonzero->program_counter() == 02345,
            "04471 returns through the saved caller on its nonzero path");
    require(compiler_resume_nonzero->memory(03000) == Word48(0671)
                && compiler_resume_nonzero->memory(03002) == Word48()
                && compiler_resume_nonzero->memory(03005) == Word48(0671)
                && compiler_resume_nonzero->accumulator() == Word48(0671),
            "04471 preserves the nonzero masked result and clears word +2");

    auto compiler_resume_zero = std::make_unique<Machine>();
    compiler_resume_zero->accumulator() = Word48(012345);
    compiler_resume_zero->reg(003) = 03100;
    compiler_resume_zero->reg(016) = 05000;
    compiler_resume_zero->reg(017) = 04301;
    compiler_resume_zero->memory(04300) = Word48(03456);
    compiler_resume_zero->memory(04446) = Word48(077777);
    compiler_resume_zero->memory(04532) = Word48(0777);
    compiler_resume_zero->memory(04533) = Word48(0345);
    compiler_resume_zero->memory(04534) = Word48(0777);
    compiler_resume_zero->memory(04777) = Word48(0666);
    compiler_resume_zero->start(04471);
    require(compiler_resume_zero->step()
                == poplan::ExecutionStatus::running
                && compiler_resume_zero->program_counter() == 03456,
            "04471 returns through the saved caller on its zero path");
    require(compiler_resume_zero->memory(03100) == Word48(012345)
                && compiler_resume_zero->memory(03101) == Word48(012345)
                && compiler_resume_zero->memory(03102) == Word48(05000)
                && compiler_resume_zero->memory(03104) == Word48(0666)
                && compiler_resume_zero->memory(03105) == Word48()
                && compiler_resume_zero->accumulator() == Word48(),
            "04471 fills the zero-result record from the saved continuation");

    // The hottest untranslated tic-tac-toe entry hashes a source word and
    // follows the selected collision chain.  This is the first live trace:
    // bucket 01354 reaches the matching object through 01534 -> 01624.
    auto interned_record = std::make_unique<Machine>();
    interned_record->accumulator() = Word48(02125110124642400ULL);
    interned_record->reg(001) = 022261;
    interned_record->reg(002) = 1;
    interned_record->reg(003) = 020670;
    interned_record->reg(004) = 077777;
    interned_record->reg(005) = 00100;
    interned_record->reg(006) = 070000;
    interned_record->reg(007) = 01200;
    interned_record->reg(010) = 03206;
    interned_record->reg(012) = 5;
    interned_record->reg(013) = 041;
    interned_record->reg(014) = 021302;
    interned_record->reg(015) = 016420;
    interned_record->reg(016) = 021301;
    interned_record->reg(017) = 066033;
    interned_record->memory(01173) = Word48(0377);
    interned_record->memory(01174) = Word48(1);
    interned_record->memory(01175) = Word48(03000000000000000ULL);
    interned_record->memory(01176) = Word48(06500000000000000ULL);
    interned_record->memory(01177) = Word48(06440000000000000ULL);
    interned_record->memory(01354) = Word48(0000153400001660ULL);
    interned_record->memory(01534) = Word48(02064751624650101ULL);
    interned_record->memory(01536) = Word48(04000000000001624ULL);
    interned_record->memory(01624) = Word48(02125110124642400ULL);
    require(interned_record->p01107() == 016420,
            "01107 returns through the traced evaluator link");
    require(interned_record->accumulator()
                == Word48(06440000000001624ULL)
                && interned_record->remainder() == Word48(01624)
                && interned_record->reg(016) == 01624,
            "01107 reproduces the traced collision-chain hit");
    require(interned_record->reg(001) == 022261
                && interned_record->reg(003) == 020670
                && interned_record->reg(004) == 077777
                && interned_record->reg(005) == 00100
                && interned_record->reg(007) == 01200
                && interned_record->reg(017) == 066033,
            "01107 restores its seven-word register save area");
    require(interned_record->memory(01171)
                == Word48(02125110124642400ULL)
                && interned_record->memory(01172) == Word48(04330),
            "01107 preserves the traced descriptor and hash value");

    auto generated_compare = std::make_unique<Machine>();
    generated_compare->accumulator() = Word48(2);
    generated_compare->reg(014) = 01234;
    generated_compare->reg(016) = 02000;
    generated_compare->reg(017) = 04000;
    generated_compare->memory(03777) = Word48(4);
    require(generated_compare->p06650() == 01234,
            "06650 takes its r14 continuation when both comparisons differ");
    require(generated_compare->reg(013) == 06600
                && generated_compare->reg(017) == 04001
                && generated_compare->memory(04000) == Word48(2)
                && generated_compare->accumulator() == Word48(2)
                && generated_compare->remainder() == Word48(2),
            "06650 retains the generated template's comparison state");

    auto generated_compare_error = std::make_unique<Machine>();
    generated_compare_error->reg(016) = 02000;
    generated_compare_error->reg(017) = 04100;
    require(generated_compare_error->p06650() == 03014
                && generated_compare_error->reg(015) == 06654
                && generated_compare_error->reg(016) == 02001,
            "06650 selects diagnostic continuation 06654 on its first match");
    require(generated_compare_error->reg(017) == 04100
                && generated_compare_error->accumulator() == Word48(),
            "06650 balances its temporary stack word on the diagnostic path");

    const auto compare_generated_template = [](
        std::uint16_t entry, Word48 newer, Word48 older) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        const std::pair<std::uint16_t, Word48> code[] = {
            {06623, Word48(07241404063106650ULL)},
            {06624, Word48(07410000074050000ULL)},
            {06625, Word48(05660006057000057ULL)},
            {06631, Word48(07241403063106650ULL)},
            {06632, Word48(07410000074060000ULL)},
            {06633, Word48(05660006057000057ULL)},
            {06637, Word48(07241405063106650ULL)},
            {06640, Word48(07410000074050000ULL)},
            {06641, Word48(05660005757000060ULL)},
            {06645, Word48(07241406063106650ULL)},
            {06646, Word48(07410000074060000ULL)},
            {06647, Word48(05660005757000060ULL)},
            {06650, Word48(05640660074000000ULL)},
            {06651, Word48(00036010154130071ULL)},
            {06652, Word48(05670005474100000ULL)},
            {06653, Word48(07250000167103014ULL)},
            {06654, Word48(07510777600360101ULL)},
            {06655, Word48(05413007162700000ULL)},
            {06656, Word48(07510777667103014ULL)},
        };
        for (const auto &[address, word] : code) {
            semantic->memory(address) = word;
            interpreted->memory(address) = word;
        }
        semantic->reg(0) = interpreted->reg(0) = 012;
        semantic->reg(016) = interpreted->reg(016) = 02000;
        semantic->reg(017) = interpreted->reg(017) = 04000;
        semantic->accumulator() = interpreted->accumulator() = newer;
        semantic->memory(03777) = interpreted->memory(03777) = older;

        std::uint16_t continuation = 0;
        switch (entry) {
        case 06623: continuation = semantic->p06623(); break;
        case 06631: continuation = semantic->p06631(); break;
        case 06637: continuation = semantic->p06637(); break;
        case 06645: continuation = semantic->p06645(); break;
        default: require(false, "generated template entry is recognized");
        }

        interpreted->start(entry);
        interpreted->set_translated_routines_enabled(false);
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 64;
             ++steps) {
            require(interpreted->step() == poplan::ExecutionStatus::running,
                    "generated template instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "generated template reaches its semantic continuation");
        require(semantic->accumulator() == interpreted->accumulator()
                    && semantic->remainder() == interpreted->remainder()
                    && semantic->alu_mode() == interpreted->alu_mode(),
                "generated template preserves BESM ALU state");
        for (std::size_t index = 0; index != 020; ++index) {
            require(semantic->reg(index) == interpreted->reg(index),
                    "generated template preserves all modifier registers");
        }
        require(semantic->memory(03777) == interpreted->memory(03777)
                    && semantic->memory(04000)
                        == interpreted->memory(04000),
                "generated template preserves its two stack operands");
        return continuation;
    };

    const std::uint16_t template_06623 =
        compare_generated_template(06623, Word48(2), Word48(4));
    const std::uint16_t template_06631 =
        compare_generated_template(06631, Word48(4), Word48(2));
    const std::uint16_t template_06637 =
        compare_generated_template(06637, Word48(2), Word48(4));
    const std::uint16_t template_06645 =
        compare_generated_template(06645, Word48(4), Word48(2));
    require(template_06623 == 06660 && template_06631 == 06660
                && template_06637 == 06657 && template_06645 == 06657,
            "06623/06631/06637/06645 retain both destination layouts");

    auto populated_record = std::make_unique<Machine>();
    populated_record->reg(003) = 03000;
    populated_record->reg(015) = 04567;
    populated_record->memory(03001) = Word48(06400000000000017ULL);
    require(populated_record->p16477() == 04567
                && populated_record->accumulator()
                    == Word48(06400000000000017ULL)
                && populated_record->remainder()
                    == Word48(06400000000000017ULL),
            "16477 returns an already populated record word directly");

    auto empty_record_word = std::make_unique<Machine>();
    empty_record_word->accumulator() = Word48(055);
    empty_record_word->reg(003) = 03000;
    empty_record_word->reg(015) = 04567;
    empty_record_word->reg(017) = 04200;
    empty_record_word->memory(02776) = Word48(0666);
    require(empty_record_word->p16477() == 02750
                && empty_record_word->reg(015) == 016502
                && empty_record_word->reg(017) == 04202,
            "16477 enters the evaluator for an empty record word");
    require(empty_record_word->memory(04200) == Word48()
                && empty_record_word->memory(04201) == Word48(04567)
                && empty_record_word->accumulator() == Word48(0666),
            "16477 preserves its caller and loads the traced descriptor");

    auto table_read = std::make_unique<Machine>();
    table_read->reg(015) = 05670;
    table_read->memory(017353) = Word48(03000);
    table_read->memory(017354) = Word48(3);
    table_read->memory(03002) = Word48(0123456701234567ULL);
    require(table_read->p17337() == 05670
                && table_read->reg(016) == 017353
                && table_read->reg(011) == 2
                && table_read->memory(017354) == Word48(2)
                && table_read->accumulator()
                    == Word48(0123456701234567ULL),
            "17337 consumes and reads the next shared table slot");

    auto table_write = std::make_unique<Machine>();
    table_write->accumulator() = Word48(0765432107654321ULL);
    table_write->reg(015) = 06701;
    table_write->reg(017) = 04300;
    table_write->memory(017353) = Word48(03100);
    table_write->memory(017354) = Word48(022);
    require(table_write->p17340() == 06701
                && table_write->reg(016) == 017353
                && table_write->reg(011) == 023
                && table_write->reg(017) == 04300,
            "17340 advances the shared table descriptor and balances r17");
    require(table_write->memory(03122)
                == Word48(0765432107654321ULL)
                && table_write->memory(017354) == Word48(023)
                && table_write->accumulator()
                    == Word48(0765432107654321ULL),
            "17340 stores the value in the selected shared table slot");

    const auto require_same_architectural_state = [](
        const Machine &semantic, const Machine &interpreted,
        const std::string &label) {
        if (semantic.program_counter() != interpreted.program_counter()
            || semantic.right_half() != interpreted.right_half()
            || semantic.accumulator() != interpreted.accumulator()
            || semantic.remainder() != interpreted.remainder()
            || semantic.alu_mode() != interpreted.alu_mode()) {
            std::cerr << label << " semantic/raw control: pc "
                      << std::oct << semantic.program_counter() << '/'
                      << interpreted.program_counter() << " half "
                      << semantic.right_half() << '/'
                      << interpreted.right_half() << " acc "
                      << semantic.accumulator().raw() << '/'
                      << interpreted.accumulator().raw() << " rmr "
                      << semantic.remainder().raw() << '/'
                      << interpreted.remainder().raw() << " rau "
                      << static_cast<unsigned>(semantic.alu_mode()) << '/'
                      << static_cast<unsigned>(interpreted.alu_mode())
                      << '\n';
        }
        require(semantic.program_counter() == interpreted.program_counter()
                    && semantic.right_half() == interpreted.right_half()
                    && semantic.accumulator() == interpreted.accumulator()
                    && semantic.remainder() == interpreted.remainder()
                    && semantic.alu_mode() == interpreted.alu_mode(),
                label + " preserves control and ALU state");
        for (std::size_t index = 0; index != 020; ++index) {
            if (semantic.reg(index) != interpreted.reg(index)) {
                std::cerr << label << " register " << std::oct << index
                          << " semantic/raw: " << semantic.reg(index)
                          << '/' << interpreted.reg(index) << '\n';
            }
            require(semantic.reg(index) == interpreted.reg(index),
                    label + " preserves all modifier registers");
        }
        for (std::size_t address = 0;
             address != Machine::core_words; ++address) {
            if (semantic.memory(static_cast<std::uint16_t>(address))
                    != interpreted.memory(
                        static_cast<std::uint16_t>(address))) {
                std::cerr << label << " memory " << std::oct << address
                          << " semantic/raw: "
                          << semantic.memory(
                                 static_cast<std::uint16_t>(address)).raw()
                          << '/'
                          << interpreted.memory(
                                 static_cast<std::uint16_t>(address)).raw()
                          << '\n';
            }
            require(semantic.memory(static_cast<std::uint16_t>(address))
                        == interpreted.memory(
                            static_cast<std::uint16_t>(address)),
                    label + " preserves complete BESM memory state");
        }
    };

    {
        const std::pair<std::uint16_t, Word48> descriptor_entry_code[] = {
            {02764, Word48(0x8a05e502300eULL)},
            {02765, Word48(0x80a0250906baULL)},
            {02766, Word48(0x0010000c06b1ULL)},
        };
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : descriptor_entry_code) {
                machine->memory(address) = word;
            }
            machine->memory(03012) = Word48(0xd80000000000ULL);
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 053;
            machine->reg(010) = 01234;
            machine->reg(015) = 07000;
            machine->reg(016) = 04567;
            machine->reg(017) = 05000;
            machine->start(02764);
        }
        semantic->step();
        interpreted->set_translated_routines_enabled(false);
        for (unsigned steps = 0;
             (interpreted->program_counter() != 03261
              || interpreted->right_half()) && steps != 8;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    "02764 raw descriptor path keeps running");
        }
        require(interpreted->program_counter() == 03261
                    && !interpreted->right_half(),
                "02764 raw descriptor path reaches ENTER_FUNCTION");
        require_same_architectural_state(
            *semantic, *interpreted, "02764 descriptor entry");
    }

    const std::pair<std::uint16_t, Word48> classifier_03330_code[] = {
        {03330, Word48(0xba06c8b00061ULL)},
        {03331, Word48(0xb09064b0a065ULL)},
        {03332, Word48(0xbb0016b08061ULL)},
        {03333, Word48(0x09042700a000ULL)},
        {03334, Word48(0xbb0016008000ULL)},
        {03335, Word48(0xdc0000090000ULL)},
        {03336, Word48(0xb08066dc0000ULL)},
    };
    const auto compare_classifier_03330 = [
        &classifier_03330_code,
        &require_same_architectural_state](Word48 input,
                                           Word48 secondary,
                                           const std::string &label) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : classifier_03330_code) {
                machine->memory(address) = word;
            }
            machine->memory(02047) = secondary;
            machine->memory(03454) = Word48(0xfe0000000000ULL);
            machine->memory(03455) = Word48(0xe80000000000ULL);
            machine->memory(03456) = Word48(1);
            machine->accumulator() = input;
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(013) = 01234;
            machine->reg(015) = 07000;
            machine->start(03330);
        }
        semantic->step();
        interpreted->disable_translated_routine(03330);
        interpreted->disable_translated_routine(03336);
        for (unsigned steps = 0;
             (interpreted->program_counter() != 07000
              || interpreted->right_half()) && steps != 16;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require(interpreted->program_counter() == 07000
                    && !interpreted->right_half(),
                label + " reaches its saved caller");
        require_same_architectural_state(*semantic, *interpreted, label);
    };

    compare_classifier_03330(
        Word48(0xe80000000000ULL), Word48(012345),
        "03330 masked classifier match");
    compare_classifier_03330(
        Word48(012345), Word48(012345),
        "03330 secondary classifier match");
    compare_classifier_03330(
        Word48(0765432107654321ULL), Word48(012345),
        "03330 classifier miss");

    const std::pair<std::uint16_t, Word48> descriptor_scan_code[] = {
        {017624, Word48(0x02200d023001ULL)},
        {017625, Word48(0x023002023003ULL)},
        {017626, Word48(0xf000001a1f94ULL)},
        {017627, Word48(0x01f003108074ULL)},
        {017630, Word48(0x10009d2a0932ULL)},
        {017631, Word48(0x208001020003ULL)},
        {017632, Word48(0x108075dc9ff2ULL)},
        {017633, Word48(0x02200210009eULL)},
        {017634, Word48(0x00800010009cULL)},
        {017635, Word48(0x10009b090000ULL)},
        {017636, Word48(0x308000109076ULL)},
        {017637, Word48(0x10a0771b8019ULL)},
        {017655, Word48(0x3e1fcf308000ULL)},
        {017717, Word48(0x10807410009fULL)},
        {017720, Word48(0x0080001000a2ULL)},
        {017721, Word48(0x1000a01000a1ULL)},
        {017722, Word48(0x1080a21b0044ULL)},
        {017730, Word48(0x10809f1b804cULL)},
        {017740, Word48(0x10809c10509bULL)},
        {017741, Word48(0x00a0001b0050ULL)},
        {017744, Word48(0x1080a01b0053ULL)},
        {017747, Word48(0x10809c01e028ULL)},
        {017750, Word48(0x19809eea0000ULL)},
        {017751, Word48(0xe0a000e00000ULL)},
        {017752, Word48(0x090000dc9ffcULL)},
        {017756, Word48(0x10809d0907a5ULL)},
        {017757, Word48(0x001000021003ULL)},
        {017760, Word48(0x021002021001ULL)},
        {017761, Word48(0x02000ddc0000ULL)},
    };
    const auto load_descriptor_scan_code = [&descriptor_scan_code](
                                               Machine &machine) {
        for (const auto &[address, word] : descriptor_scan_code) {
            machine.memory(address) = word;
        }
        machine.memory(020010) = Word48(0x000000000001ULL);
        machine.memory(020011) = Word48(0x060000000000ULL);
        machine.memory(020012) = Word48(0xff8000000000ULL);
        machine.memory(020013) = Word48(0x008000000000ULL);
    };
    const auto run_interpreted_to = [](Machine &machine,
                                       std::uint16_t target,
                                       unsigned limit,
                                       const std::string &label) {
        machine.set_translated_routines_enabled(false);
        for (unsigned steps = 0;
             (machine.program_counter() != target
              || machine.right_half()) && steps != limit;
             ++steps) {
            require(machine.step() == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require(machine.program_counter() == target
                    && !machine.right_half(),
                label + " reaches its semantic boundary");
    };

    const std::pair<std::uint16_t, Word48> hot_runtime_code[] = {
        {03314, Word48(0xda069d0c06bdULL)},
        {03374, Word48(0xdc86bf090000ULL)},
        {03375, Word48(0xf00000dc86bfULL)},
        {03376, Word48(0xda06cc090000ULL)},
        {03377, Word48(0xf0a000ba06c8ULL)},
        {03400, Word48(0xbb803a090487ULL)},
        {03401, Word48(0x008000dc0000ULL)},
        {03402, Word48(0xb08063dc0000ULL)},
        {05045, Word48(0x208001020002ULL)},
        {05046, Word48(0x2e0a63208000ULL)},
        {05047, Word48(0x01e067020009ULL)},
        {05050, Word48(0x92400a9a8a3fULL)},
        {05051, Word48(0xac0a2a090000ULL)},
        {07472, Word48(0x090000dca2a9ULL)},
        {07473, Word48(0x8a0f3a80a00dULL)},
        {07474, Word48(0xda069d0c06bdULL)},
        {016406, Word48(0x3080004e1d0aULL)},
        {016407, Word48(0x14993a51e000ULL)},
        {016410, Word48(0xf4aff9f40ff9ULL)},
        {016411, Word48(0x5a80084a8001ULL)},
        {016412, Word48(0x308001dc9d11ULL)},
        {016413, Word48(0x2e1d0d14a93bULL)},
        {016414, Word48(0x1b785d090000ULL)},
        {016415, Word48(0x14a93c1bf85eULL)},
        {016416, Word48(0xda1d061c7894ULL)},
        {016417, Word48(0xf48ff9dc8247ULL)},
        {016420, Word48(0xda1cfe0c06bdULL)},
        {016467, Word48(0x02300d303000ULL)},
        {016470, Word48(0xea1d6ddca323ULL)},
        {016471, Word48(0x14894214b8bdULL)},
        {016472, Word48(0x1418bd02100dULL)},
        {016473, Word48(0x1488bc14a8d3ULL)},
        {016474, Word48(0xdb80001408bdULL)},
        {016475, Word48(0x1488bb1408bcULL)},
        {016476, Word48(0xdc0000090000ULL)},
        {011524, Word48(0xdc86bf090000ULL)},
        {011525, Word48(0xf00000dc86bfULL)},
        {011526, Word48(0xf00000dc935eULL)},
        {011527, Word48(0xf08000dc9340ULL)},
        {011530, Word48(0xda069d0c06bdULL)},
        {016376, Word48(0xf08000021003ULL)},
        {016377, Word48(0x021007021002ULL)},
        {016400, Word48(0x021005021004ULL)},
        {016401, Word48(0x0210010c069dULL)},
        {020667, Word48(0xdc9ce1090000ULL)},
    };
    const auto compare_hot_runtime_block = [
        &hot_runtime_code, &run_interpreted_to,
        &require_same_architectural_state](
            std::uint16_t entry, std::uint16_t target,
            const auto &initialize, const std::string &label) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : hot_runtime_code) {
                machine->memory(address) = word;
            }
            initialize(*machine);
            machine->start(entry);
        }
        require(semantic->step() == poplan::ExecutionStatus::running,
                label + " semantic path keeps running");
        run_interpreted_to(*interpreted, target, 32, label);
        require_same_architectural_state(*semantic, *interpreted, label);
    };

    compare_hot_runtime_block(
        03314, 03275,
        [](Machine &machine) {
            machine.accumulator() = Word48(0765432107654321ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 07000;
        },
        "03314 binding return");
    compare_hot_runtime_block(
        03374, 03277,
        [](Machine &machine) {
            machine.accumulator() = Word48(06600000000003374ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 03235;
            machine.reg(017) = 05000;
        },
        "03374 two-value entry");
    compare_hot_runtime_block(
        03375, 03277,
        [](Machine &machine) {
            machine.accumulator() = Word48(06400000000000123ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 03375;
            machine.reg(017) = 05000;
        },
        "03375 first-value continuation");
    compare_hot_runtime_block(
        03376, 03314,
        [](Machine &machine) {
            machine.accumulator() = Word48(06400000000000123ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.memory(05000) = Word48(06400000000000123ULL);
            machine.memory(02207) = Word48(06400000000000001ULL);
            machine.memory(03453) = Word48(06400000000000002ULL);
            machine.reg(013) = 01234;
            machine.reg(015) = 03376;
            machine.reg(017) = 05001;
        },
        "03376 equal-value path");
    compare_hot_runtime_block(
        03376, 03314,
        [](Machine &machine) {
            machine.accumulator() = Word48(06400000000000123ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.memory(05000) = Word48(06400000000000456ULL);
            machine.memory(02207) = Word48(06400000000000001ULL);
            machine.memory(03453) = Word48(06400000000000002ULL);
            machine.reg(013) = 01234;
            machine.reg(015) = 03376;
            machine.reg(017) = 05001;
        },
        "03376 unequal-value path");

    compare_hot_runtime_block(
        05045, 05143,
        [](Machine &machine) {
            machine.accumulator() = Word48(0765432107654321ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.memory(04463) = Word48();
            machine.reg(002) = 04462;
            machine.reg(011) = 01111;
            machine.reg(012) = 01212;
            machine.reg(015) = 07000;
        },
        "05045 zero generated-continuation address");
    compare_hot_runtime_block(
        05045, 05057,
        [](Machine &machine) {
            machine.accumulator() = Word48(0765432107654321ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.memory(04463) = Word48(04000);
            machine.memory(04000) =
                Word48(std::uint64_t{5} << 39);
            machine.reg(002) = 04462;
            machine.reg(011) = 01111;
            machine.reg(012) = 01212;
            machine.reg(015) = 07000;
        },
        "05045 selected generated continuation");

    compare_hot_runtime_block(
        07472, 021251,
        [](Machine &machine) {
            machine.accumulator() = Word48(0765432107654321ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 07000;
        },
        "07472 character-extractor entry");
    compare_hot_runtime_block(
        07473, 03275,
        [](Machine &machine) {
            machine.memory(07507) = Word48(06400000000000000ULL);
            machine.accumulator() = Word48(06400000000000052ULL);
            machine.remainder() = Word48(07654);
            machine.alu_mode() = 021;
            machine.reg(010) = 01234;
            machine.reg(015) = 07000;
        },
        "07473 character-extractor continuation");

    compare_hot_runtime_block(
        016406, 016421,
        [](Machine &machine) {
            machine.memory(04000) = Word48(0123456701234567ULL);
            machine.memory(04001) = Word48(06400000000000106ULL);
            machine.memory(016753) = Word48(07777777777777777ULL);
            machine.memory(05001) = Word48(0707070707070707ULL);
            machine.accumulator() = Word48(0765432107654321ULL);
            machine.remainder() = Word48(0111111111111111ULL);
            machine.alu_mode() = 023;
            machine.reg(001) = 022261;
            machine.reg(003) = 04000;
            machine.reg(004) = 1;
            machine.reg(005) = 0100;
            machine.reg(015) = 07000;
            machine.reg(017) = 05010;
        },
        "16406 record-word entry");
    compare_hot_runtime_block(
        016413, 016416,
        [](Machine &machine) {
            machine.accumulator() = Word48(01234567);
            machine.remainder() = Word48(07654);
            machine.alu_mode() = 021;
            machine.memory(016754) = Word48(01234567);
            machine.reg(001) = 022261;
            machine.reg(002) = 1;
            machine.reg(015) = 07000;
        },
        "16413 zero first comparison");
    compare_hot_runtime_block(
        016413, 016505,
        [](Machine &machine) {
            machine.accumulator() = Word48(01);
            machine.remainder() = Word48(07654);
            machine.alu_mode() = 021;
            machine.memory(016754) = Word48(03);
            machine.memory(016755) = Word48(02);
            machine.reg(001) = 022261;
            machine.reg(002) = 1;
            machine.reg(015) = 07000;
        },
        "16413 matching second comparison");
    compare_hot_runtime_block(
        016415, 016417,
        [](Machine &machine) {
            machine.accumulator() = Word48(01);
            machine.remainder() = Word48(07654);
            machine.alu_mode() = 021;
            machine.memory(016755) = Word48();
            machine.reg(001) = 022261;
            machine.reg(015) = 07000;
        },
        "16415 nonzero second comparison");
    compare_hot_runtime_block(
        016416, 016505,
        [](Machine &machine) {
            machine.accumulator() = Word48(0765432107654321ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(001) = 022261;
            machine.reg(015) = 07000;
        },
        "16416 loop continuation");

    compare_hot_runtime_block(
        016467, 021443,
        [](Machine &machine) {
            machine.memory(04000) = Word48(07200000000012345ULL);
            machine.accumulator() = Word48(0765432107654321ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(003) = 04000;
            machine.reg(015) = 07000;
            machine.reg(016) = 04567;
            machine.reg(017) = 05000;
        },
        "16467 descriptor-advance entry");
    compare_hot_runtime_block(
        016471, 07000,
        [](Machine &machine) {
            machine.memory(016763) = Word48(0123);
            machine.memory(016556) = Word48(0456);
            machine.memory(016555) = Word48(01);
            machine.memory(016604) = Word48(02);
            machine.memory(05000) = Word48(0765432107654321ULL);
            machine.memory(05001) = Word48(07000);
            machine.accumulator() = Word48(01111);
            machine.remainder() = Word48(02222);
            machine.alu_mode() = 053;
            machine.reg(001) = 022261;
            machine.reg(015) = 016471;
            machine.reg(017) = 05002;
        },
        "16471 nonzero return");
    compare_hot_runtime_block(
        016471, 07000,
        [](Machine &machine) {
            machine.memory(016763) = Word48(0123);
            machine.memory(016556) = Word48(0456);
            machine.memory(016554) = Word48(0777);
            machine.memory(016555) = Word48(012);
            machine.memory(016604) = Word48(012);
            machine.memory(05000) = Word48(0765432107654321ULL);
            machine.memory(05001) = Word48(07000);
            machine.accumulator() = Word48(01111);
            machine.remainder() = Word48(02222);
            machine.alu_mode() = 053;
            machine.reg(001) = 022261;
            machine.reg(015) = 016471;
            machine.reg(017) = 05002;
        },
        "16471 zero update and return");

    compare_hot_runtime_block(
        016376, 03235,
        [](Machine &machine) {
            machine.accumulator() = Word48(07777777777777777ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.memory(05000) = Word48(0765432107654321ULL);
            machine.memory(05001) = Word48(01001);
            machine.memory(05002) = Word48(04004);
            machine.memory(05003) = Word48(05005);
            machine.memory(05004) = Word48(02002);
            machine.memory(05005) = Word48(07007);
            machine.memory(05006) = Word48(03003);
            machine.reg(001) = 01111;
            machine.reg(002) = 02222;
            machine.reg(003) = 03333;
            machine.reg(004) = 04444;
            machine.reg(005) = 05555;
            machine.reg(007) = 07777;
            machine.reg(015) = 016376;
            machine.reg(017) = 05007;
        },
        "16376 saved-register restoration");
    compare_hot_runtime_block(
        016417, 01107,
        [](Machine &machine) {
            machine.accumulator() = Word48(0765432107654321ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.memory(05001) = Word48(02125110124642400ULL);
            machine.reg(015) = 016413;
            machine.reg(017) = 05010;
        },
        "16417 hash-call continuation");
    compare_hot_runtime_block(
        016420, 03275,
        [](Machine &machine) {
            machine.accumulator() = Word48(06440000000001624ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 016420;
            machine.reg(017) = 05010;
        },
        "16420 value-forward continuation");
    compare_hot_runtime_block(
        020667, 016341,
        [](Machine &machine) {
            machine.accumulator() = Word48(0660000000020667ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 03235;
            machine.reg(017) = 05000;
        },
        "20667 generated character entry");

    compare_hot_runtime_block(
        011524, 03277,
        [](Machine &machine) {
            machine.accumulator() = Word48(07040000000000000ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 03235;
            machine.reg(017) = 05000;
        },
        "11524 generated entry");
    compare_hot_runtime_block(
        011525, 03277,
        [](Machine &machine) {
            machine.accumulator() = Word48(07040000000012345ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 011525;
            machine.reg(017) = 05000;
        },
        "11525 first-value continuation");
    compare_hot_runtime_block(
        011526, 011536,
        [](Machine &machine) {
            machine.accumulator() = Word48(06400000000000001ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 011526;
            machine.reg(017) = 05001;
        },
        "11526 second-value continuation");
    compare_hot_runtime_block(
        011527, 011500,
        [](Machine &machine) {
            machine.accumulator() = Word48(06400000000000077ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.memory(05001) = Word48(06400000000000001ULL);
            machine.reg(015) = 011527;
            machine.reg(017) = 05002;
        },
        "11527 indexed-load continuation");
    compare_hot_runtime_block(
        011530, 03275,
        [](Machine &machine) {
            machine.accumulator() = Word48(06400000000000001ULL);
            machine.remainder() = Word48(0123456701234567ULL);
            machine.alu_mode() = 053;
            machine.reg(015) = 011530;
            machine.reg(017) = 05001;
        },
        "11530 binding return");

    const std::pair<std::uint16_t, Word48> entry_01004_code[] = {
        {01004, Word48(0x02200df00000ULL)},
        {01005, Word48(0x2a02013a079fULL)},
        {01006, Word48(0x008000700277ULL)},
        {01007, Word48(0x70027b70023fULL)},
        {01010, Word48(0x700227dc8b05ULL)},
        {01011, Word48(0x090000dc8937ULL)},
        {01012, Word48(0x30800220a035ULL)},
        {01013, Word48(0xda020e0b0731ULL)},
        {01014, Word48(0x30800220a036ULL)},
        {01015, Word48(0x0b0731dc895eULL)},
        {01016, Word48(0x30800220a037ULL)},
        {01017, Word48(0x2b0012308002ULL)},
        {01020, Word48(0x20a0382b8017ULL)},
        {01021, Word48(0x208038203039ULL)},
        {01022, Word48(0x090000dc8927ULL)},
        {01023, Word48(0xdc8b08090000ULL)},
        {01024, Word48(0x090000dc8a07ULL)},
        {01025, Word48(0x700227dc85e8ULL)},
        {01026, Word48(0x708227da0206ULL)},
        {01027, Word48(0x0c0751090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> scan_16005_code[] = {
        {016005, Word48(0x8a1c0501e031ULL)},
        {016006, Word48(0x8000a4ea2253ULL)},
        {016007, Word48(0xe0800102000eULL)},
        {016010, Word48(0xe0800080908eULL)},
        {016011, Word48(0x8b800be08000ULL)},
        {016012, Word48(0x80908f80a0a4ULL)},
        {016013, Word48(0x8b8002e08000ULL)},
        {016014, Word48(0x80909080a090ULL)},
        {016015, Word48(0x8b000ae08000ULL)},
        {016016, Word48(0x80b091e00000ULL)},
        {016017, Word48(0x02200edc0000ULL)},
        {016020, Word48(0x02200df00000ULL)},
        {016021, Word48(0xea0002dc8b18ULL)},
        {016022, Word48(0x8a1c058080a4ULL)},
        {016023, Word48(0x80a091e00000ULL)},
        {016024, Word48(0x02200ef00000ULL)},
        {016025, Word48(0x090000dca247ULL)},
        {016026, Word48(0xf08000f98000ULL)},
        {016027, Word48(0x0c0000090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> format_startup_code[] = {
        {020462, Word48(0x10a02110102cULL)},
        {020463, Word48(0x10002bea216cULL)},
        {020464, Word48(0x01f003108044ULL)},
        {020465, Word48(0x1050221b800aULL)},
        {020466, Word48(0xea216e105023ULL)},
        {020467, Word48(0x1b800aea2170ULL)},
        {020470, Word48(0xe0800010002dULL)},
        {020471, Word48(0xe0800110002eULL)},
        {020472, Word48(0xea2156f08000ULL)},
        {020473, Word48(0x02100102000dULL)},
        {020474, Word48(0x0c21bc090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> format_exit_code[] = {
        {020475, Word48(0x02200d023001ULL)},
        {020476, Word48(0xf000001a212eULL)},
        {020477, Word48(0x09000002b008ULL)},
        {020500, Word48(0x100045dcaba1ULL)},
        {020501, Word48(0x10a024101032ULL)},
        {020502, Word48(0x100031108045ULL)},
        {020503, Word48(0x01f007105044ULL)},
        {020504, Word48(0x1b0017104025ULL)},
        {020505, Word48(0x090000dcaba1ULL)},
        {020506, Word48(0x10a026101036ULL)},
        {020507, Word48(0x100035033004ULL)},
        {020510, Word48(0x090000dcaba1ULL)},
        {020511, Word48(0x10a027101039ULL)},
        {020512, Word48(0x100038ea215eULL)},
        {020513, Word48(0x090000dca1bcULL)},
        {020514, Word48(0xea2168f08000ULL)},
        {020515, Word48(0x02100102000dULL)},
        {020516, Word48(0x0c21bc090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> output_cleanup_code[] = {
        {07533, Word48(0xf00000090000ULL)},
        {07534, Word48(0x02200dea0f65ULL)},
        {07535, Word48(0xe43fffdca323ULL)},
        {07536, Word48(0xba0f4cb34032ULL)},
        {07537, Word48(0xb08017b00034ULL)},
        {07540, Word48(0xb08030b01019ULL)},
        {07541, Word48(0x02100ddc0000ULL)},
    };
    const std::pair<std::uint16_t, Word48> supervisor_setup_code[] = {
        {020144, Word48(0x8a206480800fULL)},
        {020145, Word48(0x090000028043ULL)},
        {020146, Word48(0x808010028042ULL)},
        {020147, Word48(0x0c2174090000ULL)},
        {020150, Word48(0x8a2064800013ULL)},
        {020151, Word48(0x090000dca174ULL)},
        {020152, Word48(0x8a2064028041ULL)},
        {020153, Word48(0x02000c80a011ULL)},
        {020154, Word48(0x8b000d090000ULL)},
        {020155, Word48(0x80800f028043ULL)},
        {020156, Word48(0x808010028042ULL)},
        {020157, Word48(0xea1600c2500eULL)},
        {020160, Word48(0x8080130c060cULL)},
        {020161, Word48(0x099e08fa0001ULL)},
        {020162, Word48(0x8c0009090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> memory_bound_code[] = {
        {020660, Word48(0x00800f02000eULL)},
        {020661, Word48(0x02200e090b42ULL)},
        {020662, Word48(0x00000001f003ULL)},
        {020663, Word48(0x091e08006000ULL)},
        {020664, Word48(0x01e028e00000ULL)},
        {020665, Word48(0xdc0000090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> message_output_code[] = {
        {020674, Word48(0x8a21bce2400cULL)},
        {020675, Word48(0x0920ff008000ULL)},
        {020676, Word48(0x8b000901e034ULL)},
        {020677, Word48(0x80a00d800010ULL)},
        {020700, Word48(0x09000083900cULL)},
        {020701, Word48(0x090000839010ULL)},
        {020702, Word48(0x09000083900cULL)},
        {020703, Word48(0x0920fd008000ULL)},
        {020704, Word48(0x80900bdb0000ULL)},
        {020705, Word48(0x09000083400eULL)},
        {020706, Word48(0xdc0000090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> record_mask_code[] = {
        {021107, Word48(0x8a223d808017ULL)},
        {021110, Word48(0x02000c809013ULL)},
        {021111, Word48(0x02300ef0a000ULL)},
        {021112, Word48(0x80001702200cULL)},
        {021113, Word48(0x80a015e00001ULL)},
        {021114, Word48(0xc08001809014ULL)},
        {021115, Word48(0x02300e01e028ULL)},
        {021116, Word48(0xf0a000c00001ULL)},
        {021117, Word48(0xdc0000090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> retry_unwind_code[] = {
        {025223, Word48(0x02200d003000ULL)},
        {025224, Word48(0x003000dc9c69ULL)},
        {025225, Word48(0x0baa94090000ULL)},
        {025226, Word48(0xf08000da2a96ULL)},
        {025227, Word48(0x0b9c65f98000ULL)},
        {025230, Word48(0x0c0000090000ULL)},
    };
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : entry_01004_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(002) = 02345;
            machine->reg(003) = 03456;
            machine->reg(007) = 02000;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->start(01004);
        }
        semantic->step();
        run_interpreted_to(*interpreted, 05405, 24, "01004 entry setup");
        require_same_architectural_state(
            *semantic, *interpreted, "01004 entry setup");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : entry_01004_code) {
                machine->memory(address) = word;
            }
            machine->reg(002) = 01001;
            machine->reg(003) = 03000;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->memory(03002) = Word48(012345);
            machine->memory(01070) = Word48(07654);
            machine->memory(01071) = Word48(012345);
            machine->memory(01072) = Word48(06701);
            machine->accumulator() = Word48(0777);
            machine->remainder() = Word48(0666);
            machine->alu_mode() = 051;
            machine->start(01016);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 04447, 24, "01016 allocation path");
        require_same_architectural_state(
            *semantic, *interpreted, "01016 allocation path");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : scan_16005_code) {
                machine->memory(address) = word;
            }
            machine->memory(021124) = Word48(03000);
            machine->memory(03000) = Word48(0100);
            machine->memory(016223) = Word48();
            machine->memory(016224) = Word48();
            machine->memory(016225) = Word48(0100);
            machine->memory(016226) = Word48(1);
            machine->accumulator() = Word48();
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(010) = 01234;
            machine->reg(015) = 07000;
            machine->reg(016) = 04567;
            machine->reg(017) = 05000;
            machine->start(016005);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 07000, 48, "16005 matching scan path");
        require_same_architectural_state(
            *semantic, *interpreted, "16005 matching scan path");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : scan_16005_code) {
                machine->memory(address) = word;
            }
            machine->memory(021124) = Word48(03000);
            machine->memory(03000) = Word48(1);
            machine->memory(016223) = Word48(1);
            machine->accumulator() = Word48();
            machine->remainder() = Word48(012345);
            machine->alu_mode() = 051;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->start(016005);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 05430, 32, "16005 allocation path");
        require_same_architectural_state(
            *semantic, *interpreted, "16005 allocation path");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : scan_16005_code) {
                machine->memory(address) = word;
            }
            machine->memory(05000) = Word48(07000);
            machine->memory(016251) = Word48(0123456701234567ULL);
            machine->memory(016226) = Word48(0765432107654321ULL);
            machine->accumulator() = Word48(01111);
            machine->remainder() = Word48(02222);
            machine->alu_mode() = 053;
            machine->reg(010) = 01234;
            machine->reg(015) = 016022;
            machine->reg(016) = 04000;
            machine->reg(017) = 05001;
            machine->start(016022);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 021107, 16, "16022 allocated scan record");
        require_same_architectural_state(
            *semantic, *interpreted, "16022 allocated scan record");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : format_startup_code) {
                machine->memory(address) = word;
            }
            machine->memory(020517) = Word48(0x00000f0f0f0fULL);
            machine->memory(020520) = Word48(0x0000001e3660ULL);
            machine->memory(020521) = Word48(0x000000107ac0ULL);
            machine->memory(020554) = Word48(0x242e21302e25ULL);
            machine->memory(020555) = Word48(0x0f3332302e0fULL);
            machine->memory(020556) = Word48(0x242e21303a29ULL);
            machine->memory(020557) = Word48(0x0f24252d3b0fULL);
            machine->memory(020560) = Word48(0x242e21303a29ULL);
            machine->memory(020561) = Word48(0x0f2225372530ULL);
            machine->memory(020562) = Word48(0234567);
            machine->memory(05000) = Word48(07000);
            machine->memory(05001) = Word48(01234);
            machine->memory(05002) = Word48(05670);
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(001) = 020456;
            machine->reg(015) = 020462;
            machine->reg(016) = 010;
            machine->reg(017) = 05003;
            machine->start(020462);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 020674, 40, "20462 startup formatter return");
        require_same_architectural_state(
            *semantic, *interpreted, "20462 startup formatter return");
    }
    {
        auto machine = std::make_unique<Machine>();
        machine->reg(001) = 01234;
        machine->reg(015) = 07000;
        machine->reg(017) = 05000;
        const std::time_t time_before = std::time(nullptr);
        machine->start(020475);
        require(machine->step() == poplan::ExecutionStatus::running,
                "20475 exit formatter entry keeps running");
        const std::time_t time_after = std::time(nullptr);
        constexpr std::uint64_t jiffies_per_day = 24 * 60 * 60 * 50;
        const std::uint64_t jiffies = machine->memory(020563).raw();
        require(machine->program_counter() == 025641
                    && machine->reg(001) == 020456
                    && machine->reg(015) == 020501
                    && machine->reg(016) == 010
                    && machine->reg(017) == 05002
                    && machine->memory(05000) == Word48(07000)
                    && machine->memory(05001) == Word48(01234),
                "20475 saves its frame and enters FORMAT_NUMBER");
        require(circular_distance(jiffies,
                                  whole_second_jiffies(time_before),
                                  jiffies_per_day) < 50
                    || circular_distance(jiffies,
                                         whole_second_jiffies(time_after),
                                         jiffies_per_day) < 50,
                "20475 stores current local time in 1/50-second jiffies");
    }
    for (const std::uint16_t entry :
         {020501, 020506, 020511, 020514}) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : format_exit_code) {
                machine->memory(address) = word;
            }
            machine->memory(020522) = Word48(0000001703607442ULL);
            machine->memory(020523) = Word48(0000000020365400ULL);
            machine->memory(020524) = Word48(0000001710430045ULL);
            machine->memory(020525) = Word48(0000001703607572ULL);
            machine->memory(020562) = Word48(0200000);
            machine->memory(020563) = Word48(0200040);
            machine->memory(05000) = Word48(07000);
            machine->memory(05001) = Word48(01234);
            machine->memory(05002) = Word48(05670);
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(001) = 020456;
            machine->reg(015) = entry;
            machine->reg(016) = 04567;
            machine->reg(017) = 05003;
            machine->start(entry);
        }
        semantic->step();
        const std::uint16_t target =
            entry == 020511 || entry == 020514 ? 020674 : 025641;
        run_interpreted_to(
            *interpreted, target, 24, "20475 exit formatter continuation");
        require_same_architectural_state(
            *semantic, *interpreted, "20475 exit formatter continuation");
    }
    for (const std::uint16_t entry : {020144, 020150}) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : supervisor_setup_code) {
                machine->memory(address) = word;
            }
            machine->memory(020163) = Word48(020150);
            machine->memory(020164) = Word48(1);
            machine->memory(020165) = Word48(020);
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(010) = 01234;
            machine->reg(015) = 07000;
            machine->reg(016) = 04567;
            machine->start(entry);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 020564, 16, "20144 supervisor setup entry");
        require_same_architectural_state(
            *semantic, *interpreted, "20144 supervisor setup entry");
    }
    for (const Word48 input : {Word48(0123), Word48(020)}) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : supervisor_setup_code) {
                machine->memory(address) = word;
            }
            machine->memory(020163) = Word48(020150);
            machine->memory(020164) = Word48(1);
            machine->memory(020165) = Word48(020);
            machine->memory(020167) = Word48(0671234567012345ULL);
            machine->memory(017010) = Word48(04777);
            machine->accumulator() = input;
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(010) = 01234;
            machine->reg(014) = 02345;
            machine->reg(015) = 020152;
            machine->reg(016) = 04567;
            machine->reg(017) = 05555;
            machine->start(020152);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 03014, 32, "20152 supervisor setup return");
        require_same_architectural_state(
            *semantic, *interpreted, "20152 supervisor setup return");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : memory_bound_code) {
                machine->memory(address) = word;
            }
            machine->memory(017) = Word48(04000);
            machine->memory(017010) = Word48(021300);
            machine->memory(021300) = Word48(0016760000033064ULL);
            machine->memory(04000) = Word48(0765432107654321ULL);
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(0765432107654321ULL);
            machine->alu_mode() = 063;
            machine->reg(015) = 07000;
            machine->reg(016) = 04567;
            machine->start(020660);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 07000, 24, "20660 memory-bound setup");
        require_same_architectural_state(
            *semantic, *interpreted, "20660 memory-bound setup");
    }
    for (const auto [available, output_status] :
         {std::pair{false, Word48()}, std::pair{true, Word48()},
          std::pair{true, Word48(2)}}) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : message_output_code) {
                machine->memory(address) = word;
            }
            machine->memory(020707) = Word48(2);
            machine->memory(020710) = Word48(0x040000ffffffULL);
            machine->memory(020711) = Word48(0xc10000c4000aULL);
            machine->memory(020712) = Word48(0xc00000c00000ULL);
            machine->memory(020713) = Word48(0x000000800000ULL);
            machine->memory(020375) = output_status;
            machine->memory(020377) = available ? Word48(012) : Word48();
            machine->memory(020550) = Word48(0x223125232e0fULL);
            machine->memory(020551) = Word48(0x22202c0f242eULL);
            machine->memory(020552) = Word48(0x21302e232e0fULL);
            machine->memory(020553) = Word48(0x7a0000000000ULL);
            machine->console_available() = available;
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(010) = 01234;
            machine->reg(014) = 02345;
            machine->reg(015) = 07000;
            machine->reg(016) = 020550;
            machine->start(020674);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 07000, 40, "20674 message output");
        require_same_architectural_state(
            *semantic, *interpreted, "20674 message output");
        require(semantic->console_output() == interpreted->console_output(),
                "20674 semantic and raw paths emit the same GOST bytes");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : output_cleanup_code) {
                machine->memory(address) = word;
            }
            machine->memory(07544) = Word48(0377);
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(013) = 01234;
            machine->reg(015) = 07000;
            machine->reg(016) = 04567;
            machine->reg(017) = 05000;
            machine->start(07533);
        }
        semantic->step();
        run_interpreted_to(*interpreted, 021443, 12, "07533 cleanup entry");
        require_same_architectural_state(
            *semantic, *interpreted, "07533 cleanup entry");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : output_cleanup_code) {
                machine->memory(address) = word;
            }
            machine->memory(07543) = Word48(077601);
            machine->memory(07574) = Word48(0x300000000f66ULL);
            machine->memory(05000) = Word48(0765432107654321ULL);
            machine->memory(05001) = Word48(07000);
            machine->accumulator() = Word48(01111);
            machine->remainder() = Word48(02222);
            machine->alu_mode() = 053;
            machine->reg(013) = 01234;
            machine->reg(015) = 07536;
            machine->reg(016) = 07545;
            machine->reg(017) = 05002;
            machine->start(07536);
        }
        semantic->step();
        run_interpreted_to(*interpreted, 07000, 20, "07536 cleanup return");
        require_same_architectural_state(
            *semantic, *interpreted, "07536 cleanup return");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : record_mask_code) {
                machine->memory(address) = word;
            }
            machine->memory(021120) = Word48(0xffffffff8000ULL);
            machine->memory(021121) = Word48(0xff8000ffffffULL);
            machine->memory(021122) = Word48(0xe62253000000ULL);
            machine->memory(021124) = Word48(0xe62253002253ULL);
            machine->memory(02254) = Word48(0765432107654321ULL);
            machine->memory(03001) = Word48(0123456701234567ULL);
            machine->accumulator() = Word48(01111);
            machine->remainder() = Word48(02222);
            machine->alu_mode() = 053;
            machine->reg(010) = 01234;
            machine->reg(014) = 02345;
            machine->reg(015) = 07000;
            machine->reg(016) = 03000;
            machine->reg(017) = 05000;
            machine->start(021107);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 07000, 32, "21107 record mask update");
        require_same_architectural_state(
            *semantic, *interpreted, "21107 record mask update");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : retry_unwind_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->start(025223);
        }
        semantic->step();
        run_interpreted_to(*interpreted, 016151, 12, "25223 retry entry");
        require_same_architectural_state(
            *semantic, *interpreted, "25223 retry entry");
    }
    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : retry_unwind_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(012345);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 004;
            machine->reg(015) = 025225;
            machine->reg(017) = 05002;
            machine->start(025225);
        }
        semantic->step();
        run_interpreted_to(*interpreted, 016151, 8, "25225 retry loop");
        require_same_architectural_state(
            *semantic, *interpreted, "25225 retry loop");
    }
    for (const Word48 stacked : {Word48(0123), Word48()}) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : retry_unwind_code) {
                machine->memory(address) = word;
            }
            machine->memory(05000) = Word48(07000);
            machine->memory(05001) = stacked;
            machine->accumulator() = Word48();
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 004;
            machine->reg(015) = 025225;
            machine->reg(017) = 05002;
            machine->start(025225);
        }
        semantic->step();
        const std::uint16_t target =
            stacked.raw() == 0 ? 07000 : 016145;
        run_interpreted_to(*interpreted, target, 12, "25225 unwind path");
        require_same_architectural_state(
            *semantic, *interpreted, "25225 unwind path");
    }

    const std::pair<std::uint16_t, Word48> cold_start_entry_code[] = {
        {05230, Word48(0x0220011a0a98ULL)},
        {05231, Word48(0x100068022003ULL)},
        {05232, Word48(0x10006a3a0000ULL)},
        {05233, Word48(0x022002100069ULL)},
        {05234, Word48(0x02200410006bULL)},
        {05235, Word48(0x02200d10006cULL)},
        {05236, Word48(0x0982004a0000ULL)},
        {05237, Word48(0x090000dca0b3ULL)},
    };
    const std::pair<std::uint16_t, Word48> cold_start_copy_code[] = {
        {05255, Word48(0x3e8ab1ea7ff9ULL)},
        {05256, Word48(0x490001e08007ULL)},
        {05257, Word48(0x0921a7e00007ULL)},
        {05260, Word48(0xef8aae090000ULL)},
        {05261, Word48(0x099e096a0000ULL)},
        {05262, Word48(0xea2179e08000ULL)},
        {05263, Word48(0x109055091e0aULL)},
        {05264, Word48(0x00a00010a056ULL)},
        {05265, Word48(0xe00000dca064ULL)},
    };
    const std::pair<std::uint16_t, Word48> cold_start_init_code[] = {
        {05266, Word48(0x091e09008000ULL)},
        {05267, Word48(0x01e02610a057ULL)},
        {05270, Word48(0x10006613a066ULL)},
        {05271, Word48(0x3e8abb40800aULL)},
        {05272, Word48(0x0920fa000003ULL)},
        {05273, Word48(0xea0af80920feULL)},
        {05274, Word48(0x0080011b0026ULL)},
        {05275, Word48(0xea0af5090000ULL)},
        {05276, Word48(0xca0280e08000ULL)},
        {05277, Word48(0xc000cfc000f7ULL)},
        {05300, Word48(0xe08001c000cbULL)},
        {05301, Word48(0xc000f309034bULL)},
        {05302, Word48(0x0080000921b6ULL)},
        {05303, Word48(0x000000ea156bULL)},
        {05304, Word48(0x10806101e028ULL)},
        {05305, Word48(0xe0a000109058ULL)},
        {05306, Word48(0xe0a000e00000ULL)},
        {05307, Word48(0xea156d108062ULL)},
        {05310, Word48(0x01e028e0a000ULL)},
        {05311, Word48(0x109058e0a000ULL)},
        {05312, Word48(0xe00000408000ULL)},
        {05313, Word48(0x090000dc9c05ULL)},
    };
    const std::pair<std::uint16_t, Word48> cold_start_finish_code[] = {
        {05314, Word48(0x09067d000000ULL)},
        {05315, Word48(0x2a1e03090000ULL)},
        {05316, Word48(0x2a8001208000ULL)},
        {05317, Word48(0x1b003e109059ULL)},
        {05320, Word48(0x1b8053022002ULL)},
        {05321, Word48(0x10a05a1b0053ULL)},
        {05322, Word48(0x208000028080ULL)},
        {05323, Word48(0x10a05b1b0036ULL)},
        {05324, Word48(0x208000da0aceULL)},
        {05325, Word48(0x0c1c65090000ULL)},
        {05326, Word48(0x3e8add108067ULL)},
        {05327, Word48(0x1b0045108064ULL)},
        {05330, Word48(0x00302501e033ULL)},
        {05331, Word48(0x01e06b01e036ULL)},
        {05332, Word48(0x198067000001ULL)},
        {05333, Word48(0x090000dc9c2cULL)},
        {05334, Word48(0x198067000000ULL)},
        {05335, Word48(0x3e8adedca1bbULL)},
        {05336, Word48(0x090000dca12eULL)},
        {05337, Word48(0x3e8ae110805cULL)},
        {05340, Word48(0x091728000000ULL)},
        {05341, Word48(0x10806c02000dULL)},
        {05342, Word48(0x10806b020004ULL)},
        {05343, Word48(0x10806a020003ULL)},
        {05344, Word48(0x108069020002ULL)},
        {05345, Word48(0x108068020001ULL)},
        {05346, Word48(0xdc0000090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> io_init_code[] = {
        {020263, Word48(0x8a20789a2b0dULL)},
        {020264, Word48(0x090000039000ULL)},
        {020265, Word48(0x800080809064ULL)},
        {020266, Word48(0x8000868b8043ULL)},
        {020267, Word48(0x808075900000ULL)},
        {020270, Word48(0x808077900001ULL)},
        {020271, Word48(0x80807380007bULL)},
        {020272, Word48(0x8c0046090000ULL)},
        {020273, Word48(0x808076900000ULL)},
        {020274, Word48(0x808078900001ULL)},
        {020275, Word48(0x80807480007bULL)},
        {020276, Word48(0x808079900002ULL)},
        {020277, Word48(0x80808001e058ULL)},
        {020300, Word48(0xf0000001e028ULL)},
        {020301, Word48(0xf0a00080007dULL)},
        {020302, Word48(0x80808001e058ULL)},
        {020303, Word48(0x8000818b0051ULL)},
        {020304, Word48(0x01200080a05eULL)},
        {020305, Word48(0x8b8051808081ULL)},
        {020306, Word48(0x80a064809080ULL)},
        {020307, Word48(0x8b8051808080ULL)},
        {020310, Word48(0x013000800087ULL)},
        {020311, Word48(0x80808701e034ULL)},
        {020312, Word48(0x80008080a07bULL)},
        {020313, Word48(0x80007b808080ULL)},
        {020314, Word48(0x80a07280007fULL)},
        {020315, Word48(0x80806780007eULL)},
        {020316, Word48(0x808065800082ULL)},
        {020317, Word48(0x80805f800092ULL)},
        {020320, Word48(0xdc0000090000ULL)},
    };
    const std::pair<std::uint16_t, Word48> io_init_data[] = {
        {020322, Word48(0x400000000000ULL)},
        {020323, Word48(0x800000000000ULL)},
        {020324, Word48(0xffffffffffffULL)},
        {020325, Word48(0xffffffff00ffULL)},
        {020326, Word48(0x000000000001ULL)},
        {020327, Word48(0xff0000000000ULL)},
        {020330, Word48(0xfffffffffffeULL)},
        {020331, Word48(0x0000000005dcULL)},
        {020332, Word48(0x0000000000ffULL)},
        {020333, Word48(0x000000000002ULL)},
        {020334, Word48(0x000000ffffffULL)},
        {020335, Word48(0x1fff00000000ULL)},
        {020336, Word48(0x040000ffffffULL)},
        {020337, Word48(0x000000000005ULL)},
        {020340, Word48(0x191c1c1c00ffULL)},
        {020341, Word48(0x002100002100ULL)},
        {020342, Word48(0x000000800000ULL)},
        {020343, Word48(0x002120002120ULL)},
        {020344, Word48(0x008000800000ULL)},
        {020345, Word48(0x223a0f24332cULL)},
        {020346, Word48(0x202b280f312bULL)},
        {020347, Word48(0x28382a2e2c0fULL)},
        {020350, Word48(0x242e2b232e0fULL)},
        {020351, Word48(0xff0000000000ULL)},
        {020352, Word48(0x8100a88400b5ULL)},
        {020353, Word48(0x81c088840091ULL)},
        {020354, Word48(0x81c08884009cULL)},
        {020355, Word48(0x00000000003cULL)},
        {020356, Word48(0x00000000007eULL)},
        {020357, Word48(0x00000000003cULL)},
        {020360, Word48(0x00000000004fULL)},
        {020361, Word48(0x300000002120ULL)},
    };

    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : cold_start_entry_code) {
                machine->memory(address) = word;
            }
            machine->memory(01000) = Word48(04567);
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(001) = 01234;
            machine->reg(002) = 02345;
            machine->reg(003) = 03456;
            machine->reg(004) = 04567;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->start(05230);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 020263, 24, "05230 cold-start entry");
        require_same_architectural_state(
            *semantic, *interpreted, "05230 cold-start entry");
    }

    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : cold_start_copy_code) {
                machine->memory(address) = word;
            }
            machine->reg(001) = 05230;
            machine->reg(003) = 0;
            machine->reg(004) = 06000;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->memory(017011) = Word48(01234);
            machine->memory(017012) = Word48(0765432107654321ULL);
            machine->memory(020571) = Word48(0123456701234567ULL);
            machine->memory(05355) = Word48(0777777777777777ULL);
            machine->memory(05356) = Word48(0000000000000765ULL);
            for (unsigned offset = 1; offset != 8; ++offset) {
                machine->memory(static_cast<std::uint16_t>(06000 + offset)) =
                    Word48(01000 + offset);
            }
            machine->accumulator() = Word48(0777);
            machine->remainder() = Word48(0666);
            machine->alu_mode() = 051;
            machine->start(05255);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 020144, 80, "05255 startup table copy");
        require_same_architectural_state(
            *semantic, *interpreted, "05255 startup table copy");
    }

    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : cold_start_init_code) {
                machine->memory(address) = word;
            }
            machine->reg(001) = 05230;
            machine->reg(003) = 0;
            machine->reg(004) = 06000;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->memory(017011) = Word48(0123456701234567ULL);
            machine->memory(06012) = Word48(0765432107654321ULL);
            machine->memory(020377) = Word48(1);
            machine->memory(05365) = Word48(01111);
            machine->memory(05366) = Word48(02222);
            machine->memory(01513) = Word48(03333);
            machine->memory(05357) = Word48(04444);
            machine->memory(05360) = Word48(0777777777777777ULL);
            machine->memory(05371) = Word48(05555);
            machine->memory(05372) = Word48(06666);
            machine->memory(012553) = Word48(012345);
            machine->memory(012555) = Word48(076543);
            machine->memory(06000) = Word48(070707);
            machine->accumulator() = Word48(0777);
            machine->remainder() = Word48(0666);
            machine->alu_mode() = 053;
            machine->start(05266);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 016005, 96, "05266 startup state setup");
        require_same_architectural_state(
            *semantic, *interpreted, "05266 startup state setup");
    }

    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : cold_start_finish_code) {
                machine->memory(address) = word;
            }
            machine->reg(001) = 05230;
            machine->reg(003) = 0;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->memory(017004) = Word48();
            machine->memory(05377) = Word48();
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 061;
            machine->start(05314);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 020673, 40, "05314 cold-start scan exit");
        require_same_architectural_state(
            *semantic, *interpreted, "05314 cold-start scan exit");
    }

    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : cold_start_finish_code) {
                machine->memory(address) = word;
            }
            machine->reg(001) = 05230;
            machine->reg(002) = 0777;
            machine->reg(003) = 0666;
            machine->reg(004) = 0555;
            machine->reg(015) = 05341;
            machine->memory(05400) = Word48(01234);
            machine->memory(05401) = Word48(02345);
            machine->memory(05402) = Word48(03456);
            machine->memory(05403) = Word48(04567);
            machine->memory(05404) = Word48(07000);
            machine->accumulator() = Word48(01111);
            machine->remainder() = Word48(02222);
            machine->alu_mode() = 063;
            machine->start(05341);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 07000, 16, "05341 cold-start restoration");
        require_same_architectural_state(
            *semantic, *interpreted, "05341 cold-start restoration");
    }

    const auto compare_io_init = [
        &io_init_code, &io_init_data, &run_interpreted_to,
        &require_same_architectural_state](bool console_available,
                                           const std::string &label) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : io_init_code) {
                machine->memory(address) = word;
            }
            for (const auto &[address, word] : io_init_data) {
                machine->memory(address) = word;
            }
            machine->console_available() = console_available;
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 063;
            machine->reg(010) = 01234;
            machine->reg(011) = 02345;
            machine->reg(015) = 07000;
            machine->reg(016) = 03456;
            machine->reg(017) = 05000;
            machine->start(020263);
        }
        semantic->step();
        run_interpreted_to(*interpreted, 07000, 80, label);
        require_same_architectural_state(
            *semantic, *interpreted, label);
    };
    compare_io_init(true, "20263 available-console initialization");
    compare_io_init(false, "20263 unavailable-console initialization");

    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            load_descriptor_scan_code(*machine);
            machine->memory(04463) = Word48(04321);
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(0123456701234567ULL);
            machine->alu_mode() = 067;
            machine->reg(001) = 01234;
            machine->reg(002) = 02345;
            machine->reg(003) = 03456;
            machine->reg(015) = 07000;
            machine->reg(017) = 05000;
            machine->start(017624);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 017762, 32, "17624 descriptor-scan entry");
        require_same_architectural_state(
            *semantic, *interpreted, "17624 descriptor-scan entry");
    }

    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            load_descriptor_scan_code(*machine);
            machine->memory(04462) = Word48(0123456701234567ULL);
            machine->accumulator() = Word48(0765432107654321ULL);
            machine->remainder() = Word48(012345);
            machine->alu_mode() = 053;
            machine->reg(001) = 017624;
            machine->reg(002) = 04462;
            machine->reg(003) = 0;
            machine->reg(015) = 07111;
            machine->reg(017) = 05200;
            machine->start(017633);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 017774, 48, "17633 empty descriptor path");
        require_same_architectural_state(
            *semantic, *interpreted, "17633 empty descriptor path");
    }

    {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            load_descriptor_scan_code(*machine);
            machine->memory(020061) = Word48(0765432107654321ULL);
            machine->memory(03645) = Word48(0123456701234567ULL);
            machine->memory(05000) = Word48(07000);
            machine->memory(05001) = Word48(01234);
            machine->memory(05002) = Word48(02345);
            machine->memory(05003) = Word48(03456);
            machine->accumulator() = Word48(01111);
            machine->remainder() = Word48(02222);
            machine->alu_mode() = 063;
            machine->reg(001) = 017624;
            machine->reg(002) = 04462;
            machine->reg(003) = 05555;
            machine->reg(015) = 017756;
            machine->reg(017) = 05004;
            machine->start(017756);
        }
        semantic->step();
        run_interpreted_to(
            *interpreted, 07000, 16, "17756 descriptor-scan return");
        require_same_architectural_state(
            *semantic, *interpreted, "17756 descriptor-scan return");
    }

    const std::pair<std::uint16_t, Word48> generated_dispatch_code[] = {
        {013121, Word48(0x02200df00000ULL)},
        {013122, Word48(0x1081351b00baULL)},
        {013123, Word48(0x008000100135ULL)},
        {013124, Word48(0x108133dc86bdULL)},
        {013125, Word48(0x090377008000ULL)},
        {013126, Word48(0x090000dc85e8ULL)},
        {013127, Word48(0xf980000c0000ULL)},
    };
    const auto compare_generated_dispatch = [
        &generated_dispatch_code,
        &require_same_architectural_state](std::uint16_t entry,
                                           Word48 pending) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : generated_dispatch_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(012345);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 012635;
            machine->reg(015) = 07700;
            machine->reg(017) = 04000;
            machine->memory(013322) = pending;
            machine->memory(013320) =
                Word48(06400000000000040ULL);
            machine->memory(01567) =
                Word48(06600000000007475ULL);
            if (entry == 013127) {
                machine->reg(017) = 04002;
                machine->memory(04001) = Word48(07700);
            }
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        for (const std::uint16_t address : {013121, 013125, 013127}) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 24;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    "13121 instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "13121 instruction path reaches its semantic boundary");
        require_same_architectural_state(
            *semantic, *interpreted, "13121 generated dispatch");
        return continuation;
    };

    require(compare_generated_dispatch(013121, Word48(1)) == 03275,
            "13121 sends its pending value to PUSH_ACC");
    require(compare_generated_dispatch(013121, Word48()) == 07700,
            "13121 takes the computed empty return");
    require(compare_generated_dispatch(013125, Word48(1)) == 02750,
            "13125 dispatches the shared descriptor");
    require(compare_generated_dispatch(013127, Word48(1)) == 07700,
            "13127 pops and follows its indirect return");

    const std::pair<std::uint16_t, Word48> generated_cycle_code[] = {
        {013007, Word48(0x02200df00000ULL)},
        {013010, Word48(0x01f003108130ULL)},
        {013011, Word48(0x10512c1b0070ULL)},
        {013012, Word48(0x090000dc9627ULL)},
        {013017, Word48(0x02200df00000ULL)},
        {013020, Word48(0x10813210b117ULL)},
        {013021, Word48(0x1b0077108108ULL)},
        {013022, Word48(0x100141008000ULL)},
        {013023, Word48(0x1001391c0079ULL)},
        {013024, Word48(0x008000100141ULL)},
        {013025, Word48(0x108118100139ULL)},
        {013026, Word48(0x10813801f013ULL)},
        {013027, Word48(0x1b807d10b141ULL)},
        {013030, Word48(0x090000dc963aULL)},
        {013047, Word48(0x02200df00000ULL)},
        {013050, Word48(0x10813010b108ULL)},
        {013051, Word48(0x100130108132ULL)},
        {013052, Word48(0x10a1181b8090ULL)},
        {013053, Word48(0x10813610b108ULL)},
        {013054, Word48(0x1001361c0095ULL)},
        {013062, Word48(0xf980000c0000ULL)},
        {013111, Word48(0x02200df00000ULL)},
        {013112, Word48(0x090000dc9651ULL)},
        {013130, Word48(0x02200df00000ULL)},
        {013131, Word48(0x108119100133ULL)},
        {013132, Word48(0x10812901f002ULL)},
        {013133, Word48(0x004000100129ULL)},
        {013134, Word48(0x1b00c2006000ULL)},
        {013135, Word48(0x100129108114ULL)},
        {013136, Word48(0x100133090000ULL)},
        {013137, Word48(0x10812901f003ULL)},
        {013140, Word48(0x104110100134ULL)},
        {013141, Word48(0x01f002004000ULL)},
        {013142, Word48(0x106129100129ULL)},
        {013143, Word48(0x10813410911bULL)},
        {013144, Word48(0x100134ea7ff5ULL)},
        {013145, Word48(0x008000090000ULL)},
        {013146, Word48(0x190147e0000bULL)},
        {013147, Word48(0xef9666008000ULL)},
        {013150, Word48(0x10012e01f003ULL)},
        {013151, Word48(0x1081341b00d5ULL)},
        {013152, Word48(0x10812e10b108ULL)},
        {013153, Word48(0x10012e108134ULL)},
        {013154, Word48(0x10e12510411cULL)},
        {013155, Word48(0x10911bf00000ULL)},
        {013156, Word48(0x10f124019040ULL)},
        {013157, Word48(0x10613419812eULL)},
        {013160, Word48(0x101146100134ULL)},
        {013161, Word48(0x1c00cc090000ULL)},
        {013162, Word48(0x10811d100140ULL)},
        {013163, Word48(0x00800010012fULL)},
        {013164, Word48(0x100130100136ULL)},
        {013165, Word48(0x10013110013aULL)},
        {013166, Word48(0x108108100137ULL)},
        {013167, Word48(0x10013510811bULL)},
        {013170, Word48(0x100138108144ULL)},
        {013171, Word48(0x1b80dddc9651ULL)},
        {013172, Word48(0xf980000c0000ULL)},
        {013217, Word48(0x1081281b80f5ULL)},
        {013220, Word48(0x108108100128ULL)},
        {013221, Word48(0xdc0000090000ULL)},
        {013222, Word48(0x10b108100128ULL)},
        {013223, Word48(0xea7fd7108129ULL)},
        {013224, Word48(0x190129e0302aULL)},
        {013225, Word48(0xef9694f00000ULL)},
        {013226, Word48(0xdc0000090000ULL)},
    };
    const auto compare_generated_cycle = [
        &generated_cycle_code,
        &require_same_architectural_state](
            std::uint16_t entry, Word48 sequence_number) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : generated_cycle_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 012635;
            machine->reg(015) = 07000;
            machine->reg(016) = 01234;
            machine->reg(017) = 04000;

            machine->memory(013245) = Word48(1);
            machine->memory(013255) = Word48(0xd00000000000ULL);
            machine->memory(013261) = Word48(0xd0000000002dULL);
            machine->memory(013264) = Word48(0xfffffffffffbULL);
            machine->memory(013265) = Word48(9);
            machine->memory(013266) = Word48(0xd00000000020ULL);
            machine->memory(013267) = Word48(0xd0000000002eULL);
            machine->memory(013270) = Word48(0x01ffffffffffULL);
            machine->memory(013271) = Word48(0x080000000000ULL);
            machine->memory(013272) = Word48(013173);
            machine->memory(013301) = Word48(0x80000000000aULL);
            machine->memory(013302) = Word48(0x80a000000000ULL);

            machine->memory(013304) = Word48(1);
            machine->memory(013311) = Word48(1);
            machine->memory(013315) = Word48();
            machine->memory(013317) = Word48(9);
            machine->memory(013322) = Word48();
            machine->memory(013323) = Word48();
            machine->memory(013324) = Word48();
            machine->memory(013325) = Word48();
            machine->memory(013326) = Word48();
            machine->memory(013327) = Word48();
            machine->memory(013334) = Word48();
            machine->memory(013341) = Word48();
            machine->memory(013344) = Word48();
            for (std::uint16_t address = 013305;
                 address != 013361; ++address) {
                if (machine->memory(address) == Word48()) {
                    machine->memory(address) = Word48(address);
                }
            }
            machine->memory(013305) = entry == 013217
                ? sequence_number : Word48();
            machine->memory(013306) = entry == 013130
                ? sequence_number : Word48(013306);
            machine->memory(013315) = Word48();
            machine->memory(013317) = Word48(9);
            machine->memory(013322) = Word48();
            machine->memory(013323) = Word48();
            machine->memory(013324) = Word48();
            machine->memory(013325) = Word48();
            machine->memory(013326) = Word48();
            machine->memory(013327) = Word48();
            machine->memory(013334) = Word48();
            machine->memory(013341) = Word48();
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        for (const std::uint16_t address : {
                 013007, 013017, 013047, 013111, 013130, 013217}) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 256;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    "generated cycle instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "generated cycle reaches its semantic boundary");
        require_same_architectural_state(
            *semantic, *interpreted, "13007..13217 generated cycle");
        return continuation;
    };

    require(compare_generated_cycle(013007, Word48()) == 013047,
            "13007 enters the generated update boundary");
    require(compare_generated_cycle(013017, Word48()) == 013072,
            "13017 enters the generated status boundary");
    require(compare_generated_cycle(013047, Word48()) == 07000,
            "13047 balances its frame on the equal-selector arm");
    require(compare_generated_cycle(013111, Word48()) == 013121,
            "13111 enters the pending-dispatch boundary");
    require(compare_generated_cycle(
                013130, Word48(0xd00000000000ULL)) == 013121,
            "13130 builds the zero-sequence generated state");
    require(compare_generated_cycle(
                013130, Word48(0xd00000000001ULL)) == 013121,
            "13130 builds the nonzero-sequence generated state");
    require(compare_generated_cycle(013217, Word48()) == 07000,
            "13217 initializes an empty generated sequence");
    require(compare_generated_cycle(013217, Word48(1)) == 07000,
            "13217 expands a nonempty generated sequence on r17");

    const std::pair<std::uint16_t, Word48> generated_leader_code[] = {
        {017571, Word48(0x8a1f7902300dULL)},
        {017572, Word48(0x803008f00000ULL)},
        {017573, Word48(0xea1f7ff48ffdULL)},
        {017574, Word48(0x090000dc9f82ULL)},
        {017575, Word48(0xf48ffffafffdULL)},
        {017576, Word48(0xf980010c0000ULL)},
        {025427, Word48(0x02200d023001ULL)},
        {025430, Word48(0xf000001a2b17ULL)},
        {025431, Word48(0x108034091f78ULL)},
        {025432, Word48(0x000000008000ULL)},
        {025433, Word48(0x100041090000ULL)},
        {025434, Word48(0x091f77008000ULL)},
        {025435, Word48(0x109035091f77ULL)},
        {025436, Word48(0x000000090000ULL)},
        {025437, Word48(0x09079f008000ULL)},
        {025440, Word48(0x01e06910a03aULL)},
        {025441, Word48(0x1b80110987a1ULL)},
        {025442, Word48(0x048fff109036ULL)},
        {025443, Word48(0x10a03b1b0012ULL)},
        {025444, Word48(0x090000dc9f3aULL)},
        {025445, Word48(0x090000dc9e38ULL)},
        {025446, Word48(0x1080411b0005ULL)},
        {025447, Word48(0x1c0008090000ULL)},
        {025450, Word48(0xea04120c060cULL)},
        {025451, Word48(0x09079f008000ULL)},
        {025452, Word48(0x10a03d1b8019ULL)},
        {025453, Word48(0xea1f77e08000ULL)},
        {025454, Word48(0x10903510a03cULL)},
        {025455, Word48(0xe00000da2b1fULL)},
        {025456, Word48(0x0c1e3d090000ULL)},
        {025457, Word48(0xda2b1f0c1e38ULL)},
        {025460, Word48(0x09079f008000ULL)},
        {025461, Word48(0x10a03e1b8022ULL)},
        {025462, Word48(0x090000dc8ce3ULL)},
        {025463, Word48(0x100042108042ULL)},
        {025464, Word48(0x01e06910a037ULL)},
        {025465, Word48(0x1b8021108042ULL)},
        {025466, Word48(0x10b0381b0021ULL)},
        {025467, Word48(0x10b0391b0030ULL)},
        {025470, Word48(0xea04110c060cULL)},
        {025471, Word48(0x1080411b802bULL)},
        {025472, Word48(0x09079f008000ULL)},
        {025473, Word48(0x10a03f1b8029ULL)},
        {025474, Word48(0x108034100041ULL)},
        {025475, Word48(0xea1f77e08000ULL)},
        {025476, Word48(0x109035e00000ULL)},
        {025477, Word48(0x1c0018090000ULL)},
        {025500, Word48(0xf08000021001ULL)},
        {025501, Word48(0x02000ddc0000ULL)},
        {025502, Word48(0x09079f008000ULL)},
        {025503, Word48(0x10a0401b802fULL)},
        {025504, Word48(0x008000100041ULL)},
        {025505, Word48(0x1c0026090000ULL)},
        {025506, Word48(0xea04140c060cULL)},
        {025507, Word48(0xea1f77e08000ULL)},
        {025510, Word48(0x109035103042ULL)},
        {025511, Word48(0x01e015f0a000ULL)},
        {025512, Word48(0xe000001c0018ULL)},
        {025556, Word48(0x8a2b6e008000ULL)},
        {025557, Word48(0x091f76000000ULL)},
        {025560, Word48(0x091f78000000ULL)},
        {025561, Word48(0x09079f008000ULL)},
        {025562, Word48(0x80000e01e069ULL)},
        {025563, Word48(0x80a00c8b0007ULL)},
        {025564, Word48(0xea04120c060cULL)},
        {025565, Word48(0x0987a1048fffULL)},
        {025566, Word48(0x80900b80a00dULL)},
        {025567, Word48(0x0b9f3aea0413ULL)},
        {025570, Word48(0x0c060c090000ULL)},
    };
    const auto load_generated_leader_code = [
        &generated_leader_code](Machine &machine) {
        for (const auto &[address, word] : generated_leader_code) {
            machine.memory(address) = word;
        }
        machine.memory(025513) = Word48(0x000000000001ULL);
        machine.memory(025514) = Word48(0x87ffffffffffULL);
        machine.memory(025515) = Word48(0x780000000000ULL);
        machine.memory(025516) = Word48(0x000000000068ULL);
        machine.memory(025517) = Word48(0x2fffffffffffULL);
        machine.memory(025520) = Word48(0xfffffffffff6ULL);
        machine.memory(025521) = Word48(0x000000000069ULL);
        machine.memory(025522) = Word48(0x500000000000ULL);
        machine.memory(025523) = Word48(0x580000000000ULL);
        machine.memory(025524) = Word48(0xd20000000504ULL);
        machine.memory(025525) = Word48(0xd20000000510ULL);
        machine.memory(025526) = Word48(0xd20000000490ULL);
        machine.memory(025527) = Word48(0xd20000000494ULL);
        machine.memory(025571) = Word48(0x780000000000ULL);
        machine.memory(025572) = Word48(0151);
        machine.memory(025573) = Word48(0x500000000000ULL);
    };
    const auto compare_generated_leader = [
        &load_generated_leader_code,
        &require_same_architectural_state](
            std::uint16_t entry, auto initialize,
            const std::string &label, unsigned max_steps) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            load_generated_leader_code(*machine);
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 0141;
            machine->reg(015) = 07000;
            machine->reg(017) = 04000;
            initialize(*machine);
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        for (const std::uint16_t address : {
                 017571, 017575, 025427, 025434, 025437,
                 025445, 025446, 025450, 025451, 025457,
                 025460, 025463, 025470, 025471, 025475,
                 025500, 025502, 025506, 025507, 025556}) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != max_steps;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                label + " reaches its semantic boundary");
        require_same_architectural_state(
            *semantic, *interpreted, label);
        return continuation;
    };

    require(compare_generated_leader(
                017571,
                [](Machine &machine) {
                    machine.memory(017601) = Word48(07654);
                }, "17571 generated wrapper", 16) == 017602,
            "17571 preserves the 17602 call boundary");
    require(compare_generated_leader(
                017575,
                [](Machine &machine) {
                    machine.reg(017) = 04003;
                    machine.memory(04002) = Word48(01234);
                    machine.memory(04001) = Word48(07123);
                }, "17575 generated wrapper return", 8) == 07123,
            "17575 releases its three-word frame");

    const auto initialize_25427 = [](Machine &machine,
                                     Word48 descriptor,
                                     Word48 stacked_descriptor) {
        machine.memory(03637) = descriptor;
        machine.memory(03641) = Word48(05001);
        machine.memory(05000) = stacked_descriptor;
        machine.memory(017567) = Word48(07777777777777777ULL);
    };
    require(compare_generated_leader(
                025427,
                [&initialize_25427](Machine &machine) {
                    initialize_25427(
                        machine, Word48(0xd20000000500ULL),
                        Word48(0x780000000000ULL));
                }, "25427 generated external arm", 64) == 017472,
            "25427 preserves its 17472 boundary");
    require(compare_generated_leader(
                025427,
                [&initialize_25427](Machine &machine) {
                    initialize_25427(
                        machine, Word48(0xd20000000500ULL),
                        Word48(0x500000000000ULL));
                }, "25427 generated return arm", 128) == 07000,
            "25427 restores r1 and its saved caller");
    require(compare_generated_leader(
                025427,
                [&initialize_25427](Machine &machine) {
                    initialize_25427(
                        machine, Word48(0xd20000000510ULL),
                        Word48(0x500000000000ULL));
                }, "25427 generated dispatch arm", 96) == 06343,
            "25427 preserves its rare 06343 boundary");

    require(compare_generated_leader(
                025556,
                [](Machine &machine) {
                    machine.memory(03637) =
                        Word48(0xd20000000500ULL);
                    machine.memory(03641) = Word48(05001);
                    machine.memory(05000) =
                        Word48(0x780000000000ULL);
                }, "25556 generated classifier", 32) == 017472,
            "25556 selects the traced 17472 arm");
    require(compare_generated_leader(
                025556,
                [](Machine &machine) {
                    machine.memory(03637) = Word48();
                    machine.memory(03641) = Word48(05001);
                    machine.memory(05000) = Word48();
                }, "25556 generated diagnostic", 16) == 03014,
            "25556 preserves diagnostic 02022");

    const std::pair<std::uint16_t, Word48> generated_scan_code[] = {
        {016151, Word48(0x8a1c058000a5ULL)},
        {016152, Word48(0x80809a02000eULL)},
        {016153, Word48(0xee1c79eaffffULL)},
        {016154, Word48(0x8080a58b006aULL)},
        {016155, Word48(0x89009be0a000ULL)},
        {016156, Word48(0x8b8066090000ULL)},
        {016157, Word48(0x89009be08000ULL)},
        {016160, Word48(0xf00000090000ULL)},
        {016161, Word48(0xea800102200eULL)},
        {016162, Word48(0x80a09a8b0071ULL)},
        {016163, Word48(0x89009be08000ULL)},
        {016164, Word48(0x89009be40fffULL)},
        {016165, Word48(0x8c006c090000ULL)},
        {016166, Word48(0x80809a80b096ULL)},
        {016167, Word48(0x80909680109aULL)},
        {016170, Word48(0xdc0000090000ULL)},
        {016171, Word48(0xea2253090000ULL)},
        {016172, Word48(0xe0800101e058ULL)},
        {016173, Word48(0x02000ee08000ULL)},
        {016174, Word48(0x80908e8b808dULL)},
        {016175, Word48(0xe08000809096ULL)},
        {016176, Word48(0x8b007580a0a5ULL)},
        {016177, Word48(0x8b007c8080a5ULL)},
        {016200, Word48(0x8b8075090000ULL)},
        {016201, Word48(0x02200d02300eULL)},
        {016202, Word48(0xe03000809090ULL)},
        {016203, Word48(0x8b0088e08000ULL)},
        {016204, Word48(0x8090928b0083ULL)},
        {016205, Word48(0xe0a000e00000ULL)},
        {016206, Word48(0x090000dca17dULL)},
        {016207, Word48(0x8a1c05090000ULL)},
        {016210, Word48(0xf0800002100eULL)},
        {016211, Word48(0x02000de08000ULL)},
        {016212, Word48(0x809096f00000ULL)},
        {016213, Word48(0xe0a000e00000ULL)},
        {016214, Word48(0xf08000dc0000ULL)},
        {016215, Word48(0x090000dca23dULL)},
        {016216, Word48(0x8a1c05f08000ULL)},
        {016217, Word48(0x02100e02000dULL)},
        {016220, Word48(0xe08000809096ULL)},
        {016221, Word48(0xdc0000090000ULL)},
        {016222, Word48(0x008000dc0000ULL)},
    };
    const std::initializer_list<std::uint16_t> generated_scan_entries = {
        016151, 016153, 016157, 016161, 016166, 016171,
        016201, 016207, 016210, 016215, 016216, 016222,
    };
    const auto compare_generated_scan = [
        &generated_scan_code, &generated_scan_entries,
        &require_same_architectural_state](std::uint16_t entry,
                                           auto initialize,
                                           const std::string &label,
                                           unsigned max_steps) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : generated_scan_code) {
                machine->memory(address) = word;
            }
            machine->memory(016223) = Word48(0x400000000000ULL);
            machine->memory(016225) = Word48(0x1ffe00000000ULL);
            machine->memory(016227) = Word48(0x200000000000ULL);
            machine->memory(016233) = Word48(0x000000007fffULL);
            machine->accumulator() = Word48();
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(010) = 016005;
            machine->reg(015) = 07000;
            machine->reg(016) = 03000;
            machine->reg(017) = 04000;
            initialize(*machine);
            machine->start(entry);
        }
        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        for (const std::uint16_t address : generated_scan_entries) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != max_steps;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                label + " reaches its semantic boundary");
        require_same_architectural_state(*semantic, *interpreted, label);
        return continuation;
    };

    require(compare_generated_scan(
                016151,
                [](Machine &machine) {
                    machine.memory(016237) = Word48(3);
                    machine.memory(016242) = Word48(076000);
                }, "16151 table fast path", 48) == 07000,
            "16151 returns the selected table word");
    require(compare_generated_scan(
                016151,
                [](Machine &machine) {
                    machine.memory(021124) = Word48(
                        static_cast<std::uint64_t>(03000) << 24);
                    machine.memory(03000) = Word48(0x400000000000ULL);
                }, "16151 descriptor scan", 48) == 07000,
            "16151 preserves the traced zero-result scan arm");
    require(compare_generated_scan(
                016201,
                [](Machine &machine) {
                    machine.memory(03000) = Word48();
                }, "16201 external zero arm", 16) == 021075,
            "16201 preserves the 21075 call boundary");
    require(compare_generated_scan(
                016201,
                [](Machine &machine) {
                    machine.memory(03000) = Word48(0x300000000000ULL);
                }, "16201 external rewrite arm", 24) == 020575,
            "16201 preserves the 20575 call boundary");
    require(compare_generated_scan(
                016207,
                [](Machine &machine) {
                    machine.reg(017) = 04002;
                    machine.memory(04000) = Word48(07000);
                    machine.memory(04001) = Word48(03000);
                    machine.memory(03000) = Word48(012345);
                }, "16207 rewrite continuation", 24) == 07000,
            "16207 restores the descriptor frame after 20575");
    require(compare_generated_scan(
                016216,
                [](Machine &machine) {
                    machine.reg(017) = 04002;
                    machine.memory(04000) = Word48(07000);
                    machine.memory(04001) = Word48(03000);
                    machine.memory(03000) = Word48(012345);
                }, "16216 mask continuation", 16) == 07000,
            "16216 restores the descriptor frame after 21075");

    const std::pair<std::uint16_t, Word48> generated_format_code[] = {
        {025641, Word48(0x8a2ba1fa8001ULL)},
        {025642, Word48(0x02300df00000ULL)},
        {025643, Word48(0x01f007f48ffeULL)},
        {025644, Word48(0x80f01880f017ULL)},
        {025645, Word48(0x80001a019040ULL)},
        {025646, Word48(0x80f015dcabb0ULL)},
        {025647, Word48(0x80a014f40ffeULL)},
        {025650, Word48(0x80801a80f017ULL)},
        {025651, Word48(0x80001a019040ULL)},
        {025652, Word48(0x80f015dcabb0ULL)},
        {025653, Word48(0x01e058f40ffdULL)},
        {025654, Word48(0x80801adcabb0ULL)},
        {025655, Word48(0xf4affdf40ffdULL)},
        {025656, Word48(0xf0800002100dULL)},
        {025657, Word48(0xdc0000090000ULL)},
        {025660, Word48(0x80f01980001bULL)},
        {025661, Word48(0x01904080f016ULL)},
        {025662, Word48(0x01e02080301bULL)},
        {025663, Word48(0x01e018f0a000ULL)},
        {025664, Word48(0x80a014dc0000ULL)},
    };
    auto semantic_format = std::make_unique<Machine>();
    auto interpreted_format = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_format.get(), interpreted_format.get()}) {
        for (const auto &[address, word] : generated_format_code) {
            machine->memory(address) = word;
        }
        machine->memory(025665) = Word48(0x00000e000000ULL);
        machine->memory(025666) = Word48(0x80000000003cULL);
        machine->memory(025667) = Word48(0x80000000000aULL);
        machine->memory(025670) = Word48(0x800444444445ULL);
        machine->memory(025671) = Word48(0x80051eb851ecULL);
        machine->memory(025672) = Word48(0x80199999999aULL);
        machine->accumulator() = Word48(0000000000640114ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(010) = 01234;
        machine->reg(015) = 07000;
        machine->reg(017) = 04000;
        machine->start(025641);
    }
    for (const std::uint16_t address : {
             025641, 025647, 025653, 025655, 025660}) {
        interpreted_format->disable_translated_routine(address);
    }
    for (unsigned steps = 0;
         semantic_format->program_counter() != 07000 && steps != 16;
         ++steps) {
        require(semantic_format->step()
                    == poplan::ExecutionStatus::running,
                "25641 semantic formatting path keeps running");
    }
    for (unsigned steps = 0;
         (interpreted_format->program_counter() != 07000
          || interpreted_format->right_half()) && steps != 128;
         ++steps) {
        require(interpreted_format->step()
                    == poplan::ExecutionStatus::running,
                "25641 instruction formatting path keeps running");
    }
    require(semantic_format->program_counter() == 07000
                && interpreted_format->program_counter() == 07000
                && !semantic_format->right_half()
                && !interpreted_format->right_half(),
            "25641 formatting paths reach their saved caller");
    require_same_architectural_state(
        *semantic_format, *interpreted_format,
        "25641 and 25660 generated formatting");

    const std::pair<std::uint16_t, Word48> generated_store_code[] = {
        {04507, Word48(0x02300df43ffeULL)},
        {04510, Word48(0xba0916b09010ULL)},
        {04511, Word48(0xb0a047003000ULL)},
        {04512, Word48(0x090000dc8a8dULL)},
        {04513, Word48(0xba0916b9801eULL)},
        {04514, Word48(0x000001b0101eULL)},
        {04515, Word48(0x02100ddc0000ULL)},
    };
    const auto compare_generated_store = [
        &generated_store_code,
        &require_same_architectural_state](std::uint16_t entry) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : generated_store_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = entry == 04507
                ? Word48(07200000000065415ULL)
                : Word48(07200000000065343ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(013) = 01234;
            machine->reg(015) = entry == 04507 ? 04303 : 05433;
            machine->reg(017) = entry == 04507 ? 04000 : 04003;
            machine->memory(04446) = Word48(077777);
            machine->memory(04535) =
                Word48(0160000000000000ULL);
            machine->memory(04464) = Word48(05000);
            machine->memory(04000) =
                Word48(07200000000065415ULL);
            machine->memory(04001) =
                Word48(07200000000065415ULL);
            machine->memory(04002) = Word48(04303);
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        interpreted->disable_translated_routine(04507);
        interpreted->disable_translated_routine(04513);
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 24;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    "04507 instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "04507 instruction path reaches its semantic boundary");
        require_same_architectural_state(
            *semantic, *interpreted, "04507 generated store");
        return continuation;
    };

    require(compare_generated_store(04507) == 05215,
            "04507 builds the two-word allocation request");
    require(compare_generated_store(04513) == 04303,
            "04513 stores the allocation and selects its stacked return");

    const std::pair<std::uint16_t, Word48> compiler_wrapper_code[] = {
        {04161, Word48(0x02300202300dULL)},
        {04162, Word48(0x02300d2a075eULL)},
        {04163, Word48(0x090000dc88d2ULL)},
        {04164, Word48(0xee8880208043ULL)},
        {04165, Word48(0x200044dc8937ULL)},
        {04166, Word48(0x090000dc9ea2ULL)},
        {04167, Word48(0xee887d298044ULL)},
        {04170, Word48(0x048fff01e06fULL)},
        {04171, Word48(0x2b812c208044ULL)},
        {04172, Word48(0x2031addc8927ULL)},
        {04173, Word48(0x02100d02100dULL)},
        {04174, Word48(0x021002dc0000ULL)},
        {04175, Word48(0x20804320a19cULL)},
        {04176, Word48(0x2b8123ea0778ULL)},
        {04177, Word48(0xda08822c0000ULL)},
        {04200, Word48(0xea07642c0121ULL)},
        {04201, Word48(0xea0774dc875eULL)},
        {04202, Word48(0xee088b098934ULL)},
        {04203, Word48(0xea0000e08000ULL)},
        {04204, Word48(0x2091a82b012aULL)},
        {04205, Word48(0x20a19e2b812dULL)},
        {04206, Word48(0xe0800020a19bULL)},
        {04207, Word48(0xe000002c011dULL)},
        {04210, Word48(0xe0800020a1aeULL)},
        {04211, Word48(0xe000002c011dULL)},
        {04212, Word48(0xea0820dc860cULL)},
        {04213, Word48(0xea08300c060cULL)},
    };
    const auto compare_compiler_wrapper = [
        &compiler_wrapper_code,
        &require_same_architectural_state](std::uint16_t entry,
                                           std::uint16_t status) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : compiler_wrapper_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(002) = entry == 04161 ? 04536 : 03536;
            machine->reg(015) = 04610;
            machine->reg(016) = status;
            machine->reg(017) = entry == 04161 ? 04000 : 04003;
            machine->memory(04000) = Word48(0765432107654321ULL);
            machine->memory(04001) = Word48(04536);
            machine->memory(04002) = Word48(04610);
            machine->memory(03641) = Word48(05001);
            machine->memory(03642) = entry == 04167
                ? Word48(05001) : Word48();
            machine->memory(04464) = Word48(06000);
            machine->memory(06000) = Word48(01234);
            machine->memory(05000) = Word48();
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        for (const auto &[address, word] : compiler_wrapper_code) {
            (void) word;
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 48;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    "04161 instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "04161 instruction path reaches its semantic boundary");
        require_same_architectural_state(
            *semantic, *interpreted, "04161 compiler wrapper");
        return continuation;
    };

    require(compare_compiler_wrapper(04161, 0) == 04322,
            "04161 saves its frame and enters classification");
    require(compare_compiler_wrapper(04164, 0) == 04467,
            "04164 stores the selected value before compiler dispatch");
    require(compare_compiler_wrapper(04164, 1) == 03536,
            "04164 preserves the alternate 03536 continuation");
    require(compare_compiler_wrapper(04167, 0) == 04447,
            "04167 preserves the traced allocation boundary");
    require(compare_compiler_wrapper(04173, 0) == 04610,
            "04173 restores and balances the three-word frame");
    require(compare_compiler_wrapper(04175, 0) == 03536,
            "04175 preserves its computed 03536 continuation");
    require(compare_compiler_wrapper(04202, 1) == 04610,
            "04202 updates the selected word and restores the frame");
    require(compare_compiler_wrapper(04202, 0) == 03014,
            "04202 preserves its zero-status diagnostic boundary");
    require(compare_compiler_wrapper(04212, 0) == 03014,
            "04212 preserves diagnostic code 04040 and link 04213");

    const std::pair<std::uint16_t, Word48> record_marker_code[] = {
        {004426, Word48(05640442614100002ULL)},
        {004427, Word48(05412007656700003ULL)},
        {004430, Word48(05410007757000005ULL)},
        {004431, Word48(01410000254120100ULL)},
        {004432, Word48(06670000054100101ULL)},
        {004433, Word48(01400000200100000ULL)},
        {004434, Word48(06700000002200000ULL)},
    };
    const auto compare_record_marker = [
        &record_marker_code,
        &require_same_architectural_state](Word48 field,
                                           std::uint16_t modifier = 0) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : record_marker_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(0) = modifier;
            machine->reg(003) = 03000;
            machine->reg(013) = 07123;
            machine->reg(015) = 05670;
            machine->reg(017) = 04000;
            machine->memory(0) = Word48(0666);
            machine->memory(03002) = field;
            machine->memory(04524) = Word48(01423);
            machine->memory(04525) = Word48(04517);
            machine->memory(04526) = Word48(01427);
            machine->memory(04527) = Word48(04436);
            machine->start(04426);
        }

        require(semantic->step() == poplan::ExecutionStatus::running,
                "04426 semantic path keeps running");
        const std::uint16_t continuation = semantic->program_counter();
        interpreted->disable_translated_routine(04426);
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 32;
             ++steps) {
            require(interpreted->step() == poplan::ExecutionStatus::running,
                    "04426 instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "04426 instruction path reaches its semantic boundary");
        require_same_architectural_state(
            *semantic, *interpreted, "04426 record marker");
        return semantic;
    };

    const auto first_record_marker =
        compare_record_marker(Word48(01423));
    require(first_record_marker->memory(03002) == Word48(04517)
                && first_record_marker->accumulator() == Word48(0666),
            "04426 replaces the first record marker");
    const auto second_record_marker =
        compare_record_marker(Word48(01427));
    require(second_record_marker->memory(03002) == Word48(04436)
                && second_record_marker->accumulator() == Word48(0666),
            "04426 replaces the second record marker");
    const auto ordinary_record_marker =
        compare_record_marker(Word48(01234), 7);
    require(ordinary_record_marker->memory(03002) == Word48(01234)
                && ordinary_record_marker->accumulator()
                    == Word48(01234 ^ 01427)
                && ordinary_record_marker->remainder()
                    == Word48(01234 ^ 01427),
            "04426 preserves its ordinary mismatch result");

    const std::pair<std::uint16_t, Word48> record_byte_code[] = {
        {016742, Word48(01410000164440007ULL)},
        {016743, Word48(06711642102200000ULL)},
        {016744, Word48(00512451137000000ULL)},
    };
    const auto compare_record_byte = [
        &install_tagged_byte_lookup,
        &record_byte_code,
        &require_same_architectural_state](Word48 record_byte,
                                           std::uint16_t modifier = 0) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            install_tagged_byte_lookup(*machine);
            for (const auto &[address, word] : record_byte_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(0) = modifier;
            machine->reg(003) = 03000;
            machine->reg(007) = 07123;
            machine->reg(015) = 04567;
            machine->reg(017) = 04000;
            machine->memory(03001) = record_byte;
            machine->memory(016772) = Word48(2);
            machine->start(016742);
        }

        for (unsigned steps = 0;
             (semantic->program_counter() != 04567
              || semantic->right_half()) && steps != 16;
             ++steps) {
            require(semantic->step() == poplan::ExecutionStatus::running,
                    "16742 semantic path keeps running");
        }
        interpreted->disable_translated_routine(016742);
        interpreted->disable_translated_routine(016744);
        for (unsigned steps = 0;
             (interpreted->program_counter() != 04567
              || interpreted->right_half()) && steps != 32;
             ++steps) {
            require(interpreted->step() == poplan::ExecutionStatus::running,
                    "16742 instruction path keeps running");
        }
        require(semantic->program_counter() == 04567
                    && !semantic->right_half()
                    && interpreted->program_counter() == 04567
                    && !interpreted->right_half(),
                "16742 paths return through the saved r7 link");
        require_same_architectural_state(
            *semantic, *interpreted, "16742 record byte");
        return semantic;
    };

    const auto tagged_record_byte = compare_record_byte(
        Word48(06400000000000012ULL), 7);
    require(tagged_record_byte->accumulator() == Word48(016)
                && tagged_record_byte->remainder() == Word48(014)
                && tagged_record_byte->reg(007) == 04567
                && tagged_record_byte->reg(015) == 016744,
            "16742 converts and adjusts a tagged record byte");
    const auto untagged_record_byte =
        compare_record_byte(Word48(012));
    require(untagged_record_byte->accumulator() == Word48(017)
                && untagged_record_byte->remainder() == Word48(015),
            "16742 retains 16421's untagged-byte fallback");

    const std::pair<std::uint16_t, Word48> address_wrapper_code[] = {
        {011464, Word48(00043000100430015ULL)},
        {011465, Word48(07400000006411464ULL)},
        {011466, Word48(07637777572400000ULL)},
        {011467, Word48(07341147472500001ULL)},
        {011470, Word48(00220000067105430ULL)},
        {011471, Word48(07510777500360050ULL)},
        {011472, Word48(07512777504130310ULL)},
        {011473, Word48(07000000002200000ULL)},
        {011474, Word48(00042001604120036ULL)},
        {011475, Word48(07500777574100000ULL)},
        {011476, Word48(00041001500410001ULL)},
        {011477, Word48(06700000002200000ULL)},
    };
    const auto prepare_address_wrapper = [&address_wrapper_code](
        Machine &machine) {
        for (const auto &[address, word] : address_wrapper_code) {
            machine.memory(address) = word;
        }
        machine.remainder() = Word48(07654);
        machine.alu_mode() = 025;
        machine.reg(001) = 05007;
        machine.reg(002) = 065517;
        machine.reg(003) = 065523;
        machine.reg(015) = 05163;
        machine.reg(016) = 07123;
        machine.reg(017) = 04000;
        machine.memory(011522) =
            Word48(07040000000000000ULL);
        machine.memory(011774) = Word48(1);
    };

    auto zero_address_semantic = std::make_unique<Machine>();
    auto zero_address_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {zero_address_semantic.get(),
                             zero_address_interpreted.get()}) {
        prepare_address_wrapper(*machine);
        machine->accumulator() = Word48();
        machine->start(011464);
    }
    require(zero_address_semantic->step()
                == poplan::ExecutionStatus::running,
            "11464 zero-address semantic path keeps running");
    zero_address_interpreted->disable_translated_routine(011464);
    zero_address_interpreted->disable_translated_routine(011471);
    for (unsigned steps = 0;
         (zero_address_interpreted->program_counter() != 05163
          || zero_address_interpreted->right_half()) && steps != 32;
         ++steps) {
        require(zero_address_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "11464 zero-address instruction path keeps running");
    }
    require_same_architectural_state(
        *zero_address_semantic, *zero_address_interpreted,
        "11464 zero address");
    require(zero_address_semantic->accumulator()
                == Word48(07040000000000000ULL)
                && zero_address_semantic->remainder() == Word48()
                && zero_address_semantic->alu_mode() == 005
                && zero_address_semantic->reg(001) == 05007
                && zero_address_semantic->reg(015) == 05163
                && zero_address_semantic->reg(016) == 0
                && zero_address_semantic->reg(017) == 04000,
            "11464 zero address skips allocation and restores its frame");

    auto address_transfer_semantic = std::make_unique<Machine>();
    auto address_transfer_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {address_transfer_semantic.get(),
                             address_transfer_interpreted.get()}) {
        prepare_address_wrapper(*machine);
        machine->accumulator() = Word48(1);
        machine->start(011464);
    }
    require(address_transfer_semantic->step()
                == poplan::ExecutionStatus::running,
            "11464 allocation transfer keeps running");
    address_transfer_interpreted->disable_translated_routine(011464);
    address_transfer_interpreted->disable_translated_routine(011471);
    for (unsigned steps = 0;
         (address_transfer_interpreted->program_counter() != 05430
          || address_transfer_interpreted->right_half()) && steps != 24;
         ++steps) {
        require(address_transfer_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "11464 allocation instruction path keeps running");
    }
    require_same_architectural_state(
        *address_transfer_semantic, *address_transfer_interpreted,
        "11464 allocation transfer");
    require(address_transfer_semantic->accumulator() == Word48(05163)
                && address_transfer_semantic->remainder() == Word48(07654)
                && address_transfer_semantic->alu_mode() == 005
                && address_transfer_semantic->reg(001) == 011464
                && address_transfer_semantic->reg(015) == 011471
                && address_transfer_semantic->reg(016) == 2
                && address_transfer_semantic->reg(017) == 04003,
            "11464 builds the observed allocator frame and continuation");

    auto address_resume_semantic = std::make_unique<Machine>();
    auto address_resume_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {address_resume_semantic.get(),
                             address_resume_interpreted.get()}) {
        prepare_address_wrapper(*machine);
        machine->accumulator() =
            Word48(06500000000000000ULL);
        machine->reg(001) = 011464;
        machine->reg(015) = 05433;
        machine->reg(016) = 03000;
        machine->reg(017) = 04003;
        machine->memory(04000) = Word48(1);
        machine->memory(04001) = Word48(05007);
        machine->memory(04002) = Word48(05163);
        machine->start(011471);
    }
    require(address_resume_semantic->step()
                == poplan::ExecutionStatus::running,
            "11471 semantic continuation keeps running");
    address_resume_interpreted->disable_translated_routine(011471);
    for (unsigned steps = 0;
         (address_resume_interpreted->program_counter() != 05163
          || address_resume_interpreted->right_half()) && steps != 24;
         ++steps) {
        require(address_resume_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "11471 instruction continuation keeps running");
    }
    require_same_architectural_state(
        *address_resume_semantic, *address_resume_interpreted,
        "11471 continuation");
    require(address_resume_semantic->accumulator()
                == Word48(07040000000003000ULL)
                && address_resume_semantic->remainder() == Word48(03000)
                && address_resume_semantic->alu_mode() == 005
                && address_resume_semantic->reg(001) == 05007
                && address_resume_semantic->reg(015) == 05163
                && address_resume_semantic->reg(016) == 03000
                && address_resume_semantic->reg(017) == 04000
                && address_resume_semantic->memory(03000)
                    == Word48(00000000100000002ULL),
            "11471 stores the transformed word and restores its frame");

    const std::pair<std::uint16_t, Word48> compiler_frame_code[] = {
        {07673, Word48(00042001500370006ULL)},
        {07674, Word48(00043000100430002ULL)},
        {07675, Word48(00043000526407514ULL)},
        {07676, Word48(02403024374000000ULL)},
        {07677, Word48(07010000024000243ULL)},
        {07700, Word48(00036010124130325ULL)},
        {07701, Word48(02660016724100243ULL)},
        {07702, Word48(06640774203012674ULL)},
        {07703, Word48(02410024367103330ULL)},
        {07704, Word48(02660023224100326ULL)},
        {07705, Word48(00220000067103275ULL)},
        {07706, Word48(07240000167107652ULL)},
        {07707, Word48(02410024367110353ULL)},
        {07710, Word48(00036007126700200ULL)},
        {07711, Word48(02410024367103330ULL)},
        {07712, Word48(02670021272407757ULL)},
        {07713, Word48(06640774227000157ULL)},
        {07714, Word48(02410024367117021ULL)},
        {07715, Word48(02403024367117013ULL)},
        {07716, Word48(02400024372407757ULL)},
        {07717, Word48(00220000067107673ULL)},
        {07720, Word48(07410000024000243ULL)},
        {07721, Word48(00220000067110353ULL)},
        {07722, Word48(00036007126600175ULL)},
        {07723, Word48(02410032267103275ULL)},
        {07724, Word48(07240000167107652ULL)},
        {07725, Word48(02700020002200000ULL)},
        {07726, Word48(02410032767103275ULL)},
        {07727, Word48(07240000127000225ULL)},
        {07730, Word48(02410024300400016ULL)},
        {07731, Word48(02411031724120330ULL)},
        {07732, Word48(02670024570100000ULL)},
        {07733, Word48(00640000012477773ULL)},
        {07734, Word48(02400024424110331ULL)},
        {07735, Word48(02660022306500001ULL)},
        {07736, Word48(02412032067103275ULL)},
        {07737, Word48(02410024400360110ULL)},
        {07740, Word48(01370773404440016ULL)},
        {07741, Word48(00220000067107652ULL)},
        {07742, Word48(07410000024010243ULL)},
        {07743, Word48(00041000500410002ULL)},
        {07744, Word48(00041000100400015ULL)},
        {07745, Word48(06700000002200000ULL)},
        {07746, Word48(02410024324110317ULL)},
        {07747, Word48(02412033226700214ULL)},
        {07750, Word48(07240775367107673ULL)},
        {07751, Word48(07240775467107673ULL)},
        {07752, Word48(02700022602200000ULL)},
    };
    const auto prepare_compiler_frame = [&compiler_frame_code](
        Machine &machine) {
        for (const auto &[address, word] : compiler_frame_code) {
            machine.memory(address) = word;
        }
        machine.accumulator() = Word48(0123456701234567ULL);
        machine.remainder() = Word48(07654);
        machine.alu_mode() = 025;
        machine.reg(001) = 0141;
        machine.reg(002) = 07777;
        machine.reg(005) = 04536;
        machine.reg(015) = 05670;
        machine.reg(016) = 03000;
        machine.reg(017) = 04000;
        machine.memory(07757) = Word48(0765432107654321ULL);
    };

    const auto compare_compiler_frame_entry = [
        &prepare_compiler_frame,
        &require_same_architectural_state](Word48 input,
                                           Word48 branch_constant,
                                           std::uint16_t boundary,
                                           const std::string &label) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            prepare_compiler_frame(*machine);
            machine->memory(03000) = input;
            machine->memory(010041) = branch_constant;
            machine->start(07673);
        }
        require(semantic->step() == poplan::ExecutionStatus::running,
                label + " semantic path keeps running");
        interpreted->set_translated_routines_enabled(false);
        for (unsigned steps = 0;
             (interpreted->program_counter() != boundary
              || interpreted->right_half()) && steps != 48;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require(interpreted->program_counter() == boundary
                    && !interpreted->right_half(),
                label + " instruction path reaches its boundary");
        require_same_architectural_state(*semantic, *interpreted, label);
        return semantic;
    };

    const auto compiler_frame_transfer = compare_compiler_frame_entry(
        Word48(), Word48(), 012674, "07673 transfer");
    require(compiler_frame_transfer->reg(005) == 07514
                && compiler_frame_transfer->reg(015) == 07742
                && compiler_frame_transfer->reg(017) == 04005
                && compiler_frame_transfer->accumulator() == Word48()
                && compiler_frame_transfer->remainder() == Word48()
                && compiler_frame_transfer->alu_mode() == 006,
            "07673 preserves its frame around the 12674 boundary");

    const auto compiler_frame_check = compare_compiler_frame_entry(
        Word48(04000000000000000ULL),
        Word48(02000000000000000ULL), 03330,
        "07673 checked branch");
    require(compiler_frame_check->reg(015) == 07704
                && compiler_frame_check->reg(017) == 04005
                && compiler_frame_check->accumulator()
                    == Word48(04000000000000000ULL)
                && compiler_frame_check->remainder()
                    == Word48(04000000000000000ULL)
                && compiler_frame_check->alu_mode() == 006,
            "07673 enters 03330 with the original selected word");

    auto compiler_loop_semantic = std::make_unique<Machine>();
    auto compiler_loop_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {compiler_loop_semantic.get(),
                             compiler_loop_interpreted.get()}) {
        prepare_compiler_frame(*machine);
        machine->accumulator() = Word48();
        machine->alu_mode() = 006;
        machine->reg(005) = 07514;
        machine->reg(015) = 07704;
        machine->reg(017) = 04005;
        machine->memory(04000) = Word48(05670);
        machine->memory(04001) = Word48(0141);
        machine->memory(04002) = Word48(07777);
        machine->memory(04003) = Word48(04536);
        machine->memory(04004) = Word48(0765432107654321ULL);
        machine->memory(07757) = Word48(03000);
        machine->memory(010033) = Word48(Word48::mask);
        machine->memory(010046) = Word48(1);
        machine->memory(010044) = Word48(03000);
        machine->memory(010045) = Word48();
        machine->memory(03000) = Word48(0777);
        machine->start(07704);
    }
    require(compiler_loop_semantic->step()
                == poplan::ExecutionStatus::running,
            "07704 semantic loop keeps running");
    compiler_loop_interpreted->set_translated_routines_enabled(false);
    for (unsigned steps = 0;
         (compiler_loop_interpreted->program_counter() != 07652
          || compiler_loop_interpreted->right_half()) && steps != 160;
         ++steps) {
        require(compiler_loop_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "07704 instruction loop keeps running");
    }
    require_same_architectural_state(
        *compiler_loop_semantic, *compiler_loop_interpreted,
        "07704 bit loop");
    require(compiler_loop_semantic->reg(001) == 0
                && compiler_loop_semantic->reg(002) == 0
                && compiler_loop_semantic->reg(015) == 07742
                && compiler_loop_semantic->reg(016) == 0
                && compiler_loop_semantic->reg(017) == 04005,
            "07704 completes six bit-loop passes at the 07652 boundary");

    auto compiler_cleanup_semantic = std::make_unique<Machine>();
    auto compiler_cleanup_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {compiler_cleanup_semantic.get(),
                             compiler_cleanup_interpreted.get()}) {
        prepare_compiler_frame(*machine);
        machine->accumulator() = Word48(01234);
        machine->alu_mode() = 025;
        machine->reg(001) = 0;
        machine->reg(002) = 0;
        machine->reg(005) = 07514;
        machine->reg(015) = 07742;
        machine->reg(017) = 04005;
        machine->memory(04000) = Word48(05670);
        machine->memory(04001) = Word48(0141);
        machine->memory(04002) = Word48(07777);
        machine->memory(04003) = Word48(04536);
        machine->memory(04004) = Word48(0765432107654321ULL);
        machine->start(07742);
    }
    require(compiler_cleanup_semantic->step()
                == poplan::ExecutionStatus::running,
            "07742 semantic cleanup keeps running");
    compiler_cleanup_interpreted->set_translated_routines_enabled(false);
    for (unsigned steps = 0;
         (compiler_cleanup_interpreted->program_counter() != 05670
          || compiler_cleanup_interpreted->right_half()) && steps != 24;
         ++steps) {
        require(compiler_cleanup_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "07742 instruction cleanup keeps running");
    }
    require_same_architectural_state(
        *compiler_cleanup_semantic, *compiler_cleanup_interpreted,
        "07742 cleanup");
    require(compiler_cleanup_semantic->accumulator() == Word48(05670)
                && compiler_cleanup_semantic->remainder() == Word48(07654)
                && compiler_cleanup_semantic->alu_mode() == 005
                && compiler_cleanup_semantic->reg(001) == 0141
                && compiler_cleanup_semantic->reg(002) == 07777
                && compiler_cleanup_semantic->reg(005) == 04536
                && compiler_cleanup_semantic->reg(015) == 05670
                && compiler_cleanup_semantic->reg(017) == 04000
                && compiler_cleanup_semantic->memory(07757)
                    == Word48(0765432107654321ULL),
            "07742 restores the five-word compiler frame");

    const std::pair<std::uint16_t, Word48> addressed_update_code[] = {
        {03716, Word48(00040001673403723ULL)},
        {03717, Word48(07010000000400014ULL)},
        {03720, Word48(06340372260100000ULL)},
        {03721, Word48(07513777760000000ULL)},
        {03722, Word48(07010000112700160ULL)},
        {03723, Word48(07657777767000000ULL)},
    };
    const auto compare_addressed_update = [
        &addressed_update_code,
        &require_same_architectural_state](Word48 input,
                                           Word48 first_word,
                                           Word48 second_word,
                                           std::uint16_t r2,
                                           std::uint16_t boundary,
                                           const std::string &label) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : addressed_update_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = input;
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(002) = r2;
            machine->reg(014) = 07123;
            machine->reg(015) = 05670;
            machine->reg(016) = 07654;
            machine->reg(017) = 04001;
            machine->memory(03000) = first_word;
            machine->memory(03001) = second_word;
            machine->memory(03100) = Word48(5);
            machine->memory(04000) = Word48(2);
            machine->start(03716);
        }
        require(semantic->step() == poplan::ExecutionStatus::running,
                label + " semantic path keeps running");
        interpreted->set_translated_routines_enabled(false);
        for (unsigned steps = 0;
             (interpreted->program_counter() != boundary
              || interpreted->right_half()) && steps != 24;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require(interpreted->program_counter() == boundary
                    && !interpreted->right_half(),
                label + " instruction path reaches its boundary");
        require_same_architectural_state(*semantic, *interpreted, label);
        return semantic;
    };

    const auto zero_addressed_update = compare_addressed_update(
        Word48(), Word48(), Word48(), 03536, 05670,
        "03716 zero address");
    require(zero_addressed_update->reg(016) == 0
                && zero_addressed_update->reg(017) == 04000
                && zero_addressed_update->remainder() == Word48(07654)
                && zero_addressed_update->alu_mode() == 025,
            "03716 zero address returns without touching ALU state");

    const auto stored_addressed_update = compare_addressed_update(
        Word48(03000), Word48(03100), Word48(), 03536, 05670,
        "03716 stored update");
    require(stored_addressed_update->memory(03100) == Word48(7)
                && stored_addressed_update->reg(014) == 03100
                && stored_addressed_update->reg(016) == 03000
                && stored_addressed_update->reg(017) == 04000
                && stored_addressed_update->remainder() == Word48()
                && stored_addressed_update->alu_mode() == 005,
            "03716 cyclically updates the selected word and returns");

    const auto transferred_addressed_update = compare_addressed_update(
        Word48(03000), Word48(), Word48(1), 05620, 06000,
        "03716 computed transfer");
    require(transferred_addressed_update->reg(014) == 0
                && transferred_addressed_update->reg(017) == 04001
                && transferred_addressed_update->accumulator() == Word48(1)
                && transferred_addressed_update->remainder() == Word48(1)
                && transferred_addressed_update->alu_mode() == 005,
            "03716 preserves the stack on its computed transfer");

    const std::pair<std::uint16_t, Word48> table_write_wrapper_code[] = {
        {04074, Word48(00043001502204464ULL)},
        {04075, Word48(00003000067117340ULL)},
        {04076, Word48(00220446300100000ULL)},
        {04077, Word48(00220000067117340ULL)},
        {04100, Word48(03410117367117340ULL)},
        {04101, Word48(03410116767117340ULL)},
        {04102, Word48(01010010767117340ULL)},
        {04103, Word48(00010000002204463ULL)},
        {04104, Word48(00000000034001173ULL)},
        {04105, Word48(03400116710100627ULL)},
        {04106, Word48(01000010710100647ULL)},
        {04107, Word48(00220446400010000ULL)},
        {04110, Word48(00041001567000000ULL)},
    };
    const auto prepare_table_write_wrapper = [&table_write_wrapper_code](
        Machine &machine) {
        for (const auto &[address, word] : table_write_wrapper_code) {
            machine.memory(address) = word;
        }
        machine.accumulator() = Word48(0123);
        machine.remainder() = Word48(07654);
        machine.alu_mode() = 025;
        machine.reg(002) = 03536;
        machine.reg(007) = 01000;
        machine.reg(015) = 05670;
        machine.reg(017) = 04000;
        machine.memory(04464) = Word48(03000);
    };

    auto table_write_entry_semantic = std::make_unique<Machine>();
    auto table_write_entry_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {table_write_entry_semantic.get(),
                             table_write_entry_interpreted.get()}) {
        prepare_table_write_wrapper(*machine);
        machine->start(04074);
    }
    require(table_write_entry_semantic->step()
                == poplan::ExecutionStatus::running,
            "04074 semantic entry keeps running");
    table_write_entry_interpreted->set_translated_routines_enabled(false);
    for (unsigned steps = 0;
         (table_write_entry_interpreted->program_counter() != 017340
          || table_write_entry_interpreted->right_half()) && steps != 16;
         ++steps) {
        require(table_write_entry_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "04074 instruction entry keeps running");
    }
    require_same_architectural_state(
        *table_write_entry_semantic, *table_write_entry_interpreted,
        "04074 entry");
    require(table_write_entry_semantic->accumulator() == Word48(03000)
                && table_write_entry_semantic->reg(015) == 04076
                && table_write_entry_semantic->reg(017) == 04002
                && table_write_entry_semantic->memory(04000)
                    == Word48(0123)
                && table_write_entry_semantic->memory(04001)
                    == Word48(05670),
            "04074 builds its two-word table-write frame");

    struct TableWriteContinuationCase {
        std::uint16_t entry;
        std::uint16_t link;
        Word48 value;
    };
    const TableWriteContinuationCase table_write_continuations[] = {
        {04076, 04100, Word48(0701)},
        {04100, 04101, Word48(0702)},
        {04101, 04102, Word48(0703)},
        {04102, 04103, Word48(0704)},
    };
    for (const auto &test : table_write_continuations) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            prepare_table_write_wrapper(*machine);
            machine->reg(015) = test.entry;
            machine->reg(017) = 04002;
            machine->memory(04000) = Word48(0123);
            machine->memory(04001) = Word48(05670);
            machine->memory(04463) = Word48(0701);
            machine->memory(02173) = Word48(0702);
            machine->memory(02167) = Word48(0703);
            machine->memory(03645) = Word48(0704);
            machine->start(test.entry);
        }
        const std::string label =
            std::to_string(test.entry) + " table-write continuation";
        require(semantic->step() == poplan::ExecutionStatus::running,
                label + " semantic path keeps running");
        interpreted->set_translated_routines_enabled(false);
        for (unsigned steps = 0;
             (interpreted->program_counter() != 017340
              || interpreted->right_half()) && steps != 12;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require_same_architectural_state(
            *semantic, *interpreted, label);
        require(semantic->accumulator() == test.value
                    && semantic->reg(015) == test.link,
                label + " forwards the expected table-write value");
    }

    auto table_write_cleanup_semantic = std::make_unique<Machine>();
    auto table_write_cleanup_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {table_write_cleanup_semantic.get(),
                             table_write_cleanup_interpreted.get()}) {
        prepare_table_write_wrapper(*machine);
        machine->accumulator() = Word48(07123);
        machine->reg(015) = 04103;
        machine->reg(017) = 04002;
        machine->memory(0) = Word48(0666);
        machine->memory(04000) = Word48(0123);
        machine->memory(04001) = Word48(05670);
        machine->memory(04365) = Word48(0777);
        machine->memory(04405) = Word48(03000);
        machine->start(04103);
    }
    require(table_write_cleanup_semantic->step()
                == poplan::ExecutionStatus::running,
            "04103 semantic cleanup keeps running");
    table_write_cleanup_interpreted->set_translated_routines_enabled(false);
    for (unsigned steps = 0;
         (table_write_cleanup_interpreted->program_counter() != 05670
          || table_write_cleanup_interpreted->right_half()) && steps != 24;
         ++steps) {
        require(table_write_cleanup_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "04103 instruction cleanup keeps running");
    }
    require_same_architectural_state(
        *table_write_cleanup_semantic, *table_write_cleanup_interpreted,
        "04103 cleanup");
    require(table_write_cleanup_semantic->accumulator() == Word48(0123)
                && table_write_cleanup_semantic->remainder() == Word48(07654)
                && table_write_cleanup_semantic->alu_mode() == 005
                && table_write_cleanup_semantic->reg(015) == 05670
                && table_write_cleanup_semantic->reg(017) == 04000
                && table_write_cleanup_semantic->memory(04463)
                    == Word48(0666)
                && table_write_cleanup_semantic->memory(04464)
                    == Word48(03000)
                && table_write_cleanup_semantic->memory(03645)
                    == Word48(0777),
            "04103 stores results and restores the table-write frame");

    const std::pair<std::uint16_t, Word48> alternate_hash_code[] = {
        {01122, Word48(00043000100430005ULL)},
        {01123, Word48(00043000300430004ULL)},
        {01124, Word48(00043001500430007ULL)},
        {01125, Word48(00640112075037771ULL)},
        {01126, Word48(00036010100400003ULL)},
        {01127, Word48(04445000354440007ULL)},
        {01130, Word48(02640000075107771ULL)},
        {01131, Word48(00411005406600013ULL)},
        {01132, Word48(02640000102200000ULL)},
        {01133, Word48(01410000027501135ULL)},
        {01134, Word48(00036013002200000ULL)},
        {01135, Word48(07044000400400016ULL)},
        {01136, Word48(07350114450440016ULL)},
        {01137, Word48(00220000067105430ULL)},
        {01167, Word48(04640614352400002ULL)},
        {01170, Word48(05640000003001122ULL)},
    };
    auto alternate_hash_semantic = std::make_unique<Machine>();
    auto alternate_hash_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {alternate_hash_semantic.get(),
                             alternate_hash_interpreted.get()}) {
        for (const auto &[address, word] : alternate_hash_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48();
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(001) = 07001;
        machine->reg(003) = 07003;
        machine->reg(004) = 07004;
        machine->reg(005) = 07005;
        machine->reg(007) = 07007;
        machine->reg(015) = 05670;
        machine->reg(016) = 03000;
        machine->reg(017) = 04000;
        machine->start(01167);
    }
    require(alternate_hash_semantic->step()
                == poplan::ExecutionStatus::running,
            "01167 semantic alternate hash entry keeps running");
    alternate_hash_interpreted->set_translated_routines_enabled(false);
    for (unsigned steps = 0;
         (alternate_hash_interpreted->program_counter() != 05430
          || alternate_hash_interpreted->right_half()) && steps != 48;
         ++steps) {
        require(alternate_hash_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "01167 instruction alternate hash entry keeps running");
    }
    require_same_architectural_state(
        *alternate_hash_semantic, *alternate_hash_interpreted,
        "01167 alternate shared-hash entry");

    const std::pair<std::uint16_t, Word48> compiler_chain_code[] = {
        {04675, Word48(00042000200430015ULL)},
        {04676, Word48(00043001512404536ULL)},
        {04677, Word48(01410000210120235ULL)},
        {04700, Word48(01270014466404677ULL)},
        {04701, Word48(00300446702200000ULL)},
        {04702, Word48(01410000210120245ULL)},
        {04703, Word48(01270015302203504ULL)},
        {04704, Word48(00010000012700152ULL)},
        {04705, Word48(01010024502200000ULL)},
        {04706, Word48(01003024167104447ULL)},
        {04707, Word48(06640467703004467ULL)},
        {04710, Word48(01010024613000150ULL)},
        {04711, Word48(01410000214120032ULL)},
        {04712, Word48(01260017214100002ULL)},
        {04713, Word48(01412003312600217ULL)},
        {04714, Word48(00220000067117253ULL)},
        {04715, Word48(07340471766404677ULL)},
        {04716, Word48(01300000002200000ULL)},
        {04717, Word48(00041001500410015ULL)},
        {04720, Word48(00040000267000000ULL)},
        {04721, Word48(01010006267104740ULL)},
        {04722, Word48(07010000012700170ULL)},
        {04723, Word48(01410000670000000ULL)},
        {04724, Word48(00042001667104507ULL)},
        {04725, Word48(06640454403004467ULL)},
        {04726, Word48(07241510010100062ULL)},
        {04727, Word48(06710301402200000ULL)},
        {04730, Word48(00220000067104467ULL)},
        {04731, Word48(01270020114100004ULL)},
        {04732, Word48(01012023112600201ULL)},
        {04733, Word48(01410000067104740ULL)},
        {04734, Word48(00042001610030247ULL)},
        {04735, Word48(00220000067104447ULL)},
        {04736, Word48(06640467703004467ULL)},
        {04737, Word48(07240414003003014ULL)},
        {04740, Word48(00043001534031167ULL)},
        {04741, Word48(01260021002200000ULL)},
        {04742, Word48(00040001460100000ULL)},
        {04743, Word48(00040001670100001ULL)},
        {04744, Word48(07512777612600215ULL)},
        {04745, Word48(06010000112700204ULL)},
        {04746, Word48(00010000075037775ULL)},
        {04747, Word48(00220000067105215ULL)},
        {04750, Word48(03403116767105215ULL)},
        {04751, Word48(03400116700400016ULL)},
        {04752, Word48(07010000000400016ULL)},
        {04753, Word48(07410000000410015ULL)},
        {04754, Word48(06700000002200000ULL)},
        {04755, Word48(01003025067104447ULL)},
        {04756, Word48(06640467703004467ULL)},
    };
    const auto prepare_compiler_chain = [&compiler_chain_code](
        Machine &machine) {
        for (const auto &[address, word] : compiler_chain_code) {
            machine.memory(address) = word;
        }
        machine.accumulator() = Word48(07123);
        machine.remainder() = Word48(07654);
        machine.alu_mode() = 025;
        machine.reg(002) = 01234;
        machine.reg(003) = 03000;
        machine.reg(007) = 01000;
        machine.reg(015) = 05670;
        machine.reg(016) = 03100;
        machine.reg(017) = 04000;
    };
    const auto compare_compiler_chain = [
        &prepare_compiler_chain,
        &require_same_architectural_state](std::uint16_t entry,
                                           std::uint16_t boundary,
                                           const std::string &label,
                                           const auto &configure) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            prepare_compiler_chain(*machine);
            configure(*machine);
            machine->start(entry);
        }
        require(semantic->step() == poplan::ExecutionStatus::running,
                label + " semantic path keeps running");
        interpreted->set_translated_routines_enabled(false);
        for (unsigned steps = 0;
             (interpreted->program_counter() != boundary
              || interpreted->right_half()) && steps != 64;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require(interpreted->program_counter() == boundary
                    && !interpreted->right_half(),
                label + " instruction path reaches its boundary");
        require_same_architectural_state(*semantic, *interpreted, label);
        return semantic;
    };

    const auto compiler_chain_equal = compare_compiler_chain(
        04675, 04467, "04675 first-marker call",
        [](Machine &machine) {
            machine.memory(03002) = Word48(02253);
            machine.memory(04773) = Word48(02253);
        });
    require(compiler_chain_equal->reg(002) == 04536
                && compiler_chain_equal->reg(015) == 04677
                && compiler_chain_equal->reg(017) == 04002
                && compiler_chain_equal->memory(04000) == Word48(01234)
                && compiler_chain_equal->memory(04001) == Word48(05670),
            "04675 preserves its two-word compiler-chain frame");

    const auto compiler_chain_scan = compare_compiler_chain(
        04675, 017253, "04675 shared scan call",
        [](Machine &machine) {
            machine.memory(03002) = Word48(02253);
            machine.memory(04773) = Word48(1);
            machine.memory(05003) = Word48(2);
            machine.memory(03032) = Word48(3);
            machine.memory(03033) = Word48(4);
        });
    require(compiler_chain_scan->reg(015) == 04715
                && compiler_chain_scan->accumulator() == Word48(02257),
            "04675 retains the shared 17253 scan boundary");

    const auto compiler_chain_cleanup = compare_compiler_chain(
        04715, 05670, "04715 compiler-chain cleanup",
        [](Machine &machine) {
            machine.accumulator() = Word48();
            machine.reg(002) = 04536;
            machine.reg(015) = 04715;
            machine.reg(016) = 0;
            machine.reg(017) = 04002;
            machine.memory(04000) = Word48(01234);
            machine.memory(04001) = Word48(05670);
        });
    require(compiler_chain_cleanup->reg(002) == 01234
                && compiler_chain_cleanup->reg(015) == 05670
                && compiler_chain_cleanup->reg(017) == 04000,
            "04715 restores the compiler-chain frame");

    const auto compiler_chain_helper_entry = compare_compiler_chain(
        04721, 04740, "04721 chain-helper entry",
        [](Machine &machine) {
            machine.reg(002) = 04536;
            machine.reg(015) = 04561;
            machine.memory(04620) = Word48(06440000000065453ULL);
        });
    require(compiler_chain_helper_entry->accumulator()
                    == Word48(06440000000065453ULL)
                && compiler_chain_helper_entry->reg(015) == 04722,
            "04721 forwards the selected word to the 04740 boundary");

    const auto compiler_chain_select = compare_compiler_chain(
        04722, 04507, "04722 selected record call",
        [](Machine &machine) {
            machine.reg(002) = 04536;
            machine.reg(003) = 03000;
            machine.reg(015) = 04722;
            machine.reg(016) = 03100;
            machine.memory(03100) = Word48();
            machine.memory(03006) = Word48(022);
        });
    require(compiler_chain_select->memory(03100) == Word48(022)
                && compiler_chain_select->accumulator() == Word48(03100)
                && compiler_chain_select->reg(015) == 04725,
            "04722 installs the selected record before 04507");

    const auto compiler_chain_finish = compare_compiler_chain(
        04751, 04722, "04751 chain-helper return",
        [](Machine &machine) {
            machine.accumulator() = Word48(03200);
            machine.reg(007) = 01000;
            machine.reg(015) = 04751;
            machine.reg(017) = 04002;
            machine.memory(03200) = Word48(03300);
            machine.memory(04000) = Word48(065453);
            machine.memory(04001) = Word48(04722);
        });
    require(compiler_chain_finish->memory(02167) == Word48(03200)
                && compiler_chain_finish->reg(015) == 04722
                && compiler_chain_finish->reg(016) == 03300
                && compiler_chain_finish->reg(017) == 04000
                && compiler_chain_finish->accumulator() == Word48(065453),
            "04751 stores without popping before its stacked return");

    const std::pair<std::uint16_t, Word48> chain_helper_code[] = {
        {04740, Word48(00043001534031167ULL)},
        {04741, Word48(01260021002200000ULL)},
        {04742, Word48(00040001460100000ULL)},
        {04743, Word48(00040001670100001ULL)},
        {04744, Word48(07512777612600215ULL)},
        {04745, Word48(06010000112700204ULL)},
        {04746, Word48(00010000075037775ULL)},
        {04747, Word48(00220000067105215ULL)},
        {04750, Word48(03403116767105215ULL)},
        {04751, Word48(03400116700400016ULL)},
        {04752, Word48(07010000000400016ULL)},
        {04753, Word48(07410000000410015ULL)},
        {04754, Word48(06700000002200000ULL)},
    };
    auto chain_helper_semantic = std::make_unique<Machine>();
    auto chain_helper_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {chain_helper_semantic.get(),
                             chain_helper_interpreted.get()}) {
        for (const auto &[address, word] : chain_helper_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0123);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(002) = 04536;
        machine->reg(007) = 01000;
        machine->reg(015) = 05670;
        machine->reg(017) = 04000;
        machine->memory(02167) = Word48();
        machine->start(04740);
    }
    require(chain_helper_semantic->step()
                == poplan::ExecutionStatus::running,
            "04740 semantic zero-chain path keeps running");
    chain_helper_interpreted->set_translated_routines_enabled(false);
    for (unsigned steps = 0;
         (chain_helper_interpreted->program_counter() != 05215
          || chain_helper_interpreted->right_half()) && steps != 24;
         ++steps) {
        require(chain_helper_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "04740 instruction zero-chain path keeps running");
    }
    require_same_architectural_state(
        *chain_helper_semantic, *chain_helper_interpreted,
        "04740 zero-chain allocation entry");

    auto chain_match_semantic = std::make_unique<Machine>();
    auto chain_match_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {chain_match_semantic.get(),
                             chain_match_interpreted.get()}) {
        for (const auto &[address, word] : chain_helper_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0123);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(002) = 04536;
        machine->reg(007) = 01000;
        machine->reg(015) = 05670;
        machine->reg(017) = 04000;
        machine->memory(02167) = Word48(03000);
        machine->memory(03000) = Word48(03100);
        machine->memory(03101) = Word48(0123);
        machine->start(04740);
    }
    require(chain_match_semantic->step()
                == poplan::ExecutionStatus::running,
            "04740 semantic matching-chain path keeps running");
    chain_match_interpreted->set_translated_routines_enabled(false);
    for (unsigned steps = 0;
         (chain_match_interpreted->program_counter() != 05670
          || chain_match_interpreted->right_half()) && steps != 32;
         ++steps) {
        require(chain_match_interpreted->step()
                    == poplan::ExecutionStatus::running,
                    "04740 instruction matching-chain path keeps running");
    }
    require_same_architectural_state(
        *chain_match_semantic, *chain_match_interpreted,
        "04740 matching-chain dynamic return");

    const std::pair<std::uint16_t, Word48> table_search_code[] = {
        {016254, Word48(04241625440000036ULL)},
        {016255, Word48(00037000340100030ULL)},
        {016256, Word48(04000003470100000ULL)},
        {016257, Word48(04000003502200000ULL)},
        {016260, Word48(04010003440040031ULL)},
        {016261, Word48(04012003542600021ULL)},
        {016262, Word48(04010003440040035ULL)},
        {016263, Word48(00036010174000000ULL)},
        {016264, Word48(04012003270170001ULL)},
        {016265, Word48(00031010000400014ULL)},
        {016266, Word48(07220000362500000ULL)},
        {016267, Word48(06010000070110002ULL)},
        {016270, Word48(04012003340130036ULL)},
        {016271, Word48(04260001774100000ULL)},
        {016272, Word48(04000003443000004ULL)},
        {016273, Word48(07410000040000035ULL)},
        {016274, Word48(06044001343000004ULL)},
        {016275, Word48(04010003570120000ULL)},
        {016276, Word48(04260002554100000ULL)},
        {016277, Word48(07011000240120036ULL)},
        {016300, Word48(04260002743000026ULL)},
        {016301, Word48(06044001372300001ULL)},
        {016302, Word48(05650000040100031ULL)},
        {016303, Word48(05444001667000000ULL)},
        {016304, Word48(00037777777777777ULL)},
        {016305, Word48(1)},
        {016306, Word48(04000000000000000ULL)},
        {016307, Word48(07777777777777777ULL)},
    };
    auto semantic_table_search = std::make_unique<Machine>();
    auto interpreted_table_search = std::make_unique<Machine>();
    for (Machine *machine : {semantic_table_search.get(),
                             interpreted_table_search.get()}) {
        for (const auto &[address, word] : table_search_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(01717);
        machine->remainder() = Word48();
        machine->alu_mode() = 007;
        machine->reg(001) = 017624;
        machine->reg(002) = 065517;
        machine->reg(003) = 065523;
        machine->reg(006) = 070000;
        machine->reg(007) = 01200;
        machine->reg(011) = 05447;
        machine->reg(012) = 4;
        machine->reg(013) = 077766;
        machine->reg(014) = 032553;
        machine->reg(015) = 017700;
        machine->reg(016) = 025575;
        machine->reg(017) = 066027;
        machine->memory(025575) = Word48(020);
        machine->memory(025576) = Word48(2);
        machine->memory(025577) = Word48(077777);
        machine->memory(025600) =
            Word48(02021010000001403ULL);
        machine->memory(025616) =
            Word48(02021010000001437ULL);
        machine->memory(025626) =
            Word48(02111212000001677ULL);
        machine->memory(025632) =
            Word48(02011010000002063ULL);
        machine->memory(025630) =
            Word48(02111010000001747ULL);
        machine->memory(066027) =
            Word48(0140000000000000ULL);
        machine->memory(066030) = Word48(017633);
        machine->memory(066031) = Word48(017764);
        machine->start(016254);
    }

    require(semantic_table_search->step()
                == poplan::ExecutionStatus::running,
            "16254 semantic path keeps running");
    interpreted_table_search->disable_translated_routine(016254);
    for (unsigned steps = 0;
         (interpreted_table_search->program_counter() != 017700
          || interpreted_table_search->right_half()) && steps != 160;
         ++steps) {
        require(interpreted_table_search->step()
                    == poplan::ExecutionStatus::running,
                "16254 instruction path keeps running");
    }
    require(interpreted_table_search->program_counter() == 017700
                && !interpreted_table_search->right_half(),
            "16254 instruction path returns through r15");
    require_same_architectural_state(
        *semantic_table_search, *interpreted_table_search, "16254");
    require(semantic_table_search->accumulator() == Word48(1)
                && semantic_table_search->remainder() == Word48(050)
                && semantic_table_search->alu_mode() == 007
                && semantic_table_search->reg(013) == 025630
                && semantic_table_search->reg(014) == 025630
                && semantic_table_search->reg(016) == 025630
                && semantic_table_search->reg(017) == 066027,
            "16254 reproduces the observed table-search result and frame");

    const std::pair<std::uint16_t, Word48> compiler_repeat_code[] = {
        {004665, Word48(00042000200430015ULL)},
        {004666, Word48(00043001512404536ULL)},
        {004667, Word48(07240354467103536ULL)},
        {004670, Word48(01410000210120244ULL)},
        {004671, Word48(01270013566404667ULL)},
        {004672, Word48(00300446702200000ULL)},
        {004673, Word48(00041001500410015ULL)},
        {004674, Word48(00040000267000000ULL)},
    };
    const auto compare_compiler_repeat = [
        &compiler_repeat_code,
        &require_same_architectural_state](
            std::uint16_t entry, bool equal_fields,
            std::uint16_t modifier = 0) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : compiler_repeat_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(0) = modifier;
            machine->reg(001) = 02261;
            machine->reg(002) = 04536;
            machine->reg(003) = 03000;
            machine->reg(015) = entry == 04665 ? 04605 : 04670;
            machine->reg(016) = 05200;
            machine->reg(017) = entry == 04673 ? 04003 : 04001;
            machine->memory(03002) = Word48(055);
            machine->memory(05002) =
                equal_fields ? Word48(055) : Word48(066);
            machine->memory(04000) = Word48(0777);
            machine->memory(04001) = Word48(04536);
            machine->memory(04002) = Word48(04605);
            machine->start(entry);
        }

        require(semantic->step() == poplan::ExecutionStatus::running,
                "04665 semantic path keeps running");
        const std::uint16_t continuation = semantic->program_counter();
        for (const std::uint16_t address :
             {004665, 004667, 004670, 004673}) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 32;
             ++steps) {
            require(interpreted->step() == poplan::ExecutionStatus::running,
                    "04665 instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "04665 instruction path reaches its semantic boundary");
        if (semantic->accumulator() != interpreted->accumulator()
            || semantic->remainder() != interpreted->remainder()
            || semantic->alu_mode() != interpreted->alu_mode()) {
            std::cerr << "04665 entry/equal/modifier ACC semantic/raw: "
                      << std::oct << entry << '/' << equal_fields << '/'
                      << modifier << ' ' << semantic->accumulator().raw()
                      << '/' << interpreted->accumulator().raw()
                      << " RMR " << semantic->remainder().raw() << '/'
                      << interpreted->remainder().raw() << " mode "
                      << static_cast<unsigned>(semantic->alu_mode()) << '/'
                      << static_cast<unsigned>(interpreted->alu_mode())
                      << '\n';
        }
        require_same_architectural_state(
            *semantic, *interpreted, "04665 compiler repeat");
        return continuation;
    };

    require(compare_compiler_repeat(04665, false) == 03536,
            "04665 saves its caller and enters the 03536 frame builder");
    require(compare_compiler_repeat(04665, false, 1) == 03536,
            "04665 ignores dirty r0 storage in short instructions");
    require(compare_compiler_repeat(04667, false) == 03536,
            "04667 preserves its independent repeat entry");
    require(compare_compiler_repeat(04670, false) == 04673,
            "04670 selects restoration for unequal record fields");
    require(compare_compiler_repeat(04670, true) == 04467,
            "04670 repeats through 04467 for equal record fields");
    require(compare_compiler_repeat(04673, false) == 04605,
            "04673 restores the compiler base and caller");
    require(compare_compiler_repeat(04673, false, 1) == 04605,
            "04673 restores through r15 when r0 storage is dirty");

    auto allocator_entry_semantic = std::make_unique<Machine>();
    auto allocator_entry_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {allocator_entry_semantic.get(),
                             allocator_entry_interpreted.get()}) {
        machine->memory(017762) = Word48(0043001574000000ULL);
        machine->memory(017763) = Word48(07240000267105430ULL);
        machine->accumulator() = Word48(01400000000000123ULL);
        machine->remainder() = Word48(077);
        machine->alu_mode() = 031;
        machine->reg(0) = 012;
        machine->reg(015) = 017633;
        machine->reg(017) = 04000;
        machine->start(017762);
    }
    allocator_entry_semantic->step();
    allocator_entry_interpreted->disable_translated_routine(017762);
    while (allocator_entry_interpreted->program_counter() != 05430
           || allocator_entry_interpreted->right_half()) {
        require(allocator_entry_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "17762 instruction entry keeps running");
    }
    require_same_architectural_state(
        *allocator_entry_semantic, *allocator_entry_interpreted, "17762");
    require(allocator_entry_semantic->program_counter() == 05430
                && allocator_entry_semantic->reg(016) == 2
                && allocator_entry_semantic->reg(015) == 017764
                && allocator_entry_semantic->reg(017) == 04002,
            "17762 builds its two-word allocator frame");

    auto allocator_resume_semantic = std::make_unique<Machine>();
    auto allocator_resume_interpreted = std::make_unique<Machine>();
    const std::pair<std::uint16_t, Word48> allocator_resume_code[] = {
        {017764, Word48(0037000375107776ULL)},
        {017765, Word48(07001000000410015ULL)},
        {017766, Word48(0042001604120205ULL)},
        {017767, Word48(01000000100400002ULL)},
        {017770, Word48(0042000304120205ULL)},
        {017771, Word48(07000000104100235ULL)},
        {017772, Word48(0413016404000235ULL)},
        {017773, Word48(06700000002200000ULL)},
    };
    for (Machine *machine : {allocator_resume_semantic.get(),
                             allocator_resume_interpreted.get()}) {
        for (const auto &[address, word] : allocator_resume_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(06500000000000000ULL);
        machine->remainder() = Word48(076);
        machine->alu_mode() = 025;
        machine->reg(0) = 0;
        machine->reg(001) = 01000;
        machine->reg(002) = 02000;
        machine->reg(003) = 03000;
        machine->reg(015) = 05433;
        machine->reg(016) = 05000;
        machine->reg(017) = 04002;
        machine->memory(04000) = Word48(01400000000000123ULL);
        machine->memory(04001) = Word48(017633);
        machine->memory(01205) = Word48(07200000000000000ULL);
        machine->memory(01235) = Word48(1);
        machine->memory(01164) = Word48(1);
        machine->start(017764);
    }
    allocator_resume_semantic->step();
    allocator_resume_interpreted->disable_translated_routine(017764);
    while (allocator_resume_interpreted->program_counter() != 017633
           || allocator_resume_interpreted->right_half()) {
        require(allocator_resume_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "17764 instruction continuation keeps running");
    }
    require_same_architectural_state(
        *allocator_resume_semantic, *allocator_resume_interpreted, "17764");
    require(allocator_resume_semantic->reg(017) == 04000
                && allocator_resume_semantic->memory(05000)
                    == Word48(01400000000000123ULL)
                && allocator_resume_semantic->memory(01235) == Word48(2),
            "17764 installs the allocated object and balances its frame");

    const std::pair<std::uint16_t, Word48> compiler_select_code[] = {
        {03736, Word48(0043001500430002ULL)},
        {03737, Word48(0043000400430004ULL)},
        {03740, Word48(01240353667117417ULL)},
        {03741, Word48(07350374367104467ULL)},
        {03742, Word48(07240000013000213ULL)},
        {03743, Word48(01010010510120630ULL)},
        {03744, Word48(01270022310100643ULL)},
        {03745, Word48(01012010312700216ULL)},
        {03746, Word48(02240000002200000ULL)},
        {03747, Word48(0220000067104214ULL)},
        {03750, Word48(07240000002200000ULL)},
        {03751, Word48(0041000400410004ULL)},
        {03752, Word48(0041000200410015ULL)},
        {03753, Word48(06700000002200000ULL)},
        {03754, Word48(01010010310120644ULL)},
        {03755, Word48(01270022122400001ULL)},
        {03756, Word48(01300021102200000ULL)},
        {03757, Word48(01010010310120645ULL)},
        {03760, Word48(01260022402200000ULL)},
        {03761, Word48(07240000113000213ULL)},
    };
    const auto install_compiler_select_code = [
        &compiler_select_code](Machine &machine) {
        for (const auto &[address, word] : compiler_select_code) {
            machine.memory(address) = word;
        }
    };

    auto compiler_select_entry_semantic = std::make_unique<Machine>();
    auto compiler_select_entry_interpreted = std::make_unique<Machine>();
    for (Machine *machine : {compiler_select_entry_semantic.get(),
                             compiler_select_entry_interpreted.get()}) {
        install_compiler_select_code(*machine);
        machine->accumulator() = Word48(0123456701234567ULL);
        machine->remainder() = Word48(076);
        machine->alu_mode() = 025;
        machine->reg(001) = 01111;
        machine->reg(002) = 02222;
        machine->reg(004) = 04444;
        machine->reg(015) = 03550;
        machine->reg(017) = 04000;
        machine->start(03736);
    }
    compiler_select_entry_semantic->step();
    compiler_select_entry_interpreted->disable_translated_routine(03736);
    while (compiler_select_entry_interpreted->program_counter() != 017417
           || compiler_select_entry_interpreted->right_half()) {
        require(compiler_select_entry_interpreted->step()
                    == poplan::ExecutionStatus::running,
                "03736 instruction entry keeps running");
    }
    require_same_architectural_state(
        *compiler_select_entry_semantic,
        *compiler_select_entry_interpreted, "03736");
    require(compiler_select_entry_semantic->reg(002) == 03536
                && compiler_select_entry_semantic->reg(015) == 03741
                && compiler_select_entry_semantic->reg(017) == 04004,
            "03736 builds its four-word saved-register frame");

    const auto compare_compiler_select_continuation = [
        &install_compiler_select_code,
        &require_same_architectural_state](
            std::uint16_t entry, std::uint16_t status,
            Word48 first_left, Word48 first_right,
            Word48 second_left, Word48 second_right,
            Word48 third_right) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            install_compiler_select_code(*machine);
            machine->accumulator() = Word48(0777);
            machine->remainder() = Word48(076);
            machine->alu_mode() = 025;
            machine->reg(002) = 03536;
            machine->reg(004) = 01234;
            machine->reg(015) = entry;
            machine->reg(016) = status;
            machine->reg(017) = 04004;
            machine->memory(04000) = Word48(0123456701234567ULL);
            machine->memory(04001) = Word48(03550);
            machine->memory(04002) = Word48(02222);
            machine->memory(04003) = Word48(04444);
            machine->memory(03643) = first_left;
            machine->memory(04366) = first_right;
            machine->memory(04401) = second_left;
            machine->memory(03641) = second_right;
            machine->memory(04402) = Word48(012);
            machine->memory(04403) = third_right;
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        for (const std::uint16_t address : {
                 03741, 03742, 03743, 03750, 03751,
                 03754, 03757, 03761}) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 64;
             ++steps) {
            require(interpreted->step() == poplan::ExecutionStatus::running,
                    "03736 continuation instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "03736 continuation reaches its semantic boundary");
        require_same_architectural_state(
            *semantic, *interpreted, "03736 continuation");
        return continuation;
    };

    require(compare_compiler_select_continuation(
                03741, 0, Word48(), Word48(), Word48(), Word48(), Word48())
                == 04467,
            "03741 retains the compiler-wrapper call boundary");
    require(compare_compiler_select_continuation(
                03742, 0, Word48(), Word48(), Word48(), Word48(), Word48())
                == 03550,
            "03742 restores the caller after the compiler wrapper");
    require(compare_compiler_select_continuation(
                03741, 1, Word48(1), Word48(2),
                Word48(), Word48(), Word48()) == 03550,
            "03743 restores directly after its first unequal comparison");
    require(compare_compiler_select_continuation(
                03741, 1, Word48(5), Word48(5),
                Word48(7), Word48(7), Word48()) == 04214,
            "03743 retains the 04214 call boundary for equal values");
    require(compare_compiler_select_continuation(
                03741, 1, Word48(5), Word48(5),
                Word48(7), Word48(010), Word48(011)) == 03550,
            "03754/03757 restore after their unequal comparisons");
    require(compare_compiler_select_continuation(
                03741, 1, Word48(5), Word48(5),
                Word48(7), Word48(010), Word48(010)) == 03762,
            "03757 retains its generated-code continuation on equality");
    require(compare_compiler_select_continuation(
                03750, 0, Word48(), Word48(), Word48(), Word48(), Word48())
                == 03550,
            "03750 restores the caller after 04214");

    const std::pair<std::uint16_t, Word48> compiler_classify_code[] = {
        {017417, Word48(00042000200430015ULL)},
        {017420, Word48(01241741714030000ULL)},
        {017421, Word48(00036010110130044ULL)},
        {017422, Word48(01260001102200000ULL)},
        {017423, Word48(01410000067106134ULL)},
        {017424, Word48(01003004567104447ULL)},
        {017425, Word48(07240000002200000ULL)},
        {017426, Word48(07410000000410015ULL)},
        {017427, Word48(00040000267000000ULL)},
        {017430, Word48(01410000210120046ULL)},
        {017431, Word48(01270002367106526ULL)},
        {017432, Word48(07400000010110047ULL)},
        {017433, Word48(01012005012600016ULL)},
        {017434, Word48(07240440067103014ULL)},
        {017435, Word48(06710446702200000ULL)},
        {017436, Word48(07257532173417440ULL)},
        {017437, Word48(07240461067103014ULL)},
        {017440, Word48(07410000014000000ULL)},
        {017441, Word48(01300000402200000ULL)},
        {017442, Word48(01410000210120051ULL)},
        {017443, Word48(01270003566417450ULL)},
        {017444, Word48(00042001500430004ULL)},
        {017445, Word48(00043000700430005ULL)},
        {017446, Word48(01403000074000000ULL)},
        {017447, Word48(03400105703015322ULL)},
        {017450, Word48(01444001667103303ULL)},
        {017451, Word48(01410000067117571ULL)},
        {017452, Word48(01003005267104447ULL)},
        {017453, Word48(07240000013000007ULL)},
        {017454, Word48(01410000000360151ULL)},
        {017455, Word48(00040001462577627ULL)},
        {017456, Word48(06341746162577777ULL)},
        {017457, Word48(06341742362577777ULL)},
        {017460, Word48(06341742313000032ULL)},
        {017461, Word48(07240000113000007ULL)},
        {017462, Word48(06440000000002410ULL)},
        {017463, Word48(00560000000000000ULL)},
        {017464, Word48(00010000000000000ULL)},
        {017465, Word48(00000000000002457ULL)},
        {017466, Word48(07740000000000000ULL)},
        {017467, Word48(06440000000000000ULL)},
        {017470, Word48(00000000000002257ULL)},
        {017471, Word48(00100000000000000ULL)},
    };
    const auto compare_compiler_classify = [
        &compiler_classify_code,
        &require_same_architectural_state](
            std::uint16_t entry, Word48 object, Word48 field,
            Word48 accumulator, std::uint16_t status,
            std::uint16_t stack) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : compiler_classify_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = accumulator;
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 01111;
            machine->reg(002) = entry == 017417 ? 03536 : 017417;
            machine->reg(003) = 03000;
            machine->reg(004) = 04444;
            machine->reg(005) = 05555;
            machine->reg(007) = 07000;
            machine->reg(014) = 01414;
            machine->reg(015) = 03550;
            machine->reg(016) = status;
            machine->reg(017) = stack;
            machine->memory(03000) = object;
            machine->memory(03002) = field;
            for (std::uint16_t address = 04000;
                 address != 04012; ++address) {
                machine->memory(address) = Word48(address - 04000 + 05000);
            }
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        for (const std::uint16_t address : {
                 017417, 017423, 017424, 017425, 017426,
                 017430, 017432, 017435, 017436, 017440,
                 017442, 017451, 017452, 017453, 017454,
                 017461}) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 64;
             ++steps) {
            require(interpreted->step() == poplan::ExecutionStatus::running,
                    "17417 instruction path keeps running");
        }
        if (interpreted->program_counter() != continuation
            || interpreted->right_half()) {
            std::cerr << "17417 entry/target/current: " << std::oct
                      << entry << '/' << continuation << '/'
                      << interpreted->program_counter() << '\n';
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "17417 instruction path reaches its semantic boundary");
        if (semantic->accumulator() != interpreted->accumulator()
            || semantic->remainder() != interpreted->remainder()
            || semantic->alu_mode() != interpreted->alu_mode()) {
            std::cerr << "17417 ALU mismatch at entry " << std::oct << entry
                      << ": semantic acc/rmr/rau="
                      << semantic->accumulator().raw() << '/'
                      << semantic->remainder().raw() << '/'
                      << static_cast<unsigned>(semantic->alu_mode())
                      << " interpreted="
                      << interpreted->accumulator().raw() << '/'
                      << interpreted->remainder().raw() << '/'
                      << static_cast<unsigned>(interpreted->alu_mode())
                      << '\n';
        }
        require_same_architectural_state(
            *semantic, *interpreted, "17417 compiler classification");
        return continuation;
    };

    require(compare_compiler_classify(
                017417, Word48(06400000000000100ULL), Word48(),
                Word48(1), 0, 04000) == 06134,
            "17417 retains the 06134 object-processing boundary");
    require(compare_compiler_classify(
                017417, Word48(06440000000000000ULL), Word48(02457),
                Word48(), 0, 04000) == 06526,
            "17417 retains the 06526 equal-field boundary");
    require(compare_compiler_classify(
                017417, Word48(06440000000000000ULL), Word48(02257),
                Word48(), 0, 04000) == 015322,
            "17417 builds the generated frame for 15322");
    require(compare_compiler_classify(
                017424, Word48(06400000000000100ULL), Word48(),
                Word48(0123), 0, 04002) == 04447,
            "17424 retains the allocation boundary");
    require(compare_compiler_classify(
                017425, Word48(), Word48(), Word48(0777),
                012, 04003) == 05002,
            "17425 restores the saved frame and caller");
    require(compare_compiler_classify(
                017432, Word48(), Word48(),
                Word48(06440000000000000ULL), 0, 04002) == 04467,
            "17432 retains its equal-mask compiler boundary");
    require(compare_compiler_classify(
                017432, Word48(), Word48(),
                Word48(07200000000000000ULL), 0, 04002) == 03014,
            "17432 retains its diagnostic boundary");
    require(compare_compiler_classify(
                017435, Word48(), Word48(), Word48(),
                0, 04002) == 04467,
            "17435 retains the compiler-wrapper boundary");
    require(compare_compiler_classify(
                017436, Word48(06400000000000100ULL), Word48(),
                Word48(), 02457, 04002) == 06134,
            "17436 resumes object processing after a zero status adjustment");
    require(compare_compiler_classify(
                017436, Word48(), Word48(), Word48(),
                1, 04002) == 03014,
            "17436 retains its second diagnostic boundary");
    require(compare_compiler_classify(
                017440, Word48(), Word48(), Word48(),
                0, 04002) == 06134,
            "17440 installs the diagnostic result and resumes at 06134");
    require(compare_compiler_classify(
                017442, Word48(), Word48(02257), Word48(),
                0, 04002) == 015322,
            "17442 retains the generated transfer boundary");
    require(compare_compiler_classify(
                017454, Word48(std::uint64_t{0152} << 41), Word48(),
                Word48(), 0, 04002) == 06134,
            "17454 routes the traced adjacent classes through 06134");
    require(compare_compiler_classify(
                017454, Word48(std::uint64_t{0153} << 41), Word48(),
                Word48(), 0, 04002) == 06134,
            "17454 routes the second adjacent class through 06134");
    require(compare_compiler_classify(
                017454, Word48(std::uint64_t{0151} << 41), Word48(),
                Word48(), 0, 04002) == 05001,
            "17454 restores the caller for its direct-return class");
    require(compare_compiler_classify(
                017454, Word48(std::uint64_t{0162} << 41), Word48(),
                Word48(), 0, 04002) == 017571,
            "17454 retains the 17571 classification boundary");
    require(compare_compiler_classify(
                017452, Word48(), Word48(), Word48(0123),
                0, 04002) == 04447,
            "17452 retains its allocation boundary");
    require(compare_compiler_classify(
                017453, Word48(), Word48(), Word48(),
                077, 04003) == 05002,
            "17453 clears the status and restores the saved frame");

    const std::pair<std::uint16_t, Word48> record_chain_code[] = {
        {017774, Word48(01741775614100000ULL)},
        {017775, Word48(00411016604120204ULL)},
        {017776, Word48(00660015614440002ULL)},
        {017777, Word48(01410000100400003ULL)},
        {020000, Word48(00410023504130164ULL)},
        {020001, Word48(00400023567000000ULL)},
        {020002, Word48(00410023516300000ULL)},
        {020003, Word48(00000000014440002ULL)},
        {020004, Word48(01410000100400003ULL)},
        {020005, Word48(01410000004110166ULL)},
        {020006, Word48(00412020406600156ULL)},
        {020007, Word48(06700000002200000ULL)},
    };
    const auto compare_record_chain = [
        &record_chain_code,
        &require_same_architectural_state](
            std::uint16_t entry, unsigned chain_length) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : record_chain_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 017624;
            machine->reg(002) = 02222;
            machine->reg(003) = chain_length == 0 ? 0 : 03000;
            machine->reg(015) = 04567;
            machine->reg(017) = 04000;
            machine->memory(020010) = Word48(1);
            machine->memory(020012) = Word48(Word48::mask);
            machine->memory(020030) = Word48(055);
            machine->memory(020061) = Word48(035);
            machine->memory(03000) =
                chain_length >= 2 ? Word48(055) : Word48(1);
            machine->memory(03001) = Word48(03100);
            machine->memory(03100) =
                chain_length >= 3 ? Word48(055) : Word48(1);
            machine->memory(03101) = Word48(03200);
            machine->memory(03200) = Word48(1);
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        interpreted->disable_translated_routine(017774);
        interpreted->disable_translated_routine(020002);
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 128;
             ++steps) {
            require(interpreted->step() == poplan::ExecutionStatus::running,
                    "17774 instruction path keeps running");
        }
        if (interpreted->program_counter() != continuation
            || interpreted->right_half()) {
            std::cerr << "17774 entry/length/target/current: " << std::oct
                      << entry << '/' << chain_length << '/'
                      << continuation << '/'
                      << interpreted->program_counter() << '\n';
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "17774 instruction path reaches its semantic boundary");
        for (std::size_t address = 0;
             address != Machine::core_words; ++address) {
            if (semantic->memory(static_cast<std::uint16_t>(address))
                != interpreted->memory(
                    static_cast<std::uint16_t>(address))) {
                std::cerr << "17774 memory mismatch entry/length/address: "
                          << std::oct << entry << '/' << chain_length << '/'
                          << address << " semantic/interpreted="
                          << semantic->memory(
                                 static_cast<std::uint16_t>(address)).raw()
                          << '/'
                          << interpreted->memory(
                                 static_cast<std::uint16_t>(address)).raw()
                          << '\n';
                break;
            }
        }
        require_same_architectural_state(
            *semantic, *interpreted, "17774 record chain");
        return continuation;
    };

    require(compare_record_chain(017774, 0) == 017756,
            "17774 preserves the empty-chain restoration boundary");
    require(compare_record_chain(017774, 1) == 04567,
            "17774 updates and returns for an ordinary record");
    require(compare_record_chain(017774, 2) == 04567,
            "17774 follows one matched record through 20002");
    require(compare_record_chain(017774, 3) == 04567,
            "17774 follows multiple matched records through 20002");
    require(compare_record_chain(020002, 2) == 04567,
            "20002 retains its independent chain-continuation entry");

    // The first traced 16421 call maps tagged byte 012 to code 014. It uses
    // table word 16440, r12=1, and the r13-controlled 12-bit left shift.
    auto tagged_byte_012 = std::make_unique<Machine>();
    install_tagged_byte_lookup(*tagged_byte_012);
    tagged_byte_012->reg(015) = 016350;
    tagged_byte_012->accumulator() =
        Word48(06400000000000012ULL);
    require(tagged_byte_012->p16421_lookup_tagged_byte() == 016350,
            "16421 returns tagged byte 012 through r15");
    require(tagged_byte_012->accumulator() == Word48(014)
                && tagged_byte_012->memory(016435) == Word48(014),
            "16421 reproduces the traced 012 to 014 lookup");
    require(tagged_byte_012->reg(012) == 01
                && tagged_byte_012->reg(013) == 063
                && tagged_byte_012->memory(016436) == Word48(04),
            "16421 reproduces the traced table and field selectors");
    require(tagged_byte_012->alu_mode() == 004,
            "16421 leaves the ALU in logical mode");

    auto tagged_byte_106 = std::make_unique<Machine>();
    install_tagged_byte_lookup(*tagged_byte_106);
    tagged_byte_106->reg(015) = 01234;
    tagged_byte_106->accumulator() =
        Word48(06400000000000106ULL);
    require(tagged_byte_106->p16421_lookup_tagged_byte() == 01234
                && tagged_byte_106->accumulator() == Word48(01)
                && tagged_byte_106->reg(012) == 010
                && tagged_byte_106->reg(013) == 033,
            "16421 reproduces the traced 106 to 001 lookup");

    auto tagged_byte_040 = std::make_unique<Machine>();
    install_tagged_byte_lookup(*tagged_byte_040);
    tagged_byte_040->reg(015) = 02345;
    tagged_byte_040->accumulator() =
        Word48(06400000000000040ULL);
    require(tagged_byte_040->p16421_lookup_tagged_byte() == 02345
                && tagged_byte_040->accumulator() == Word48()
                && tagged_byte_040->reg(012) == 04
                && tagged_byte_040->reg(013) == 077,
            "16421 reproduces the traced 040 to 000 lookup");

    auto untagged_byte = std::make_unique<Machine>();
    install_tagged_byte_lookup(*untagged_byte);
    untagged_byte->reg(015) = 03456;
    untagged_byte->reg(012) = 071;
    untagged_byte->reg(013) = 072;
    untagged_byte->accumulator() = Word48(012);
    require(untagged_byte->p16421_lookup_tagged_byte() == 03456
                && untagged_byte->accumulator() == Word48(015)
                && untagged_byte->memory(016435) == Word48(015),
            "16421 returns code 015 for a value outside the tagged form");
    require(untagged_byte->reg(012) == 071
                && untagged_byte->reg(013) == 072,
            "16421 rejects an untagged value before selecting a field");

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
    install_input_continue(terminated_output);
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
    require(terminated_output.p20245_begin_input_continue() == 020256,
            "20245 selects the translated terminal-transfer entry");
    require(terminated_output.p20256_transfer_console() == 025361
                && terminated_output.console_output().empty()
                && terminated_output.reg(016) == 0,
            "20256 sends the EOF-terminated buffer through E71");
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

    // Trace at the first diagnostic 0377: no pending input/status word, so
    // 20245 proceeds directly to the Э71 0177 boundary at 20256.
    Machine input_continue;
    install_input_continue(input_continue);
    input_continue.memory(020440) =
        Word48(std::uint64_t{0377} << 40);
    input_continue.reg(015) = 025361;
    input_continue.alu_mode() = 003;
    require(input_continue.p20245_begin_input_continue() == 020256,
            "20245 selects traced Э71 0177 when input state is clear");
    require(input_continue.reg(010) == 020170
                && input_continue.accumulator() == Word48()
                && input_continue.remainder() == Word48()
                && input_continue.alu_mode() == 007,
            "20245 preserves the traced pre-extracode machine state");
    require(input_continue.p20256_transfer_console() == 025361,
            "20256 executes Э71 0177 and returns through the saved link");
    require(input_continue.memory(020362) == Word48(1)
                && input_continue.accumulator() == Word48(1),
            "20257 marks the continuation state available");

    // On later calls, state 20362 selects Э71 0146 first. Its traced result
    // packs to 4000000000000000 and falls through to Э71 0177.
    Machine input_status;
    install_input_continue(input_status);
    input_status.memory(020362) = Word48(1);
    input_status.reg(015) = 025361;
    require(input_status.p20245_begin_input_continue() == 020252
                && input_status.accumulator() == Word48(1),
            "20245 selects traced status extracode Э71 0146");
    require(input_status.p20252_query_console() == 020256,
            "20252 executes Э71 0146 and selects terminal transfer");
    require(input_status.accumulator()
                == Word48(02000000000000000ULL)
                && input_status.remainder()
                    == Word48(02000000000000000ULL),
            "20253 reproduces traced APX and tag comparisons");

    // The POPLAN output control word uses r10-relative buffer limits and an
    // expanded right address to request the terminal status word.
    auto e71_output = std::make_unique<Machine>();
    install_input_continue(*e71_output);
    e71_output->reg(010) = 020170;
    e71_output->reg(015) = 07654;
    e71_output->memory(020440) = Word48(
        (std::uint64_t{031} << 40)
        | (std::uint64_t{040} << 32)
        | (std::uint64_t{0377} << 24));
    require(e71_output->p20256_transfer_console() == 07654,
            "20256 returns after the E71 output control word");
    require(e71_output->console_output()
                == std::vector<std::uint8_t>({031, 040})
                && e71_output->memory(020362) == Word48(1),
            "E71 emits GOST bytes up to 0377 and completes the continuation");

    // The corresponding input control word writes a queued GOST line, its
    // 0377 terminator, and zero padding into the r10-relative input buffer.
    auto e71_input = std::make_unique<Machine>();
    e71_input->reg(010) = 020170;
    e71_input->memory(020364) =
        Word48(04034021041120221ULL);
    e71_input->queue_console_input({031, 052});
    e71_input->emulate_e71(020364);
    require(e71_input->memory(020400) == Word48(
                (std::uint64_t{031} << 40)
                | (std::uint64_t{052} << 32)
                | (std::uint64_t{0377} << 24))
                && e71_input->accumulator()
                    == Word48(01000000200000012ULL)
                && e71_input->reg(016) == 0,
            "E71 input transfers GOST bytes and returns terminal 012 status");

    auto e71_probe = std::make_unique<Machine>();
    e71_probe->emulate_e71(0);
    require(e71_probe->accumulator() == Word48(0004000000040000ULL),
            "E71 zero-address probe reports an available terminal");
    e71_probe->console_available() = false;
    e71_probe->emulate_e71(0);
    require(e71_probe->accumulator() == Word48(),
            "E71 zero-address probe reports an unavailable terminal");

    Machine unavailable_console;
    install_input_continue(unavailable_console);
    unavailable_console.memory(020377) = Word48();
    require(unavailable_console.p20245_begin_input_continue() == 020321,
            "20245 preserves the zero-console Э74 boundary");

    Machine output_status;
    install_input_continue(output_status);
    output_status.memory(020375) = Word48(2);
    require(output_status.p20245_begin_input_continue() == 020250
                && output_status.accumulator() == Word48(2),
            "20245 preserves the nonzero-status Э64 boundary");

    Machine status_branches;
    install_input_continue(status_branches);
    status_branches.memory(020365) = Word48(Word48::mask);
    status_branches.accumulator() =
        Word48(02000000000000000ULL);
    require(status_branches.p20253_resume_input_continue_status()
                == 020261,
            "20253 preserves the first exceptional status continuation");
    status_branches.accumulator() =
        Word48(06000000000000000ULL);
    require(status_branches.p20253_resume_input_continue_status()
                == 020715,
            "20253 preserves the second exceptional status continuation");

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

    auto captured_call = std::make_unique<Machine>();
    captured_call->memory(03265) = Word48(07600000000000000ULL);
    captured_call->memory(03266) = Word48(0000000200000000ULL);
    captured_call->memory(03271) = Word48(06500000000000000ULL);
    captured_call->memory(03272) =
        Word48(06606511500007555ULL);
    captured_call->memory(03273) = Word48(065115);
    captured_call->memory(03274) = Word48();
    captured_call->memory(065120) = Word48(065524);
    captured_call->memory(065524) = Word48(2);
    captured_call->memory(065525) = Word48(065110);
    captured_call->memory(065107) =
        Word48(00300000000000000ULL);
    captured_call->memory(065110) =
        Word48(06500000000000000ULL);
    captured_call->reg(015) = 06374;
    captured_call->reg(017) = 066007;
    require(captured_call->p03206_prepare_ordinary_call() == 07555,
            "03206 dispatches a function with one captured slot");
    require(captured_call->reg(017) == 066013,
            "03206 retains both XTS words for one captured slot");
    require(captured_call->memory(065110)
                == Word48(06500000000000000ULL),
            "03206 installs the original 03271 captured-slot constant");
    require(captured_call->remainder() == Word48(),
            "03206 reproduces the zero flag branch's RMR state");

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
    activation.memory(020142) = Word48(077777);
    activation.memory(020143) = Word48(1);
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
    require(activation.memory(020141) == Word48(0166017),
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
    arguments.memory(020142) = Word48(077777);
    arguments.memory(020143) = Word48(1);
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

    const std::pair<std::uint16_t, Word48> linked_replacement_code[] = {
        {03530, Word48(0xcc0000090000ULL)},
        {03531, Word48(0xd2400cba0b44ULL)},
        {03532, Word48(0x02000eee0758ULL)},
        {03533, Word48(0xdc874e090000ULL)},
        {03534, Word48(0xe080010c075aULL)},
    };
    const auto compare_linked_replacement = [
        &linked_replacement_code,
        &require_same_architectural_state](Word48 head) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : linked_replacement_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = head;
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(013) = 01234;
            machine->reg(014) = 02345;
            machine->reg(015) = 05034;
            machine->memory(05504) = Word48(0777);
            machine->memory(04001) =
                Word48(07200000000004010ULL);
            machine->memory(04011) = Word48();
            machine->start(03531);
        }

        for (const std::uint16_t address : {03531, 03532, 03534}) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             semantic->program_counter() != 05034 && steps != 16;
             ++steps) {
            require(semantic->step() == poplan::ExecutionStatus::running,
                    "03531 semantic path keeps running");
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != 05034
              || interpreted->right_half()) && steps != 32;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    "03531 instruction path keeps running");
        }
        require(semantic->program_counter() == 05034
                    && interpreted->program_counter() == 05034
                    && !semantic->right_half()
                    && !interpreted->right_half(),
                "03531 paths reach the saved caller");
        require_same_architectural_state(
            *semantic, *interpreted, "03531 linked replacement");
    };

    compare_linked_replacement(Word48());
    compare_linked_replacement(Word48(07200000000004000ULL));

    const std::pair<std::uint16_t, Word48> generated_status_code[] = {
        {013063, Word48(0x02200df00000ULL)},
        {013064, Word48(0x1081361b009cULL)},
        {013065, Word48(0x108139dc963aULL)},
        {013066, Word48(0x01f003108136ULL)},
        {013067, Word48(0x105108100136ULL)},
        {013070, Word48(0x1c0097090000ULL)},
        {013071, Word48(0xf980000c0000ULL)},
        {013072, Word48(0x10013f02200dULL)},
        {013073, Word48(0xf0000010812fULL)},
        {013074, Word48(0x10a12d1b80a1ULL)},
        {013075, Word48(0x090000dc9649ULL)},
        {013076, Word48(0x10812f10b108ULL)},
        {013077, Word48(0x10012f108135ULL)},
        {013100, Word48(0x1b00a610813fULL)},
        {013101, Word48(0x1b80a6108119ULL)},
        {013102, Word48(0x1c00a8090000ULL)},
    };
    const auto compare_generated_status = [
        &generated_status_code,
        &require_same_architectural_state](std::uint16_t entry,
                                           Word48 status,
                                           Word48 saved,
                                           Word48 selector) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : generated_status_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 012635;
            machine->reg(015) = 07700;
            machine->reg(017) = 04000;
            machine->memory(013245) =
                Word48(06400000000000001ULL);
            machine->memory(013312) = selector;
            machine->memory(013314) =
                Word48(06400000000000100ULL);
            machine->memory(013322) = status;
            machine->memory(013323) = status;
            machine->memory(013326) = Word48(06543);
            machine->memory(013334) = saved;
            machine->memory(013266) = Word48(05555);
            if (entry == 013071) {
                machine->reg(017) = 04001;
                machine->memory(04000) = Word48(07700);
            }
            machine->start(entry);
        }

        semantic->step();
        const std::uint16_t continuation = semantic->program_counter();
        for (const std::uint16_t address : {
                 013063, 013066, 013071, 013072, 013076}) {
            interpreted->disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted->program_counter() != continuation
              || interpreted->right_half()) && steps != 32;
             ++steps) {
            require(interpreted->step()
                        == poplan::ExecutionStatus::running,
                    "13063 instruction path keeps running");
        }
        require(interpreted->program_counter() == continuation
                    && !interpreted->right_half(),
                "13063 instruction path reaches its semantic boundary");
        require_same_architectural_state(
            *semantic, *interpreted, "13063 generated status");
        return continuation;
    };

    require(compare_generated_status(
                013063, Word48(), Word48(01234), Word48(02)) == 07700,
            "13063 empty status pops and follows its saved caller");
    require(compare_generated_status(
                013063, Word48(1), Word48(01234), Word48(02)) == 013072,
            "13063 pending status preserves the 13072 call boundary");
    require(compare_generated_status(
                013066, Word48(1), Word48(01234), Word48(02)) == 013064,
            "13066 subtracts the generated decrement and retests status");
    require(compare_generated_status(
                013071, Word48(1), Word48(01234), Word48(02)) == 07700,
            "13071 balances r17 and follows its indirect return");
    require(compare_generated_status(
                013072, Word48(), Word48(01234), Word48(02)) == 013103,
            "13072 updates a mismatched selector and selects the zero arm");
    require(compare_generated_status(
                013072, Word48(), Word48(01234),
                Word48(06400000000000100ULL)) == 013111,
            "13072 preserves the matching-selector call boundary");
    require(compare_generated_status(
                013076, Word48(1), Word48(), Word48(02)) == 013105,
            "13076 selects the alternate generated continuation");

    const std::pair<std::uint16_t, Word48> indirect_jump_code[] = {
        {013216, Word48(0x1981400c0000ULL)},
    };
    auto semantic_indirect_jump = std::make_unique<Machine>();
    auto interpreted_indirect_jump = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_indirect_jump.get(), interpreted_indirect_jump.get()}) {
        for (const auto &[address, word] : indirect_jump_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0123456701234567ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(001) = 012635;
        machine->reg(015) = 07700;
        machine->memory(013335) = Word48(05432);
        machine->start(013216);
    }
    semantic_indirect_jump->step();
    interpreted_indirect_jump->disable_translated_routine(013216);
    while (interpreted_indirect_jump->program_counter() != 05432
           || interpreted_indirect_jump->right_half()) {
        require(interpreted_indirect_jump->step()
                    == poplan::ExecutionStatus::running,
                "13216 instruction path keeps running");
    }
    require_same_architectural_state(
        *semantic_indirect_jump, *interpreted_indirect_jump,
        "13216 indirect jump");

    const auto compare_one_semantic_step = [
        &require_same_architectural_state](
            Machine &semantic, Machine &interpreted,
            std::initializer_list<std::uint16_t> disabled,
            const std::string &label, unsigned max_steps) {
        semantic.step();
        const std::uint16_t continuation = semantic.program_counter();
        for (const std::uint16_t address : disabled) {
            interpreted.disable_translated_routine(address);
        }
        for (unsigned steps = 0;
             (interpreted.program_counter() != continuation
              || interpreted.right_half()) && steps != max_steps;
             ++steps) {
            require(interpreted.step()
                        == poplan::ExecutionStatus::running,
                    label + " instruction path keeps running");
        }
        require(interpreted.program_counter() == continuation
                    && !interpreted.right_half(),
                label + " instruction path reaches its semantic boundary");
        require_same_architectural_state(semantic, interpreted, label);
        return continuation;
    };

    const std::pair<std::uint16_t, Word48> generated_builder_code[] = {
        {05007, Word48(0x02300102300dULL)},
        {05010, Word48(0x023002023000ULL)},
        {05011, Word48(0x1a0a0710807cULL)},
        {05012, Word48(0x1b0005ea0000ULL)},
        {05013, Word48(0x090000dc9f93ULL)},
        {05030, Word48(0x200004109075ULL)},
        {05031, Word48(0x10b07601e028ULL)},
        {05032, Word48(0x10007e091f7fULL)},
        {05033, Word48(0x008000dc8759ULL)},
        {05034, Word48(0xea1f7fdc9f8cULL)},
        {05035, Word48(0x0907a5008000ULL)},
        {05036, Word48(0x20000002000eULL)},
        {05037, Word48(0x090000dc8b18ULL)},
    };
    const std::initializer_list<std::uint16_t> generated_builder_entries = {
        05007, 05014, 05017, 05021, 05022,
        05026, 05030, 05034, 05035, 05040,
    };
    const auto compare_generated_builder = [
        &generated_builder_code, &generated_builder_entries,
        &compare_one_semantic_step](std::uint16_t entry,
                                    auto initialize,
                                    const std::string &label,
                                    unsigned max_steps) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : generated_builder_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 05007;
            machine->reg(002) = 03000;
            machine->reg(015) = 07000;
            machine->reg(016) = 01234;
            machine->reg(017) = 06000;
            initialize(*machine);
            machine->start(entry);
        }
        return compare_one_semantic_step(
            *semantic, *interpreted, generated_builder_entries,
            label, max_steps);
    };

    require(compare_generated_builder(
                05007,
                [](Machine &machine) {
                    machine.memory(05203) = Word48(1);
                },
                "05007 generated builder entry", 16) == 017623,
            "05007 preserves its initial classifier boundary");
    require(compare_generated_builder(
                05030,
                [](Machine &machine) {
                    machine.accumulator() =
                        Word48(07040000000000000ULL);
                    machine.memory(05174) = Word48(Word48::mask);
                    machine.memory(05175) = Word48(1);
                    machine.memory(017577) = Word48(0765432);
                },
                "05030 generated builder continuation", 16) == 03531,
            "05030 uses the literal UTC-selected scratch word");
    require(compare_generated_builder(
                05035,
                [](Machine &machine) {
                    machine.reg(002) = 03000;
                    machine.memory(03645) = Word48(3);
                },
                "05035 generated allocation continuation", 12) == 05430,
            "05035 uses the literal UTC-selected allocation count");

    const std::pair<std::uint16_t, Word48> compiler_selector_code[] = {
        {04536, Word48(0x02200d023003ULL)},
        {04537, Word48(0x0230022a095eULL)},
        {04540, Word48(0x203093dc9ee0ULL)},
        {04541, Word48(0x3a079f308002ULL)},
        {04542, Word48(0x20a0942b8006ULL)},
        {04543, Word48(0xda098c0c0731ULL)},
        {04544, Word48(0x3080052b8022ULL)},
        {04545, Word48(0x30800220a095ULL)},
        {04546, Word48(0x2b0036308002ULL)},
        {04547, Word48(0x20a0962b0035ULL)},
        {04550, Word48(0x30800220a097ULL)},
        {04551, Word48(0x2b0035308002ULL)},
        {04552, Word48(0x20a0982b0035ULL)},
        {04553, Word48(0x30800420a099ULL)},
        {04554, Word48(0x2b0022090000ULL)},
        {04555, Word48(0x308000200032ULL)},
        {04556, Word48(0x308002200033ULL)},
        {04557, Word48(0x308004200034ULL)},
        {04560, Word48(0x090000dc8937ULL)},
        {04561, Word48(0x20809a30a002ULL)},
        {04562, Word48(0x2b0073090000ULL)},
        {04563, Word48(0x303000303002ULL)},
        {04564, Word48(0x303004203032ULL)},
        {04565, Word48(0x300000208033ULL)},
        {04566, Word48(0x300002300003ULL)},
        {04567, Word48(0x208034300004ULL)},
        {04570, Word48(0x090000dc88d2ULL)},
        {04571, Word48(0xee897dea0774ULL)},
        {04572, Word48(0x301004301004ULL)},
        {04573, Word48(0x301002301000ULL)},
        {04574, Word48(0x2c0023090000ULL)},
        {04575, Word48(0x090000dc8916ULL)},
        {04576, Word48(0x308002dc9ee0ULL)},
        {04577, Word48(0xea07622c001cULL)},
        {04600, Word48(0xea0764090000ULL)},
        {04601, Word48(0x090000dc875eULL)},
        {04602, Word48(0x3a079f308002ULL)},
        {04603, Word48(0x20a09b2b003cULL)},
        {04604, Word48(0x090000dc89b5ULL)},
        {04605, Word48(0x30800220a09cULL)},
        {04606, Word48(0x2b802edc8937ULL)},
        {04607, Word48(0x090000dc8871ULL)},
        {04610, Word48(0x2c0027090000ULL)},
        {04611, Word48(0x20809d301002ULL)},
        {04612, Word48(0x021002021003ULL)},
        {04613, Word48(0x02000d0c1edfULL)},
        {04614, Word48(0x090000dc9eabULL)},
        {04615, Word48(0xf08000ee098aULL)},
        {04616, Word48(0xea09c0dc860cULL)},
        {04617, Word48()},
        {04620, Word48()},
        {04621, Word48()},
        {04622, Word48()},
        {04623, Word48(0xda09890c0731ULL)},
    };
    const std::initializer_list<std::uint16_t> compiler_selector_entries = {
        04536, 04541, 04544, 04561, 04571, 04572,
        04576, 04577, 04600, 04601, 04602, 04605,
        04607, 04610, 04611, 04612, 04614, 04615, 04623,
    };
    const auto compare_compiler_selector = [
        &compiler_selector_code, &compiler_selector_entries,
        &compare_one_semantic_step](std::uint16_t entry,
                                    auto initialize,
                                    const std::string &label,
                                    unsigned max_steps) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : compiler_selector_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(002) = 04536;
            machine->reg(003) = 03000;
            machine->reg(015) = 07000;
            machine->reg(016) = 01234;
            machine->reg(017) = 06000;
            initialize(*machine);
            machine->start(entry);
        }
        return compare_one_semantic_step(
            *semantic, *interpreted, compiler_selector_entries,
            label, max_steps);
    };

    require(compare_compiler_selector(
                04536,
                [](Machine &machine) {
                    machine.memory(04761) = Word48(0765432);
                },
                "04536 compiler selector entry", 16) == 017340,
            "04536 preserves the first shared-table boundary");
    require(compare_compiler_selector(
                04541,
                [](Machine &machine) {
                    machine.memory(03637) = Word48(07001);
                    machine.memory(03641) = Word48(0100);
                    machine.memory(03643) = Word48(0300);
                    machine.memory(03644) = Word48();
                    machine.memory(04762) = Word48(0200);
                    machine.memory(04763) = Word48(1);
                    machine.memory(04764) = Word48(2);
                    machine.memory(04765) = Word48(3);
                    machine.memory(04766) = Word48(4);
                    machine.memory(04767) = Word48(0400);
                },
                "04541 compiler selector scan", 64) == 04467,
            "04541 preserves the ordinary compiler boundary");
    require(compare_compiler_selector(
                04561,
                [](Machine &machine) {
                    machine.memory(03000) = Word48(0111);
                    machine.memory(03002) = Word48(0222);
                    machine.memory(03004) = Word48(0333);
                    machine.memory(04620) = Word48(0444);
                    machine.memory(04621) = Word48(0555);
                    machine.memory(04622) = Word48(0666);
                    machine.memory(04770) = Word48(0777);
                },
                "04561 compiler record swap", 40) == 04322,
            "04561 preserves the record-classifier boundary");
    require(compare_compiler_selector(
                04571,
                [](Machine &machine) {
                    machine.reg(016) = 0;
                    machine.reg(017) = 06004;
                    machine.memory(06000) = Word48(07000);
                    machine.memory(06001) = Word48(03100);
                    machine.memory(06002) = Word48(03200);
                    machine.memory(06003) = Word48(03300);
                },
                "04571 compiler restore", 32) == 03536,
            "04571 restores four saved fields before the compiler call");
    require(compare_compiler_selector(
                04602,
                [](Machine &machine) {
                    machine.memory(03641) = Word48(0100);
                    machine.memory(04771) = Word48(0200);
                },
                "04602 compiler selector continuation", 12) == 04665,
            "04602 preserves the repeated compiler boundary");
    require(compare_compiler_selector(
                04605,
                [](Machine &machine) {
                    machine.reg(003) = 03000;
                    machine.memory(03002) = Word48(0123);
                    machine.memory(04772) = Word48(0123);
                },
                "04605 compiler repeat continuation", 12) == 04467,
            "04605 preserves the matching-record compiler boundary");
    require(compare_compiler_selector(
                04615,
                [](Machine &machine) {
                    machine.reg(016) = 0;
                    machine.reg(017) = 06003;
                    machine.memory(06000) = Word48(07000);
                    machine.memory(06001) = Word48(03100);
                    machine.memory(06002) = Word48(03200);
                },
                "04615 compiler cleanup", 16) == 017337,
            "04615 restores the selector frame through 17337");

    const std::pair<std::uint16_t, Word48> shared_frame_code[] = {
        {05405, Word48(0x8a0b05808002ULL)},
        {05406, Word48(0xdb00000c05e8ULL)},
        {05410, Word48(0x02200102300dULL)},
        {05411, Word48(0x70323f7031a7ULL)},
        {05412, Word48(0x70023f708277ULL)},
        {05413, Word48(0x0b0b12090000ULL)},
        {05414, Word48(0x020001198000ULL)},
        {05415, Word48(0x0080000b0b15ULL)},
        {05416, Word48(0x1080010b8b0cULL)},
        {05417, Word48(0x70823f70a1a7ULL)},
        {05420, Word48(0x0b0b1270823fULL)},
        {05421, Word48(0xea1a000c060cULL)},
        {05422, Word48(0xf0800070123fULL)},
        {05423, Word48(0x02100d020001ULL)},
        {05424, Word48(0xdc0000090000ULL)},
        {05425, Word48(0x198000008001ULL)},
        {05426, Word48(0x70323fdc8a8dULL)},
        {05427, Word48(0x70023f0c0b0eULL)},
    };
    const std::initializer_list<std::uint16_t> shared_frame_entries = {
        05405, 05410, 05414, 05416, 05422, 05425, 05427,
    };
    const auto compare_shared_frame = [
        &shared_frame_code, &shared_frame_entries,
        &compare_one_semantic_step](std::uint16_t entry,
                                    auto initialize,
                                    const std::string &label,
                                    unsigned max_steps) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : shared_frame_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 03000;
            machine->reg(007) = 02000;
            machine->reg(015) = 07000;
            machine->reg(017) = 06000;
            initialize(*machine);
            machine->start(entry);
        }
        return compare_one_semantic_step(
            *semantic, *interpreted, shared_frame_entries,
            label, max_steps);
    };

    require(compare_shared_frame(
                05405,
                [](Machine &machine) {
                    machine.memory(05407) = Word48();
                },
                "05405 optional return", 8) == 07000,
            "05405 returns through its caller for a zero word");
    require(compare_shared_frame(
                05405,
                [](Machine &machine) {
                    machine.memory(05407) = Word48(1);
                },
                "05405 optional dispatch", 8) == 02750,
            "05405 preserves its nonzero evaluator transfer");
    require(compare_shared_frame(
                05410,
                [](Machine &machine) {
                    machine.memory(02647) = Word48(0777);
                    machine.memory(03077) = Word48(0666);
                    machine.memory(03167) = Word48();
                },
                "05410 empty shared frame", 32) == 07000,
            "05410 restores its frame when the chain is empty");
    require(compare_shared_frame(
                05410,
                [](Machine &machine) {
                    machine.memory(02647) = Word48(0777);
                    machine.memory(03077) = Word48(0666);
                    machine.memory(03167) = Word48(04000);
                    machine.memory(04000) = Word48(04100);
                    machine.memory(04100) = Word48();
                    machine.memory(04101) = Word48(0555);
                },
                "05410 shared-frame allocation", 40) == 05215,
            "05410 preserves its tagged-allocation boundary");

    const std::pair<std::uint16_t, Word48> masked_shift_code[] = {
        {016605, Word48(0xd24007dc9d45ULL)},
        {016606, Word48(0x308000149948ULL)},
        {016607, Word48(0x3000007c0000ULL)},
    };
    auto semantic_masked_shift = std::make_unique<Machine>();
    auto interpreted_masked_shift = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_masked_shift.get(), interpreted_masked_shift.get()}) {
        for (const auto &[address, word] : masked_shift_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0123456701234567ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(003) = 03000;
        machine->reg(007) = 01111;
        machine->reg(015) = 07000;
        machine->start(016605);
    }
    require(compare_one_semantic_step(
                *semantic_masked_shift, *interpreted_masked_shift,
                {016605, 016606}, "16605 masked shift entry", 8) == 016505,
            "16605 preserves the record-shift boundary");

    auto semantic_masked_shift_resume = std::make_unique<Machine>();
    auto interpreted_masked_shift_resume = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_masked_shift_resume.get(),
             interpreted_masked_shift_resume.get()}) {
        for (const auto &[address, word] : masked_shift_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0765432107654321ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(001) = 02000;
        machine->reg(003) = 03000;
        machine->reg(007) = 07000;
        machine->reg(015) = 016606;
        machine->memory(03000) = Word48(0765432107654321ULL);
        machine->memory(076510) = Word48(0777777777777000ULL);
        machine->start(016606);
    }
    require(compare_one_semantic_step(
                *semantic_masked_shift_resume,
                *interpreted_masked_shift_resume,
                {016605, 016606}, "16606 masked shift continuation", 8)
                == 07000,
            "16606 masks the record and returns through r7");

    const std::pair<std::uint16_t, Word48> chain_copy_code[] = {
        {05160, Word48(0x02300d023002ULL)},
        {05161, Word48(0x023003e24002ULL)},
        {05162, Word48(0xe03002dc9334ULL)},
        {05163, Word48(0xf40ffc2affffULL)},
        {05164, Word48(0x020003090000ULL)},
        {05165, Word48(0x208001020002ULL)},
        {05166, Word48(0x2e0a793a8001ULL)},
        {05167, Word48(0x208000300000ULL)},
        {05170, Word48(0x1c006e090000ULL)},
        {05171, Word48(0xf08000021003ULL)},
        {05172, Word48(0x02100202100dULL)},
        {05173, Word48(0xdc0000090000ULL)},
    };
    auto semantic_chain_entry = std::make_unique<Machine>();
    auto interpreted_chain_entry = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_chain_entry.get(), interpreted_chain_entry.get()}) {
        for (const auto &[address, word] : chain_copy_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(06606576500000000ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(001) = 05007;
        machine->reg(002) = 065765;
        machine->reg(003) = 03637;
        machine->reg(015) = 05026;
        machine->reg(016) = 03000;
        machine->reg(017) = 04000;
        machine->memory(03002) = Word48();
        machine->start(05160);
    }
    require(compare_one_semantic_step(
                *semantic_chain_entry, *interpreted_chain_entry,
                {05160, 05163}, "05160 chain frame", 16) == 011464,
            "05160 preserves the 11464 call boundary");

    auto semantic_chain_resume = std::make_unique<Machine>();
    auto interpreted_chain_resume = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_chain_resume.get(), interpreted_chain_resume.get()}) {
        for (const auto &[address, word] : chain_copy_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(07040000000065157ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(001) = 05007;
        machine->reg(002) = 03001;
        machine->reg(003) = 01234;
        machine->reg(015) = 05163;
        machine->reg(017) = 04004;
        machine->memory(04000) = Word48(01111);
        machine->memory(04001) = Word48(05026);
        machine->memory(04002) = Word48(065765);
        machine->memory(04003) = Word48(03637);
        machine->memory(03001) = Word48(03100);
        machine->memory(03100) = Word48(07200000000004567ULL);
        machine->memory(03101) = Word48();
        machine->start(05163);
    }
    require(compare_one_semantic_step(
                *semantic_chain_resume, *interpreted_chain_resume,
                {05160, 05163}, "05163 chain continuation", 32) == 05026,
            "05163 copies the chain and restores its four-word frame");

    const std::pair<std::uint16_t, Word48> table_scan_code[] = {
        {06424, Word48(0xea80028a0d24ULL)},
        {06425, Word48(0xe08000809055ULL)},
        {06426, Word48(0x80a058db8000ULL)},
        {06427, Word48(0xe2400ce08000ULL)},
        {06430, Word48(0x01e04f02000eULL)},
        {06431, Word48(0xee8d15023007ULL)},
        {06432, Word48(0x02300df00000ULL)},
        {06433, Word48(0xc24007ea0002ULL)},
        {06434, Word48(0xdc8b18090000ULL)},
        {06435, Word48(0x02200e01e031ULL)},
        {06436, Word48(0x70a000700000ULL)},
        {06437, Word48(0x8a0d24808059ULL)},
        {06440, Word48(0xe00000090280ULL)},
        {06441, Word48(0x00820be00001ULL)},
        {06442, Word48(0xf0800002100dULL)},
        {06443, Word48(0x021007dc0000ULL)},
    };
    const auto compare_table_scan = [
        &table_scan_code,
        &compare_one_semantic_step](bool allocate) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : table_scan_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(007) = 0777;
            machine->reg(015) = 07000;
            machine->reg(016) = 03000;
            machine->reg(017) = 06000;
            if (allocate) {
                machine->memory(06571) = Word48();
                machine->memory(06574) = Word48();
                machine->memory(03002) =
                    Word48(static_cast<std::uint64_t>(04000) << 15);
                machine->memory(04000) = Word48();
            } else {
                machine->memory(06571) = Word48(Word48::mask);
                machine->memory(06574) = Word48(2);
                machine->memory(03002) = Word48(1);
            }
            machine->start(06424);
        }
        return compare_one_semantic_step(
            *semantic, *interpreted, {06424, 06435},
            "06424 table scan", 48);
    };

    require(compare_table_scan(false) == 07000,
            "06424 returns the first nonmatching table word");
    require(compare_table_scan(true) == 05430,
            "06424 preserves its allocation boundary after a zero chain");

    auto semantic_scan_resume = std::make_unique<Machine>();
    auto interpreted_scan_resume = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_scan_resume.get(), interpreted_scan_resume.get()}) {
        for (const auto &[address, word] : table_scan_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0765432107654321ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(007) = 04000;
        machine->reg(010) = 01234;
        machine->reg(015) = 06435;
        machine->reg(016) = 05000;
        machine->reg(017) = 06003;
        machine->memory(06000) = Word48(0123456701234567ULL);
        machine->memory(06001) = Word48(0777);
        machine->memory(06002) = Word48(07000);
        machine->memory(04000) = Word48(0555);
        machine->memory(06575) = Word48(06600000000000000ULL);
        machine->memory(02213) = Word48(06500000000000000ULL);
        machine->start(06435);
    }
    require(compare_one_semantic_step(
                *semantic_scan_resume, *interpreted_scan_resume,
                {06424, 06435}, "06435 allocation continuation", 32)
                == 07000,
            "06435 installs the allocation and restores its frame");

    const std::pair<std::uint16_t, Word48> compiler_entry_code[] = {
        {017070, Word48(0x02200df00000ULL)},
        {017071, Word48(0x090000dc8ce3ULL)},
        {017072, Word48(0x09079f000000ULL)},
        {017073, Word48(0xf0800002000dULL)},
        {017074, Word48(0x0c2b11090000ULL)},
    };
    auto semantic_compiler_entry = std::make_unique<Machine>();
    auto interpreted_compiler_entry = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_compiler_entry.get(), interpreted_compiler_entry.get()}) {
        for (const auto &[address, word] : compiler_entry_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0123456701234567ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(015) = 07000;
        machine->reg(017) = 06000;
        machine->start(017070);
    }
    require(compare_one_semantic_step(
                *semantic_compiler_entry, *interpreted_compiler_entry,
                {017070, 017072}, "17070 compiler entry", 8) == 06343,
            "17070 preserves the 06343 call boundary");

    auto semantic_compiler_resume = std::make_unique<Machine>();
    auto interpreted_compiler_resume = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_compiler_resume.get(),
             interpreted_compiler_resume.get()}) {
        for (const auto &[address, word] : compiler_entry_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0765432107654321ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(015) = 017072;
        machine->reg(017) = 06001;
        machine->memory(06000) = Word48(07000);
        machine->start(017072);
    }
    require(compare_one_semantic_step(
                *semantic_compiler_resume, *interpreted_compiler_resume,
                {017070, 017072}, "17072 compiler continuation", 8)
                == 025421,
            "17072 stores the result and restores its saved caller");

    const std::pair<std::uint16_t, Word48> alternate_hash_wrapper_code[] = {
        {06134, Word48(0xf2400e02300dULL)},
        {06135, Word48(0xf000008a0c63ULL)},
        {06136, Word48(0xf48ffe01e061ULL)},
        {06137, Word48(0xf4bffe809113ULL)},
        {06140, Word48(0x090000dc8277ULL)},
        {06141, Word48(0xf0800002100dULL)},
        {06142, Word48(0x02200edc0000ULL)},
    };
    auto semantic_alternate_hash = std::make_unique<Machine>();
    auto interpreted_alternate_hash = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_alternate_hash.get(),
             interpreted_alternate_hash.get()}) {
        for (const auto &[address, word] : alternate_hash_wrapper_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(06400000000000100ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(010) = 01234;
        machine->reg(015) = 017424;
        machine->reg(016) = 02345;
        machine->reg(017) = 06000;
        machine->memory(06566) = Word48(Word48::mask);
        machine->start(06134);
    }
    require(compare_one_semantic_step(
                *semantic_alternate_hash, *interpreted_alternate_hash,
                {06134, 06141}, "06134 alternate hash entry", 20)
                == 01167,
            "06134 preserves the alternate 01167 call boundary");

    auto semantic_alternate_hash_resume = std::make_unique<Machine>();
    auto interpreted_alternate_hash_resume = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_alternate_hash_resume.get(),
             interpreted_alternate_hash_resume.get()}) {
        for (const auto &[address, word] : alternate_hash_wrapper_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0765432107654321ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(015) = 06141;
        machine->reg(016) = 06000;
        machine->reg(017) = 06002;
        machine->memory(06000) = Word48(06400000000000100ULL);
        machine->memory(06001) = Word48(017424);
        machine->start(06141);
    }
    require(compare_one_semantic_step(
                *semantic_alternate_hash_resume,
                *interpreted_alternate_hash_resume,
                {06134, 06141}, "06141 alternate hash continuation", 8)
                == 017424,
            "06141 restores the caller and returns the saved frame base");

    const std::pair<std::uint16_t, Word48> value_update_code[] = {
        {03702, Word48(0x02200d703277ULL)},
        {03703, Word48(0x2b0069dc8b08ULL)},
        {03704, Word48(0x70827770323fULL)},
        {03705, Word48(0x090000dc8a8dULL)},
        {03706, Word48(0x70023f090000ULL)},
        {03707, Word48(0x090000dc9edfULL)},
        {03710, Word48(0x70127702000dULL)},
        {03711, Word48(0x0c0937090000ULL)},
    };
    const auto initialize_value_update = [
        &value_update_code](Machine &machine) {
        for (const auto &[address, word] : value_update_code) {
            machine.memory(address) = word;
        }
        machine.accumulator() = Word48(0123456701234567ULL);
        machine.remainder() = Word48(07654);
        machine.alu_mode() = 025;
        machine.reg(002) = 03536;
        machine.reg(007) = 02000;
        machine.reg(015) = 04007;
        machine.reg(016) = 02345;
        machine.reg(017) = 05000;
    };

    auto semantic_value_zero = std::make_unique<Machine>();
    auto interpreted_value_zero = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_value_zero.get(), interpreted_value_zero.get()}) {
        initialize_value_update(*machine);
        machine->memory(03167) = Word48();
        machine->start(03702);
    }
    require(compare_one_semantic_step(
                *semantic_value_zero, *interpreted_value_zero,
                {03702, 03704, 03706, 03707, 03710},
                "03702 zero value", 12) == 017337,
            "03702 zero value enters the shared 17337 path");

    auto semantic_value_nonzero = std::make_unique<Machine>();
    auto interpreted_value_nonzero = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_value_nonzero.get(),
             interpreted_value_nonzero.get()}) {
        initialize_value_update(*machine);
        machine->memory(03167) = Word48(0654321);
        machine->start(03702);
    }
    require(compare_one_semantic_step(
                *semantic_value_nonzero, *interpreted_value_nonzero,
                {03702, 03704, 03706, 03707, 03710},
                "03702 nonzero value", 12) == 05410,
            "03702 nonzero value preserves the 05410 call boundary");

    auto semantic_value_transform = std::make_unique<Machine>();
    auto interpreted_value_transform = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_value_transform.get(),
             interpreted_value_transform.get()}) {
        initialize_value_update(*machine);
        machine->reg(015) = 03704;
        machine->memory(03167) = Word48(0654321);
        machine->memory(03077) = Word48(0765432);
        machine->start(03704);
    }
    require(compare_one_semantic_step(
                *semantic_value_transform, *interpreted_value_transform,
                {03702, 03704, 03706, 03707, 03710},
                "03704 transform continuation", 8) == 05215,
            "03704 preserves the 05215 call boundary");

    auto semantic_value_store = std::make_unique<Machine>();
    auto interpreted_value_store = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_value_store.get(), interpreted_value_store.get()}) {
        initialize_value_update(*machine);
        machine->accumulator() = Word48(0776655443322110ULL);
        machine->reg(015) = 03706;
        machine->start(03706);
    }
    require(compare_one_semantic_step(
                *semantic_value_store, *interpreted_value_store,
                {03702, 03704, 03706, 03707, 03710},
                "03706 store continuation", 8) == 017337,
            "03706 stores the transformed value before 17337");

    auto semantic_value_return = std::make_unique<Machine>();
    auto interpreted_value_return = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_value_return.get(), interpreted_value_return.get()}) {
        initialize_value_update(*machine);
        machine->accumulator() = Word48(0765432107654321ULL);
        machine->reg(015) = 03710;
        machine->reg(017) = 05001;
        machine->memory(05000) = Word48(04007);
        machine->start(03710);
    }
    require(compare_one_semantic_step(
                *semantic_value_return, *interpreted_value_return,
                {03702, 03704, 03706, 03707, 03710},
                "03710 value return", 8) == 04467,
            "03710 restores the saved caller before 04467");

    const std::pair<std::uint16_t, Word48> frame_code[] = {
        {017614, Word48(0x8a1f82008000ULL)},
        {017615, Word48(0xe00002e00000ULL)},
        {017616, Word48(0x02200e80b010ULL)},
        {017617, Word48(0xe00001dc0000ULL)},
    };
    auto semantic_frame = std::make_unique<Machine>();
    auto interpreted_frame = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_frame.get(), interpreted_frame.get()}) {
        for (const auto &[address, word] : frame_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0765432107654321ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(010) = 01234;
        machine->reg(015) = 04000;
        machine->reg(016) = 03000;
        machine->memory(0) = Word48(012345);
        machine->memory(017622) = Word48(0777777777777776ULL);
        machine->start(017614);
    }
    require(compare_one_semantic_step(
                *semantic_frame, *interpreted_frame, {017614},
                "17614 frame construction", 12) == 04000,
            "17614 constructs its three-word frame and returns");

    const std::pair<std::uint16_t, Word48> replacement_code[] = {
        {03506, Word48(0x8a074602000bULL)},
        {03507, Word48(0xba8b4201e028ULL)},
        {03510, Word48(0xdb0000f00001ULL)},
        {03511, Word48(0x01f003805017ULL)},
        {03512, Word48(0x0b874eba0b42ULL)},
        {03513, Word48(0xb08000f0a001ULL)},
        {03514, Word48(0xe0000002200eULL)},
        {03515, Word48(0xb00000dc0000ULL)},
        {03535, Word48(0x000011000000ULL)},
    };
    const auto compare_replacement = [
        &replacement_code, &compare_one_semantic_step](Word48 input) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : replacement_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = input;
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(015) = 04000;
            machine->reg(016) = 03000;
            machine->reg(017) = 05000;
            machine->memory(05502) = Word48(0765432107654321ULL);
            machine->start(03506);
        }
        return compare_one_semantic_step(
            *semantic, *interpreted, {03506},
            "03506 replacement", 20);
    };
    require(compare_replacement(Word48()) == 04000,
            "03506 returns immediately for a zero shifted address");
    require(compare_replacement(Word48(3)) == 03516,
            "03506 preserves its arithmetic branch to 03516");
    require(compare_replacement(Word48(077777)) == 04000,
            "03506 preserves its nonnegative replacement path");

    const std::pair<std::uint16_t, Word48> allocation_tag_code[] = {
        {05213, Word48(0x8a0a8b80300bULL)},
        {05214, Word48(0x8c0004090000ULL)},
        {05217, Word48(0x02300df00000ULL)},
        {05220, Word48(0xea0002dc8b18ULL)},
        {05226, Word48(0xe60000000000ULL)},
    };
    auto semantic_allocation_tag = std::make_unique<Machine>();
    auto interpreted_allocation_tag = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_allocation_tag.get(), interpreted_allocation_tag.get()}) {
        for (const auto &[address, word] : allocation_tag_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(0123456701234567ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(015) = 04000;
        machine->reg(016) = 03000;
        machine->reg(017) = 05000;
        machine->start(05213);
    }
    require(compare_one_semantic_step(
                *semantic_allocation_tag, *interpreted_allocation_tag,
                {05213, 05217}, "05213 allocation tag", 12) == 05430,
            "05213 preserves the two-word allocation boundary");

    const std::pair<std::uint16_t, Word48> generated_pair_code[] = {
        {017602, Word48(0x8a1f82023001ULL)},
        {017603, Word48(0xe2400102300dULL)},
        {017604, Word48(0xf43ffd003000ULL)},
        {017605, Word48(0x090000dc8a8bULL)},
        {017606, Word48(0x8a1f82103002ULL)},
        {017607, Word48(0x80b00e101002ULL)},
        {017610, Word48(0x198001000001ULL)},
        {017611, Word48(0x80900f101001ULL)},
        {017612, Word48(0x02100d021001ULL)},
        {017613, Word48(0xdc0000090000ULL)},
        {017620, Word48(1)},
        {017621, Word48(077777)},
    };
    auto semantic_generated_pair = std::make_unique<Machine>();
    auto interpreted_generated_pair = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_generated_pair.get(),
             interpreted_generated_pair.get()}) {
        for (const auto &[address, word] : generated_pair_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(065540);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(001) = 017150;
        machine->reg(015) = 017575;
        machine->reg(016) = 03000;
        machine->reg(017) = 05000;
        machine->start(017602);
    }
    require(compare_one_semantic_step(
                *semantic_generated_pair, *interpreted_generated_pair,
                {017602, 017606}, "17602 generated pair", 16) == 05213,
            "17602 preserves the 05213 allocation boundary");

    auto semantic_generated_pair_resume = std::make_unique<Machine>();
    auto interpreted_generated_pair_resume = std::make_unique<Machine>();
    for (Machine *machine : {
             semantic_generated_pair_resume.get(),
             interpreted_generated_pair_resume.get()}) {
        for (const auto &[address, word] : generated_pair_code) {
            machine->memory(address) = word;
        }
        machine->accumulator() = Word48(07140000000065772ULL);
        machine->remainder() = Word48(07654);
        machine->alu_mode() = 025;
        machine->reg(001) = 03000;
        machine->reg(015) = 05433;
        machine->reg(017) = 05003;
        machine->memory(03002) = Word48();
        machine->memory(05000) = Word48(065540);
        machine->memory(05001) = Word48(017150);
        machine->memory(05002) = Word48(017575);
        machine->start(017606);
    }
    require(compare_one_semantic_step(
                *semantic_generated_pair_resume,
                *interpreted_generated_pair_resume,
                {017602, 017606}, "17606 generated pair continuation", 20)
                == 017575,
            "17606 restores the generated-pair frame and caller");

    const std::pair<std::uint16_t, Word48> classifier_code[] = {
        {017472, Word48(0x02200d023001ULL)},
        {017473, Word48(0xf000000987a1ULL)},
        {017474, Word48(0x1a00008a1f3aULL)},
        {017475, Word48(0xe48fff00b000ULL)},
        {017476, Word48(0x8b002c148fffULL)},
        {017477, Word48(0x80902e80a034ULL)},
        {017500, Word48(0x8b0019090744ULL)},
        {017501, Word48(0x0080008b800aULL)},
        {017502, Word48(0x148fff80902fULL)},
        {017503, Word48(0x8b80128c0019ULL)},
        {017504, Word48(0xea1f6f090000ULL)},
        {017505, Word48(0xe0800102000eULL)},
        {017506, Word48(0xee1f53022001ULL)},
        {017507, Word48(0xe0a000809030ULL)},
        {017510, Word48(0x8b800b80803eULL)},
        {017511, Word48(0x8b802d148fffULL)},
        {017512, Word48(0x80a03d80902eULL)},
        {017513, Word48(0x8b802d090000ULL)},
        {017514, Word48(0xea1f73ca000fULL)},
        {017515, Word48(0x090000dc9ccbULL)},
        {017516, Word48(0xea079fdc86c1ULL)},
        {017517, Word48(0xea0443dc85f7ULL)},
        {017520, Word48(0xea1f6fdc86c1ULL)},
        {017521, Word48(0xea0377dc85f7ULL)},
        {017522, Word48(0x8a1f3a090000ULL)},
        {017523, Word48(0x090744008000ULL)},
        {017524, Word48(0x8b0024108000ULL)},
        {017525, Word48(0x090000dc9ee2ULL)},
        {017526, Word48(0x148fff809031ULL)},
        {017527, Word48(0x01e058023001ULL)},
        {017530, Word48(0xf0a000dc9ee2ULL)},
        {017531, Word48(0x8a1f3a022001ULL)},
        {017532, Word48(0x80a03d809032ULL)},
        {017533, Word48(0x80a03cea1f70ULL)},
        {017534, Word48(0x090000dc9f82ULL)},
        {017535, Word48(0x8a1f3a090000ULL)},
        {017536, Word48(0x148fff809033ULL)},
        {017537, Word48(0x80a03d140fffULL)},
        {017540, Word48(0x090744008000ULL)},
        {017541, Word48(0x8b8029148fffULL)},
        {017542, Word48(0x80d02f140fffULL)},
        {017543, Word48(0x12400ef08000ULL)},
        {017544, Word48(0x02100102000dULL)},
        {017545, Word48(0xdc0000090000ULL)},
        {017546, Word48(0xea04150c060cULL)},
        {017547, Word48(0xea04100c060cULL)},
        {017550, Word48(0x780000000000ULL)},
        {017551, Word48(0x040000000000ULL)},
        {017552, Word48(0x000000007fffULL)},
        {017553, Word48(0xf80000000000ULL)},
        {017554, Word48(0xf80000007fffULL)},
        {017555, Word48(0x07ffffffffffULL)},
        {017556, Word48(0x600000000000ULL)},
        {017557, Word48(0xd0000000000aULL)},
        {017560, Word48()},
        {017561, Word48(0x000000001f6fULL)},
        {017562, Word48()},
        {017566, Word48()},
        {017567, Word48()},
        {017570, Word48()},
    };
    const std::initializer_list<std::uint16_t> classifier_entries = {
        017472, 017504, 017505, 017514, 017516, 017517,
        017520, 017521, 017522, 017523, 017526, 017531,
        017535, 017536, 017543, 017546, 017547,
    };
    const auto compare_classifier = [
        &classifier_code, &classifier_entries,
        &compare_one_semantic_step](std::uint16_t entry,
                                    auto initialize,
                                    const std::string &label,
                                    unsigned max_steps) {
        auto semantic = std::make_unique<Machine>();
        auto interpreted = std::make_unique<Machine>();
        for (Machine *machine : {semantic.get(), interpreted.get()}) {
            for (const auto &[address, word] : classifier_code) {
                machine->memory(address) = word;
            }
            machine->accumulator() = Word48(0123456701234567ULL);
            machine->remainder() = Word48(07654);
            machine->alu_mode() = 025;
            machine->reg(001) = 04000;
            machine->reg(010) = 017472;
            machine->reg(015) = 06000;
            machine->reg(016) = 03000;
            machine->reg(017) = 05002;
            machine->memory(05000) = Word48(06000);
            machine->memory(05001) = Word48(025427);
            initialize(*machine);
            machine->start(entry);
        }
        return compare_one_semantic_step(
            *semantic, *interpreted, classifier_entries, label, max_steps);
    };

    require(compare_classifier(
                017472,
                [](Machine &machine) {
                    machine.accumulator() = Word48(03000000000000000ULL);
                    machine.reg(001) = 025427;
                    machine.reg(015) = 025445;
                    machine.reg(017) = 05000;
                    machine.memory(03641) = Word48(04000);
                    machine.memory(02777) =
                        Word48(03000000000000000ULL);
                    machine.memory(03777) =
                        Word48(03000000000000000ULL);
                    machine.memory(03504) = Word48();
                },
                "17472 direct classifier return", 64) == 025445,
            "17472 restores its frame on the direct path");

    require(compare_classifier(
                017505,
                [](Machine &machine) {
                    machine.reg(016) = 03000;
                    machine.memory(03001) = Word48(03100);
                    machine.memory(03100) = Word48(04000);
                    machine.memory(03777) = Word48();
                },
                "17505 scan", 32) == 016313,
            "17505 preserves the character-sequence boundary");
    require(compare_classifier(
                017516, [](Machine &) {}, "17516 continuation", 4)
                == 03301,
            "17516 preserves the first 03301 boundary");
    require(compare_classifier(
                017517, [](Machine &) {}, "17517 continuation", 4)
                == 02767,
            "17517 preserves the first 02767 boundary");
    require(compare_classifier(
                017520, [](Machine &) {}, "17520 continuation", 4)
                == 03301,
            "17520 preserves the second 03301 boundary");
    require(compare_classifier(
                017521, [](Machine &) {}, "17521 continuation", 4)
                == 02767,
            "17521 preserves the second 02767 boundary");
    require(compare_classifier(
                017522,
                [](Machine &machine) {
                    machine.memory(03504) = Word48(1);
                    machine.memory(04000) = Word48(0765432);
                },
                "17522 table update", 8) == 017342,
            "17522 preserves the first 17342 boundary");
    require(compare_classifier(
                017526,
                [](Machine &machine) {
                    machine.memory(03777) =
                        Word48(0765432107654321ULL);
                },
                "17526 table continuation", 12) == 017342,
            "17526 preserves the second 17342 boundary");
    require(compare_classifier(
                017531, [](Machine &) {}, "17531 allocation handoff", 12)
                == 017602,
            "17531 preserves the 17602 boundary");
    require(compare_classifier(
                017535,
                [](Machine &machine) {
                    machine.memory(03504) = Word48();
                    machine.memory(03777) =
                        Word48(03000000000000000ULL);
                },
                "17535 restoration", 24) == 06000,
            "17535 restores the classifier frame");
    require(compare_classifier(
                017546, [](Machine &) {}, "17546 diagnostic", 4)
                == 03014,
            "17546 preserves diagnostic 02025");
    require(compare_classifier(
                017547, [](Machine &) {}, "17547 diagnostic", 4)
                == 03014,
            "17547 preserves diagnostic 02020");
}
