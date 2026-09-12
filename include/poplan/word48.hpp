#ifndef POPLAN_WORD48_HPP
#define POPLAN_WORD48_HPP

#include <cstddef>
#include <cstdint>

namespace poplan {

class Word48 {
public:
    static constexpr std::uint64_t mask = 07777777777777777ULL;

    constexpr Word48() = default;
    explicit constexpr Word48(std::uint64_t value) : value_(value & mask) {}

    template <std::size_t Size>
    explicit constexpr Word48(const char (&ascii)[Size])
        : value_(pack_ascii(ascii))
    {
        static_assert(Size >= 1, "an ASCII string has a terminator");
        static_assert(Size <= 7, "a Word48 holds at most six ASCII bytes");
    }

    constexpr std::uint64_t raw() const { return value_; }
    constexpr std::uint16_t address() const
    {
        return static_cast<std::uint16_t>(value_ & 077777U);
    }

    friend constexpr bool operator==(Word48 left, Word48 right)
    {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(Word48 left, Word48 right)
    {
        return !(left == right);
    }

private:
    template <std::size_t Size>
    static constexpr std::uint64_t pack_ascii(const char (&ascii)[Size])
    {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index + 1 < Size; ++index) {
            value = (value << 8)
                | static_cast<unsigned char>(ascii[index]);
        }
        for (std::size_t index = Size - 1; index < 6; ++index) {
            value <<= 8;
        }
        return value;
    }

    std::uint64_t value_ = 0;
};

constexpr Word48 operator&(Word48 left, Word48 right)
{
    return Word48(left.raw() & right.raw());
}

} // namespace poplan

#endif
