cmake_minimum_required(VERSION 3.16)

project(re_poplan LANGUAGES CXX)

option(POPLAN_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

add_library(poplan_core
    src/console.cpp
    src/machine.cpp
    src/machine_cpu.cpp
)
target_include_directories(poplan_core PUBLIC include)
target_compile_features(poplan_core PUBLIC cxx_std_17)
target_compile_options(poplan_core PRIVATE -Wall -Wextra -Wpedantic)

if(POPLAN_ENABLE_SANITIZERS)
    target_compile_options(poplan_core PUBLIC
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(poplan_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(poplan src/main.cpp)
target_link_libraries(poplan PRIVATE poplan_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(poplan_core_tests tests/cpp/machine_test.cpp)
    target_link_libraries(poplan_core_tests PRIVATE poplan_core)
    add_test(
        NAME poplan-core-tests
        COMMAND poplan_core_tests ${CMAKE_SOURCE_DIR}/build/poplan.bin
    )

    add_executable(poplan_console_tests tests/cpp/console_test.cpp)
    target_link_libraries(poplan_console_tests PRIVATE poplan_core)
    add_test(NAME poplan-console-tests COMMAND poplan_console_tests)
endif()
