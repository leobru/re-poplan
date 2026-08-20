#include "poplan/console.hpp"
#include "poplan/machine.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
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
    poplan::Machine machine;
    std::istringstream input("PR(1+2);\n^\n");
    std::ostringstream output;

    require(poplan::run_io_shell(machine, input, output) == 0,
            "the I/O shell exits successfully");
    require(output.str() == ":PR(1+2);\n:",
            "the I/O shell prompts, transfers, echoes, and exits on ^");
    require(machine.reg(016) == 0,
            "E71 clears the function-key register");
    require(machine.accumulator() ==
                poplan::Word48(01000000200000012ULL),
            "the last E71 input returns terminal 012 status");

    const auto verify_cyrillic = [](const std::string &alphabet) {
        auto cyrillic = std::make_unique<poplan::Machine>();
        std::istringstream cyrillic_input(alphabet + "\n");
        std::ostringstream cyrillic_output;
        require(poplan::run_io_shell(
                    *cyrillic, cyrillic_input, cyrillic_output) == 0,
                "the I/O shell accepts UTF-8 Cyrillic input");
        for (std::size_t letter = 0; letter < 32; ++letter) {
            const std::uint8_t expected = letter < 26
                ? static_cast<std::uint8_t>(0040 + letter)
                : letter == 26
                    ? static_cast<std::uint8_t>(0135)
                    : static_cast<std::uint8_t>(0072 + letter - 27);
            const poplan::Word48 word = cyrillic->memory(
                static_cast<std::uint16_t>(020400 + letter / 6));
            const unsigned shift =
                static_cast<unsigned>(5 - letter % 6) * 8;
            const auto actual = static_cast<std::uint8_t>(
                (word.raw() >> shift) & 0377);
            require(actual == expected,
                    "Cyrillic letters map in GOST alphabetical order");
        }
        require(!cyrillic_output.str().empty()
                    && cyrillic_output.str().front() == ':'
                    && cyrillic_output.str().back() == ':',
                "UTF-8 letters transfer as one GOST byte each");
    };
    verify_cyrillic("АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ");
    verify_cyrillic("абвгдежзийклмнопрстуфхцчшщъыьэюя");
}
