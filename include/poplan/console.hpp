#ifndef POPLAN_CONSOLE_HPP
#define POPLAN_CONSOLE_HPP

#include <iosfwd>
#include <cstdint>
#include <string_view>
#include <vector>

namespace poplan {

class Machine;

// Encode host UTF-8 text as the one-byte GOST-10859 representation used by
// POPLAN terminal buffers.
std::vector<std::uint8_t> encode_gost_text(std::string_view text);

// Host adapter for the translated Э71 device. This is an I/O demonstration
// shell, not yet the complete POP-2 evaluator: non-terminating input lines are
// echoed through the original input and output control-word formats.
int run_io_shell(Machine &machine, std::istream &input,
                 std::ostream &output);

// Execute an already loaded static POPLAN image, satisfying Э71 input calls
// from the host stream and flushing translated GOST output to the host stream.
int run_image_shell(Machine &machine, std::istream &input,
                    std::ostream &output,
                    std::uint64_t instruction_limit = 100000000);

} // namespace poplan

#endif
