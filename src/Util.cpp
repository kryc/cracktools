//
//  Util.cpp
//  CrackList
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <array>
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

const std::string
Hexlify(
    const std::string_view Value
)
{
    bool needshex = false;
    for (auto c : Value)
    {
        if (c < ' ' || c > '~' || c == ':')
        {
            needshex = true;
            break;
        }
    }
    if (!needshex)
    {
        return std::string(Value);
    }
    return "$HEX[" + Util::ToHex((const uint8_t*)Value.data(), Value.size()) + "]";
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