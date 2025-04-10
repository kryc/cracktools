//
//  Util.hpp
//  CrackList
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef Util_hpp
#define Util_hpp

#include <vector>
#include <span>
#include <string>
#include <string_view>
#include <cstdint>
#include <gmpxx.h>

namespace Util
{

std::vector<uint8_t>
ParseHex(
    const std::string_view HexString
);

std::string
ToHex(
    const uint8_t* Bytes,
    const size_t Length
);

std::string
ToHex(
    std::span<const uint8_t> Bytes
);

bool
IsHex(
    const std::string_view String
);

std::string
ToLower(
    const std::string_view String
);

const std::string
Hexlify(
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

}

#endif /* Util_hpp */