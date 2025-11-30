//
//  Util.cpp
//  CrackList
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <UnsafeBuffer.hpp>
#include "Util.hpp"

namespace Util
{

static const int8_t HEX_LOOKUP[256] = {
	/* 0x00-0x0f*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x10-0x1f*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x20-0x2f*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x30-0x3f*/  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
	/* 0x40-0x4f*/ -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x50-0x5f*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x60-0x6f*/ -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x70-0x7f*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x80-0x8f*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0x90-0x9f*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0xa0-0xaf*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0xb0-0xbf*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0xc0-0xcf*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0xd0-0xdf*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0xe0-0xef*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	/* 0xf0-0xff*/ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

std::vector<uint8_t>
ParseHex(
	const std::string_view HexString,
	const size_t MaxBytes
)
{
	std::vector<uint8_t> vec;

	vec.reserve((HexString.size() + 1) / 2);
	
	for (size_t i = 0; i < HexString.size() && vec.size() < MaxBytes; i+=2)
	{
		uint8_t upper = HEX_LOOKUP[(uint8_t)HexString[i]];
		uint8_t lower = HEX_LOOKUP[(uint8_t)HexString[i + 1]];
		if (upper == (uint8_t)-1 || lower == (uint8_t)-1)
		{
			// DCHECKA(upper != (uint8_t)-1 && lower != (uint8_t)-1, "Invalid hex character encountered");
			continue;
		}
		uint8_t next = (upper << 4) | lower;
		vec.push_back(next);
	}
	
	return vec;
}

const size_t
ParseHexInplace(
	std::span<char> HexString,
	const size_t MaxBytes
)
{
	CHECKA(HexString.size() % 2 == 0, "Hex string must have even length");
	size_t length = 0;
	
	for (size_t i = 0; i < HexString.size() && i/2 < MaxBytes; i+=2)
	{
		uint8_t upper = HEX_LOOKUP[(uint8_t)HexString[i]];
		uint8_t lower = HEX_LOOKUP[(uint8_t)HexString[i + 1]];
		if (upper == (uint8_t)-1 || lower == (uint8_t)-1)
		{
			// DCHECKA(upper != (uint8_t)-1 && lower != (uint8_t)-1, "Invalid hex character encountered");
			continue;
		}
		uint8_t next = (upper << 4) | lower;
		HexString[length++] = static_cast<char>(next);
	}
	return length;
}

std::string
ToHex(
	std::span<const uint8_t> Bytes,
	const Case OutputCase
)
{
	std::ostringstream oss;
	oss << std::hex << std::setfill('0');
	if (OutputCase == Case::Upper)
	{
		oss << std::uppercase;
	}

	for (const auto& byte : Bytes)
	{
		oss << std::setw(2) << static_cast<int>(byte);
	}

	return oss.str();
}

std::string
ToHex(
	const uint8_t* Bytes,
	const size_t Length,
	const Case OutputCase
)
{
	return ToHex(
		cracktools::UnsafeSpan<const uint8_t>(Bytes, Length),
		OutputCase
	);
}

std::string
ToHex(
    std::string_view Value,
	const Case OutputCase
)
{
	return ToHex(
		cracktools::AsBytes(Value),
		OutputCase
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
	return Value.size() >= 6 &&
		   (Value.size() & 1) == 0 &&
		   cracktools::LoadUint32LittleEndian(Value) == 'XEH$' &&
		   Value[4] == '[' &&
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
			(codepoint >= 0xE000 && codepoint <= 0xF8FF) ||     // BMP PUA
			(codepoint >= 0xF0000 && codepoint <= 0xFFFFD) ||   // SPUA-A
			(codepoint >= 0x100000 && codepoint <= 0x10FFFD) || // SPUA-B
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
IsNumeric(
    const std::string_view Value
)
{
	for (char c : Value)
	{
		if (!isdigit(c))
		{
			return false;
		}
	}
	return true;
}

const bool
IsMask(
    const std::string_view Value
)
{
	for (char c : Value)
	{
		if (c != '?' && c != 'l' && c != 'u' && c != 'd')
		{
			return false;
		}
	}
	return true;
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
		return "$HEX[" + Util::ToHex(Value, Case::Lower) + "]";
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

const bool
MaybeUnHexlifyInPlace(
	std::string& Value
)
{
	if (IsHexlified(Value))
	{
		auto vec = ParseHex(Value.substr(5, Value.size() - 6));
		Value = cracktools::AsStringView(vec);
		return true;
	}
	return false;
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

const std::tuple<std::string, std::vector<std::string_view>>
ReadLines(
	std::ifstream& File,
	const size_t BlockSize
)
{
	std::vector<std::string_view> lines;
	std::string buffer;
	size_t pos = 0;
	size_t last_pos = 0;

	while (true)
	{
		// Read a block of data from the file
		buffer.resize(BlockSize);
		File.read(&buffer[0], BlockSize);
		size_t bytes_read = File.gcount();
		buffer.resize(bytes_read);

		// If we reached the end of the file, break the loop
		if (bytes_read == 0)
		{
			break;
		}

		// Find all newline characters in the block
		while (pos < bytes_read)
		{
			if (buffer[pos] == '\n')
			{
				// Extract the line from the buffer
				std::string_view line(&buffer[0] + last_pos, pos - last_pos);
				lines.push_back(line);

				// Move past the newline character
				pos++;
				last_pos = pos;
			}
			else
			{
				pos++;
			}
		}

		// If we reached the end of the buffer, break the loop
		if (pos == bytes_read)
		{
			break;
		}
	}

	// If there's any remaining text in the buffer, add it as a line
	if (last_pos < buffer.size())
	{
		std::string_view line(&buffer[0] + last_pos, buffer.size() - last_pos);
		lines.push_back(line);
	}

	return {File.fail() ? std::string("fail") : std::string(), lines};
}

const __uint128_t
CalculateKeyspaceForMask(
    const std::string_view Mask
)
{
	__uint128_t keyspace = 1;
	__uint128_t temp;
	for (char c : Mask)
	{
		switch (c)
		{
			case 'u':
			case 'l':
				temp = keyspace * 26;
				if (temp < keyspace)
				{
					return 0; // Overflow detected
				}
				keyspace = temp;
				break;
			case 'd':
				temp = keyspace * 10;
				if (temp < keyspace)
				{
					return 0; // Overflow detected
				}
				keyspace = temp;
				break;
			case 's':
				temp = keyspace * 32;
				if (temp < keyspace)
				{
					return 0; // Overflow detected
				}
				keyspace = temp;
				break;
			case '?':
			default:
				// Ignore other characters
				break;
		}
	}
	return keyspace;
}

std::array<char, 256> MASK_MAP = {
    /* 0x00 - 0x0f */  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0x10 - 0x1f */  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0x20 - 0x2f */ 's', 's', 's', 's', 's', 's', 's', 's', 's', 's', 's', 's', 's', 's', 's', 's',
    /* 0x30 - 0x3f */ 'd', 'd', 'd', 'd', 'd', 'd', 'd', 'd', 'd', 'd', 's', 's', 's', 's', 's', 's',
    /* 0x40 - 0x4f */ 's', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u',
    /* 0x50 - 0x5f */ 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 'u', 's', 's', 's', 's', 's',
    /* 0x60 - 0x6f */ 's', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l',
    /* 0x70 - 0x7f */ 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 'l', 's', 's', 's', 's', -1,
    /* 0x80 - 0x8f */ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0x90 - 0x9f */ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0xa0 - 0xaf */ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0xb0 - 0xbf */ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0xc0 - 0xcf */ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0xd0 - 0xdf */ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0xe0 - 0xef */ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    /* 0xf0 - 0xff */ -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1
};

std::optional<std::string>
GetMask(
    const std::string_view Word
)
{
    std::string mask;
    for (auto c : Word)
    {
        // Check if it is a valid ASCII character
        if (c < ' ' || c > '~')
        {
            return std::nullopt;
        }
        // Look up the mask character in the map
        const auto maskchar = MASK_MAP[c];
        if (maskchar == -1)
        {
            return std::nullopt;
        }
        mask.push_back('?');
        mask.push_back(maskchar);
    }
    return mask;
}

}