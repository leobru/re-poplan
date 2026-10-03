cmake_minimum_required(VERSION 3.16)

project(re_poplan LANGUAGES CXX)

option(POPLAN_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)

add_library(poplan_core
    src/console.cpp
    src/machine.cpp
    src/machine_cpu.cpp
    src/poplan.cpp
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

    add_test(NAME poplan-disk-host COMMAND poplan_core_tests --disk-host)
    add_test(NAME poplan-popmess-error-state COMMAND poplan_core_tests ${CMAKE_SOURCE_DIR}/build/poplan.bin --popmess-devices ${CMAKE_SOURCE_DIR}/tests/inputs/popmess-errors.pop2)
    add_test(NAME poplan-popmess-device-state COMMAND poplan_core_tests ${CMAKE_SOURCE_DIR}/build/poplan.bin --popmess-devices ${CMAKE_SOURCE_DIR}/tests/inputs/popmess-devices.pop2)
    add_test(NAME poplan-popmess-state COMMAND poplan_core_tests ${CMAKE_SOURCE_DIR}/build/poplan.bin --popmess ${CMAKE_SOURCE_DIR}/tests/inputs/popmess-constructors.pop2)

    add_test(NAME poplan-native-reals COMMAND poplan_core_tests --native-reals)

    add_test(NAME poplan-native-allocator
        COMMAND poplan_core_tests ${CMAKE_SOURCE_DIR}/build/poplan.bin --native-allocator)
    add_test(NAME poplan-native-storage
        COMMAND poplan_core_tests ${CMAKE_SOURCE_DIR}/build/poplan.bin --native-storage)

    add_test(NAME poplan-native-dictionary
        COMMAND poplan_core_tests ${CMAKE_SOURCE_DIR}/build/poplan.bin --native-dictionary)

    add_executable(poplan_console_tests tests/cpp/console_test.cpp)
    target_link_libraries(poplan_console_tests PRIVATE poplan_core)
    add_test(NAME poplan-console-tests COMMAND poplan_console_tests)
    add_test(
        NAME poplan-native-library
        COMMAND ${CMAKE_SOURCE_DIR}/tests/native-library.sh
                $<TARGET_FILE:poplan>
                ${CMAKE_SOURCE_DIR}/build/poplan.bin
                ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME poplan-language-coverage
        COMMAND ${CMAKE_SOURCE_DIR}/tests/language-coverage.sh
                $<TARGET_FILE:poplan>
                ${CMAKE_SOURCE_DIR}/build/poplan.bin
                ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME poplan-standard-functions-coverage
        COMMAND ${CMAKE_SOURCE_DIR}/tests/standard-functions-coverage.sh
                $<TARGET_FILE:poplan>
                ${CMAKE_SOURCE_DIR}/build/poplan.bin
                ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME poplan-real-literals
        COMMAND ${CMAKE_SOURCE_DIR}/tests/real-literals.sh
                $<TARGET_FILE:poplan>
                ${CMAKE_SOURCE_DIR}/build/poplan.bin
                ${CMAKE_SOURCE_DIR}
    )
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME poplan-popmess-devices
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/popmess-devices.py
            --executable $<TARGET_FILE:poplan>)
    add_test(NAME poplan-coverage-expansion
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/coverage-expansion.py
            --regression --executable $<TARGET_FILE:poplan>
            --output ${CMAKE_BINARY_DIR}/coverage-expansion)
endif()

if(BUILD_TESTING)
    # Compile the same entry point with a CPU object that fails on any machine
    # step or decode. The remaining core objects are shared with production.
    add_executable(poplan_native_guard src/main.cpp src/machine_cpu.cpp)
    target_compile_definitions(poplan_native_guard PRIVATE POPLAN_FORBID_MACHINE_EXECUTION)
    target_link_libraries(poplan_native_guard PRIVATE poplan_core)
    add_test(NAME poplan-native-probe
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/native-probe.py
                $<TARGET_FILE:poplan> $<TARGET_FILE:poplan_native_guard>
                ${CMAKE_SOURCE_DIR})
endif()
