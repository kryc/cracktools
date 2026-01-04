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
    Upper,
    Both
};

constexpr size_t MAX = std::numeric_limits<size_t>::max();

std::vector<uint8_t>
ParseHex(
    const std::string_view HexString,
    const size_t MaxBytes = MAX
);

const uint16_t
ParseHexUint16(
    const std::string_view HexString
);

const uint32_t
ParseHexUint32(
    const std::string_view HexString
);

const uint64_t
ParseHexUint64(
	const std::string_view HexString
);

const __uint128_t
ParseHexUint128(
	const std::string_view HexString
);

const size_t
ParseHexInplace(
	std::span<char> HexString,
	const size_t MaxBytes = MAX
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

const bool
IsHex(
    const char Character
);

const bool
IsHex(
    const std::string_view String
);

const bool
IsBase64(
    const char Character
);

const bool
IsBase64(
    const std::string_view String
);

std::string
ToLower(
    const std::string_view String
);

const bool
CouldBeHexlified(
    const std::string_view Value
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
IsPrintableUTF8Hexlified(
    const std::string_view Value
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
IsPrintableASCIIHexlified(
    const std::string_view Value
);

const bool
IsNumeric(
    const std::string_view Value
);

const bool
IsMask(
    const std::string_view Value
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

const bool
MaybeUnHexlifyInPlace(
	std::string& Value
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
#pragma clang unsafe_buffer_usage begin
    auto success = std::from_chars(
        String.data(),
        String.data() + String.size(),
        result
    );
#pragma clang unsafe_buffer_usage end
    CHECKA(success.ec == std::errc(), "Failed to parse number from string");
    return result;
}

const __uint128_t
CalculateKeyspaceForMask(
    const std::string_view Mask
);

std::optional<std::string>
GetMask(
    const std::string_view Word
);

const bool
IsValidEmail(
    const std::string_view Email
);

const bool
IsLikelyValidEmail(
	const std::string_view Email
);

const bool
IsValidUsername(
    const std::string_view Username
);

const bool
IsValidUsernameOrEmail(
    const std::string_view Input
);

const bool
IsValidIPv4(
    const std::string_view IPv4
);

const bool
IsAlphanumeric(
    const std::string_view Value,
    const Case CharCase = Case::Both
);

const bool
IsNumericString(
    const std::string_view Value
);

const bool
IsLikelyDateString(
    const std::string_view Value
);

const bool
CouldBeHashHex(
    const std::string_view Value
);

const bool
IsRadix64(
	const char Character
);

const bool
IsRadix64(
	const std::string_view Value
);

const bool
CouldBeCryptHash(
    const std::string_view Value,
    const bool Permissive = false
);

} // namespace Util

#endif /* Util_hpp */