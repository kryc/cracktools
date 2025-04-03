//
//  SmallString.hpp
//  RainbowCrack-
//
//  Created by Kryc on 23/06/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef SmallString_hpp
#define SmallString_hpp

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "UnsafeBuffer.hpp"

// Defined on a uint32/uint64 boundary minus
// one so that we can store the length in a
// single byte
constexpr size_t kSmallStringMaxLength = 31u;

// A small string class with no initializers
// and all public members to allow
// for faster initialization
class SmallString
{
public:
    template <typename T>
    void Set(std::span<T> NewValue) {
        static_assert(std::is_same_v<T, char> || std::is_same_v<T, const char> ||
                std::is_same_v<T, uint8_t> || std::is_same_v<T, const uint8_t>,
                "T must be char, const char, uint8_t, or const uint8_t");
        std::memcpy((char*)m_Value.data(), NewValue.data(), NewValue.size());
        SetLength(NewValue.size());
    };
    void Set(std::string_view Value) { Set(cracktools::UnsafeSpan<const char>(Value)); };
    void SetLength(const uint8_t Length) { m_Length = Length; };
    std::span<const char> Get(void) const { return std::span<const char>(m_Value).subspan(0, m_Length); };
    const std::string_view GetString(void) const { return std::string_view(Get()); };
private:
    std::array<char, kSmallStringMaxLength> m_Value;
    uint8_t m_Length;
} ;

#endif /* SmallString_hpp */
