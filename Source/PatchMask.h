#pragma once

#include <array>
#include <cstdint>

/** Which patch bay outputs are plugged into one input: bit n = source n. 128 sources (two 64-bit words).
    A plain 64-bit number converts to it (sources 0-63), so `setPatch (d, std::uint64_t (1) << s)` still works. */
struct PatchMask
{
    static constexpr int numBits = 128;
    std::array<std::uint64_t, 2> words {};

    constexpr PatchMask() = default;
    constexpr PatchMask (std::uint64_t low) noexcept : words { low, 0 } {}
    constexpr PatchMask (std::uint64_t low, std::uint64_t high) noexcept : words { low, high } {}

    static constexpr PatchMask bit (int s) noexcept
    {
        return s < 0 || s >= numBits ? PatchMask() : s < 64 ? PatchMask (std::uint64_t (1) << s) : PatchMask (0, std::uint64_t (1) << (s - 64));
    }
    /** Bits 0..count-1. */
    static constexpr PatchMask firstBits (int count) noexcept
    {
        const auto ones = [] (int n) { return n <= 0 ? std::uint64_t (0) : n >= 64 ? ~std::uint64_t (0) : (std::uint64_t (1) << n) - 1; };
        return PatchMask (ones (count), ones (count - 64));
    }

    constexpr bool test (int s) const noexcept { return (*this & bit (s)).any(); }
    constexpr bool any() const noexcept { return (words[0] | words[1]) != 0; }

    constexpr PatchMask operator| (const PatchMask& o) const noexcept { return { words[0] | o.words[0], words[1] | o.words[1] }; }
    constexpr PatchMask operator& (const PatchMask& o) const noexcept { return { words[0] & o.words[0], words[1] & o.words[1] }; }
    constexpr PatchMask operator^ (const PatchMask& o) const noexcept { return { words[0] ^ o.words[0], words[1] ^ o.words[1] }; }
    constexpr PatchMask operator~() const noexcept { return { ~words[0], ~words[1] }; }
    PatchMask& operator|= (const PatchMask& o) noexcept { return *this = *this | o; }
    PatchMask& operator&= (const PatchMask& o) noexcept { return *this = *this & o; }
    constexpr bool operator== (const PatchMask& o) const noexcept { return words == o.words; }
    constexpr bool operator!= (const PatchMask& o) const noexcept { return ! (*this == o); }

    /** For fingerprints. */
    constexpr std::uint64_t hash() const noexcept { return words[0] ^ (words[1] * 0x9e3779b97f4a7c15ull); }
};
