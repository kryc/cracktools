//
//  Random.hpp
//  CrackTools
//
//  Created by Kryc on 04/04/2026.
//  Copyright © 2026 Kryc. All rights reserved.
//

#ifndef RANDOM_HPP
#define RANDOM_HPP

#include <bit>
#include <cstdint>

constexpr uint32_t kMiniPRNGDefaultSeed = 0xd34db33f;

template <typename T>
class MiniPRNG
{
public:
    MiniPRNG(T Seed = kMiniPRNGDefaultSeed)
    {
        a = 0xf1ea2eed;
        b = c = d = Seed;
        for (int i = 0; i < 20; ++i) {
            Next();
        }
    }
    inline const T Next(void)
    {
        T e;
        if constexpr (std::is_same<T, uint64_t>::value) {
            e = a - std::rotl(b, 7);
            a = b ^ std::rotl(c, 13);
            b = c + std::rotl(d, 37);
        } else if constexpr (std::is_same<T, uint32_t>::value) {
            e = a - std::rotl(b, 32);
            a = b ^ std::rotl(c, 16);
            b = c + std::rotl(d, 11);
        }
        d = e + a;
        c = d + e;
        d = e + a;
        return d;
    }
    inline const T NextEven(void)
    {
        return Next() & ~1ULL;
    }
private:
    T a, b, c, d;
};

// Declare templates specially for 32-bit and 64-bit PRNGs
using MiniPRNG32 = MiniPRNG<uint32_t>;
using MiniPRNG64 = MiniPRNG<uint64_t>;

template <typename T>
inline const T
RandomInRange(
    MiniPRNG64& Prng,
    const T Upper
)
{
    if constexpr (std::is_same<T, uint32_t>::value ||
        std::is_same<T, uint64_t>::value) {
        return Prng.Next() % Upper;
    } else if constexpr (std::is_same<T, __uint128_t>::value) {
        __uint128_t result = Prng.Next();
        result <<= 64;
        result |= Prng.Next();
        return result % Upper;
    } else {
        static_assert(sizeof(T) == 0, "RandomInRange only supports uint32_t and uint64_t types.");
    }
}

#endif // RANDOM_HPP