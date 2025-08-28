//
//  Util.cpp
//  CrackList
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <array>
#include <cctype>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <cstdint>
#include <vector>

#include <UnsafeBuffer.hpp>
#include "Util.hpp"

namespace Util
{

std::vector<uint8_t>
ParseHex(
	const std::string_view HexString
)
{
	std::vector<uint8_t> vec;
	bool doingUpper = true;
	uint8_t next = 0;
	
	for (size_t i = 0; i < HexString.size(); i ++)
	{
		if (HexString[i] >= 0x30 && HexString[i] <= 0x39)
		{
			next |= HexString[i] - 0x30;
		}
		else if (HexString[i] >= 0x41 && HexString[i] <= 0x46)
		{
			next |= HexString[i] - 0x41 + 10;
		}
		else if (HexString[i] >= 0x61 && HexString[i] <= 0x66)
		{
			next |= HexString[i] - 0x61 + 10;
		}
		
		if ((HexString.size() % 2 == 1 && i == 0) ||
			doingUpper == false)
		{
			vec.push_back(next);
			next = 0;
			doingUpper = true;
		}
		else if (doingUpper)
		{
			next <<= 4;
			doingUpper = false;
		}
	}
	
	return vec;
}

std::string
ToHex(
	std::span<const uint8_t> Bytes
)
{
	std::ostringstream oss;
	oss << std::hex << std::setfill('0');

	for (const auto& byte : Bytes)
	{
		oss << std::setw(2) << static_cast<int>(byte);
	}

	return oss.str();
}

std::string
ToHex(
	const uint8_t* Bytes,
	const size_t Length
)
{
	return ToHex(
		cracktools::UnsafeSpan<const uint8_t>(Bytes, Length)
	);
}

std::string
ToHex(
    std::string_view Value
)
{
	return ToHex(
		cracktools::AsBytes(Value)
	);
}

bool
IsHex(
	const std::string_view String
)
{
	// Detect an odd length string
	if (String.size() & 1)
	{
		return false;
	}

	for (char c : String)
	{
		if (!isxdigit(c))
		{
			return false;
		}
	}
	
	return true;
}

std::string
ToLower(
    const std::string_view String
)
{
	std::string result;

	for (char c : String)
	{
		if (c >= 'A' && c <= 'Z')
		{
			result.push_back(c + ('a' - 'A'));
		}
		else
		{
			result.push_back(c);
		}
	}

	return result;
}

const bool
IsHexlified(
    const std::string_view Value
)
{
	return Value.size() > 6 &&
		   Value.size() % 2 == 0 &&
		   Value.starts_with("$HEX[") &&
		   Value.ends_with("]") &&
		   IsHex(Value.substr(5, Value.size() - 6));
}

const bool
IsPrintableUTF8(
	std::span<const uint8_t> Value
)
{
    size_t i = 0;
    while (i < Value.size()) {
        uint8_t byte = Value[i];
        uint32_t codepoint = 0;
        size_t remaining = Value.size() - i;

        if (byte <= 0x7F) {
            // ASCII
            codepoint = byte;
            i += 1;
        } else if ((byte & 0xE0) == 0xC0 && remaining >= 2) {
            // 2-byte sequence
            if ((Value[i + 1] & 0xC0) != 0x80) return false;
            codepoint = ((byte & 0x1F) << 6) | (Value[i + 1] & 0x3F);
            if (codepoint < 0x80) return false; // Overlong encoding
            i += 2;
        } else if ((byte & 0xF0) == 0xE0 && remaining >= 3) {
            // 3-byte sequence
            if ((Value[i + 1] & 0xC0) != 0x80 || (Value[i + 2] & 0xC0) != 0x80) return false;
            codepoint = ((byte & 0x0F) << 12) |
                        ((Value[i + 1] & 0x3F) << 6) |
                        (Value[i + 2] & 0x3F);
            if (codepoint < 0x800) return false; // Overlong encoding
            i += 3;
        } else if ((byte & 0xF8) == 0xF0 && remaining >= 4) {
            // 4-byte sequence
            if ((Value[i + 1] & 0xC0) != 0x80 ||
                (Value[i + 2] & 0xC0) != 0x80 ||
                (Value[i + 3] & 0xC0) != 0x80) return false;
            codepoint = ((byte & 0x07) << 18) |
                        ((Value[i + 1] & 0x3F) << 12) |
                        ((Value[i + 2] & 0x3F) << 6) |
                        (Value[i + 3] & 0x3F);
            if (codepoint < 0x10000 || codepoint > 0x10FFFF) return false; // Overlong or out of range
            i += 4;
        } else {
            return false; // Invalid leading byte
        }

        // Check if codepoint is printable
        if ((codepoint >= 0x00 && codepoint <= 0x1F) || // C0 controls
            (codepoint == 0x7F) ||                      // DEL
            (codepoint >= 0x80 && codepoint <= 0x9F) || // C1 controls
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) || // Surrogates
            (codepoint == 0xFFFE || codepoint == 0xFFFF)) { // Noncharacters
            return false;
        }
    }

    return true;
}

const bool
IsPrintableUTF8(
	std::string_view Value
)
{
	return IsPrintableUTF8(
		cracktools::AsBytes(Value)
	);
}

const bool
IsPrintableASCII(
	std::span<const uint8_t> Value
)
{
	for (auto c : Value)
	{
		if (c < 0x20 || c > 0x7E)
		{
			return false;
		}
	}
	return true;
}

const bool
IsPrintableASCII(
	std::string_view Value
)
{
	return IsPrintableASCII(
		cracktools::AsBytes(Value)
	);
}

const bool
NeedsHexlify(
    const std::string_view Value,
	const char Separator,
	const bool AlwaysAscii
)
{
	// Check if the string is printable
	if (AlwaysAscii && !IsPrintableASCII(Value))
	{
		return true;
	}
	else if (!AlwaysAscii && !IsPrintableUTF8(Value))
	{
		return true;
	}

	// Check if the string contains the separator
	if (Value.find(Separator) != std::string_view::npos)
	{
		return true;
	}

	// Check if it looks hexlified.
	// This check is weaker than IsHexlified
	if (Value.starts_with("$HEX["))
	{
		return true;
	}

	// Check if it ends in whitespace
	if (!Value.empty() && isspace(Value.back()))
	{
		return true;
	}

	return false;
}

const std::string
Hexlify(
    const std::string_view Value
)
{
    if (NeedsHexlify(Value))
    {
		return "$HEX[" + Util::ToHex(Value) + "]";
	}
	return std::string(Value);
}

const std::string
UnHexlify(
    const std::string_view Value
)
{
	if (IsHexlified(Value))
	{
		auto vec = ParseHex(Value.substr(5, Value.size() - 6));
		auto span = std::span<const uint8_t>(vec);
		return cracktools::AsString(span);
	}
	return std::string(Value);
}

double
NumFactor(
    const double Value,
    std::string& HumanFactor
)
{
	double value = Value;
	if (value > 1000000000.f)
    {
        value /= 1000000000.f;
		HumanFactor = "b";
        return value;
    }
    else if (value > 1000000.f)
    {
        value /= 1000000.f;
		HumanFactor = "m";
        return value;
    }
    else if (value > 1000.f)
    {
        value /= 1000.f;
		HumanFactor = "k";
        return value;
    }
	HumanFactor = "";
    return value;
}

mpz_class
NumFactor(
    const mpz_class Value,
    std::string& HumanFactor
)
{
	mpz_class value = Value;
	if (value > 1000000000)
    {
        value /= 1000000000;
		HumanFactor = "b";
        return value;
    }
    else if (value > 1000000)
    {
        value /= 1000000;
		HumanFactor = "m";
        return value;
    }
    else if (value > 1000)
    {
        value /= 1000;
		HumanFactor = "k";
        return value;
    }
	HumanFactor = "";
    return value;
}

const double
SizeFactor(
    const double SizeBytes,
    std::string& HumanFactor
)
{
	double value = SizeBytes;
	double tb = 1024 * 1024;
	tb *= 1024 * 1024;
	if (value > tb)
    {
        value /= (1024 * 1024);
		value /= (1024 * 1024);
		HumanFactor = "TB";
        return value;
    }
	else if (value > 1024 * 1024 * 1024)
    {
        value /= (1024 * 1024 * 1024);
		HumanFactor = "GB";
        return value;
    }
    else if (value > 1024 * 1024)
    {
        value /= (1024 * 1024);
		HumanFactor = "MB";
        return value;
    }
    else if (value > 1024)
    {
        value /= 1024;
		HumanFactor = "KB";
        return value;
    }
	HumanFactor = "";
    return value;
}

}