#include "poplan/console.hpp"

#include "poplan/machine.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <istream>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace poplan {

namespace {

constexpr std::uint16_t input_control = 020364;
constexpr std::uint16_t output_control = 020367;
constexpr std::uint16_t input_buffer = 020400;
constexpr std::uint16_t output_buffer = 020440;
constexpr std::size_t input_capacity = (020411 - input_buffer + 1) * 6;
constexpr std::size_t output_capacity = (020455 - output_buffer + 1) * 6;
constexpr std::uint8_t gost_eof = 0377;
constexpr std::uint8_t gost_newline = 0214;
constexpr std::uint8_t gost_overline = 0115;

std::uint8_t ascii_to_gost(unsigned char byte)
{
    if (byte >= '0' && byte <= '9') {
        return static_cast<std::uint8_t>(byte - '0');
    }

    static constexpr char latin[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr std::uint8_t latin_gost[] = {
        0040, 0042, 0061, 0077, 0045, 0100, 0101, 0055, 0102,
        0103, 0052, 0104, 0054, 0105, 0056, 0060, 0106, 0107,
        0110, 0062, 0111, 0112, 0113, 0065, 0063, 0114,
    };
    if (byte >= 'a' && byte <= 'z') {
        byte = static_cast<unsigned char>(byte - 'a' + 'A');
    }
    for (std::size_t index = 0; latin[index] != '\0'; ++index) {
        if (byte == static_cast<unsigned char>(latin[index])) {
            return latin_gost[index];
        }
    }

    switch (byte) {
    case ' ': return 0017;
    case '+': return 0012;
    case '-': return 0013;
    case '/': return 0014;
    case ',': return 0015;
    case '.': return 0016;
    case '@': return 0021;
    case '(': return 0022;
    case ')': return 0023;
    case '=': return 0025;
    case ';': return 0026;
    case '[': return 0027;
    case ']': return 0030;
    case '*': return 0031;
    case '`': return 0032;
    case '\'': return 0033;
    case '#': return 0034;
    case '<': return 0035;
    case '>': return 0036;
    case ':': return 0037;
    case '^': return gost_overline;
    case '&': return 0121;
    case '~': return 0123;
    case '%': return 0126;
    case '|': return 0130;
    case '_': return 0132;
    case '!': return 0133;
    case '"': return 0134;
    case '?': return 0136;
    default: return 0017;
    }
}

char gost_to_ascii(std::uint8_t byte)
{
    if (byte <= 0011) {
        return static_cast<char>('0' + byte);
    }

    static constexpr char latin[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr std::uint8_t latin_gost[] = {
        0040, 0042, 0061, 0077, 0045, 0100, 0101, 0055, 0102,
        0103, 0052, 0104, 0054, 0105, 0056, 0060, 0106, 0107,
        0110, 0062, 0111, 0112, 0113, 0065, 0063, 0114,
    };
    for (std::size_t index = 0; latin[index] != '\0'; ++index) {
        if (byte == latin_gost[index]) {
            return latin[index];
        }
    }

    switch (byte) {
    case 0012: return '+';
    case 0013: return '-';
    case 0014: return '/';
    case 0015: return ',';
    case 0016: return '.';
    case 0017: return ' ';
    case 0021: return '@';
    case 0022: return '(';
    case 0023: return ')';
    case 0025: return '=';
    case 0026: return ';';
    case 0027: return '[';
    case 0030: return ']';
    case 0031: return '*';
    case 0032: return '`';
    case 0033: return '\'';
    case 0034: return '#';
    case 0035: return '<';
    case 0036: return '>';
    case 0037: return ':';
    case 0115: return '^';
    case 0121: return '&';
    case 0123: return '~';
    case 0126: return '%';
    case 0130: return '|';
    case 0132: return '_';
    case 0133: return '!';
    case 0134: return '"';
    case 0136: return '?';
    default: return '?';
    }
}

std::uint32_t gost_to_unicode(std::uint8_t byte)
{
    static constexpr std::array<std::uint32_t, 0140> table = {{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', '+', '-', '/', ',', '.', ' ',
        0x23e8, 0x2191, '(', ')', 0x00d7, '=', ';', '[',
        ']', '*', 0x2018, 0x2019, 0x2260, '<', '>', ':',
        0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
        0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
        0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
        0x0428, 0x0429, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 'D',
        'F', 'G', 'I', 'J', 'L', 'N', 'Q', 'R',
        'S', 'U', 'V', 'W', 'Z', 0x203e, 0x2a7d, 0x2a7e,
        0x2228, 0x2227, 0x2283, 0x00ac, 0x00f7, 0x2261, '%', 0x25c7,
        '|', 0x2015, '_', '!', '"', 0x042a, 0x00b0, 0x2032,
    }};
    return byte < table.size() ? table[byte] : 0xfffd;
}

void write_utf8(std::ostream &output, std::uint32_t codepoint)
{
    if (codepoint <= 0x7f) {
        output.put(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.put(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.put(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        output.put(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.put(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.put(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

void store_bytes(Machine &machine, std::uint16_t address,
                 const std::vector<std::uint8_t> &bytes,
                 std::size_t capacity)
{
    for (std::size_t word = 0; word < capacity / 6; ++word) {
        std::uint64_t packed = 0;
        for (std::size_t byte = 0; byte < 6; ++byte) {
            const std::size_t index = word * 6 + byte;
            packed <<= 8;
            if (index < bytes.size()) {
                packed |= bytes[index];
            }
        }
        machine.memory(static_cast<std::uint16_t>(address + word)) =
            Word48(packed);
    }
}

std::uint8_t load_byte(const Machine &machine, std::uint16_t address,
                       std::size_t index)
{
    const Word48 word = machine.memory(
        static_cast<std::uint16_t>(address + index / 6));
    const unsigned shift = static_cast<unsigned>(5 - index % 6) * 8;
    return static_cast<std::uint8_t>((word.raw() >> shift) & 0377);
}

void flush_console_output(Machine &machine, std::ostream &output,
                          bool unicode = false,
                          bool terminate_record = false)
{
    bool ended_with_newline = false;
    for (const std::uint8_t byte : machine.console_output()) {
        if (byte == gost_newline || byte == 0175) {
            output.put('\n');
            ended_with_newline = true;
        } else if (unicode) {
            write_utf8(output, gost_to_unicode(byte));
            ended_with_newline = false;
        } else {
            output.put(gost_to_ascii(byte));
            ended_with_newline = false;
        }
    }
    if (terminate_record && !ended_with_newline) {
        output.put('\n');
    }
    machine.clear_console_output();
    output.flush();
}

void emit(Machine &machine, std::ostream &output,
          std::vector<std::uint8_t> bytes)
{
    if (bytes.size() >= output_capacity) {
        bytes.resize(output_capacity - 1);
    }
    bytes.push_back(gost_eof);
    store_bytes(machine, output_buffer, bytes, output_capacity);
    machine.emulate_e71(output_control);
    flush_console_output(machine, output);
}

} // namespace

int run_io_shell(Machine &machine, std::istream &input,
                 std::ostream &output)
{
    machine.reg(010) = 020170;
    machine.memory(input_control) = Word48(04034021041120221ULL);
    machine.memory(output_control) = Word48(04020025041120265ULL);

    std::string line;
    for (;;) {
        emit(machine, output, {0037});
        if (!std::getline(input, line)) {
            return 0;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::vector<std::uint8_t> encoded;
        encoded.reserve(line.size());
        for (const unsigned char byte : line) {
            encoded.push_back(ascii_to_gost(byte));
        }
        if (encoded.size() >= input_capacity) {
            encoded.resize(input_capacity - 1);
        }
        machine.queue_console_input(std::move(encoded));
        machine.emulate_e71(input_control);

        std::vector<std::uint8_t> transferred;
        for (std::size_t index = 0; index < input_capacity; ++index) {
            const std::uint8_t byte = load_byte(machine, input_buffer, index);
            if (byte == gost_eof) {
                break;
            }
            transferred.push_back(byte);
        }
        if (transferred.size() == 1 && transferred.front() == gost_overline) {
            return 0;
        }
        transferred.push_back(gost_newline);
        emit(machine, output, std::move(transferred));
    }
}

int run_image_shell(Machine &machine, std::istream &input,
                    std::ostream &output,
                    std::uint64_t instruction_limit)
{
    machine.boot_static_image();
    const bool trace_cpu = std::getenv("POPLAN_CPU_TRACE") != nullptr;
    const bool trace_routines =
        std::getenv("POPLAN_ROUTINE_TRACE") != nullptr;
    if (std::getenv("POPLAN_INTERPRET_ONLY") != nullptr) {
        machine.set_translated_routines_enabled(false);
    }
    bool prompt_pending = false;
    std::string line;
    while (machine.instruction_count() < instruction_limit) {
        if (trace_cpu) {
            std::clog << std::oct << std::setfill('0') << std::setw(5)
                      << machine.program_counter()
                      << (machine.right_half() ? 'R' : 'L')
                      << " acc=" << std::setw(16)
                      << machine.accumulator().raw()
                      << " r15=" << std::setw(5) << machine.reg(015)
                      << '\n';
        }
        const std::uint16_t entry = machine.program_counter();
        const std::uint64_t routine_count =
            machine.translated_routine_count();
        const ExecutionStatus status = machine.step();
        if (trace_routines
            && machine.translated_routine_count() != routine_count) {
            std::clog << "ROUTINE " << std::oct << std::setfill('0')
                      << std::setw(5) << entry << " -> "
                      << std::setw(5) << machine.program_counter() << '\n';
        }
        if (!machine.console_output().empty()) {
            if (prompt_pending) {
                output.put('\n');
                prompt_pending = false;
            }
            flush_console_output(machine, output, true, true);
        }
        if (status == ExecutionStatus::halted) {
            return 0;
        }
        if (status != ExecutionStatus::input_required) {
            continue;
        }

        if (trace_cpu) {
            std::clog << "INPUT_REQUIRED pc=" << std::oct
                      << machine.program_counter() << '\n';
        }

        output.put(':');
        output.flush();
        prompt_pending = true;
        if (!std::getline(input, line)) {
            return 0;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::vector<std::uint8_t> encoded;
        encoded.reserve(line.size());
        for (const unsigned char byte : line) {
            encoded.push_back(ascii_to_gost(byte));
        }
        machine.queue_console_input(std::move(encoded));
    }
    std::ostringstream message;
    message << "POPLAN instruction limit exceeded at " << std::oct
            << machine.program_counter();
    throw MachineError(message.str());
}

} // namespace poplan
