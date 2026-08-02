#include "poplan/console.hpp"
#include "poplan/machine.hpp"

#include <cstdlib>
#include <iostream>
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
}
