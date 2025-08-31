//
//  Util.hpp
//  CrackList
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef Util_hpp
#define Util_hpp

#include <charconv>
#include <vector>
#include <span>
#include <string>
#include <string_view>
#include <cstdint>
#include <gmpxx.h>

#include "Check.hpp"
#include "UnsafeBuffer.hpp"

namespace Util
{

enum class Case
{
    Lower,
    Upper
};

std::vector<uint8_t>
ParseHex(
    const std::string_view HexString
);

std::string
ToHex(
    const uint8_t* Bytes,
    const size_t Length,
    const Case OutputCase = Case::Lower
);

std::string
ToHex(
    std::span<const uint8_t> Bytes,
    const Case OutputCase = Case::Lower
);

std::string
ToHex(
    std::string_view Value,
    const Case OutputCase = Case::Lower
);

bool
IsHex(
    const std::string_view String
);

std::string
ToLower(
    const std::string_view String
);

const bool
IsHexlified(
    const std::string_view Value
);

const bool
IsPrintableUTF8(
	std::span<const uint8_t> Value
);

const bool
IsPrintableUTF8(
	std::string_view Value
);

const bool
IsPrintableASCII(
	std::span<const uint8_t> Value
);

const bool
IsPrintableASCII(
	std::string_view Value
);

const bool
NeedsHexlify(
    const std::string_view Value,
    const char Separator = ':',
    const bool AlwaysAscii = false
);

const std::string
Hexlify(
    const std::string_view Value
);

const std::string
UnHexlify(
    const std::string_view Value
);

double
NumFactor(
    const double Value,
    std::string& HumanFactor
);

mpz_class
NumFactor(
    const mpz_class Value,
    std::string& HumanFactor
);

const double
SizeFactor(
    const double SizeBytes,
    std::string& HumanFactor
);

template <typename T>
const T
ParseNumber(
    const std::string_view String
)
{
    T result{};
    auto success = std::from_chars(
        String.data(),
        String.data() + String.size(),
        result
    );
    CHECKA(success.ec == std::errc(), "Failed to parse number from string");
    return result;
}

} // namespace Util

#endif /* Util_hpp */