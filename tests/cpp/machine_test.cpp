#include "poplan/machine.hpp"

#include <ctime>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

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
}
