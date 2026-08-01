#ifndef POPLAN_WORD48_HPP
#define POPLAN_WORD48_HPP

#include <cstdint>

namespace poplan {

class Word48 {
public:
    static constexpr std::uint64_t mask = 07777777777777777ULL;

    constexpr Word48() = default;
    explicit constexpr Word48(std::uint64_t value) : value_(value & mask) {}

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
    std::uint64_t value_ = 0;
};

constexpr Word48 operator&(Word48 left, Word48 right)
{
    return Word48(left.raw() & right.raw());
}

} // namespace poplan

#endif
