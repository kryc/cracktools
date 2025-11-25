//
//  HexlifyMain.cpp
//  Hexlify
//
//  Created by Kryc on 29/08/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <string>
#include <string_view>

#include "SimdHash.hpp"

#include "UnsafeBuffer.hpp"
#include "Util.hpp"

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

void
CheckCracked(
    const std::string_view InputCrackedFile
)
{
    std::array<uint8_t, MAX_HASH_SIZE> hashBuffer;
    // Check if the input file exists, if not read from stdin
    std::istream* input = &std::cin;
    std::ifstream infile;
    if (!InputCrackedFile.empty())
    {
        infile.open(InputCrackedFile.data(), std::ios::in | std::ios::binary);
        if (!infile.is_open())
        {
            std::cerr << "Error opening input file: " << InputCrackedFile << std::endl;
            return;
        }
        input = &infile;
    }

    std::string line;
    for (; std::getline(*input, line); )
    {
        std::string_view lineView = line;
        size_t colonPos = lineView.find(':');
        if (colonPos == std::string_view::npos)
        {
            std::cerr << "Malformed cracked line: " << line << std::endl;
            continue;
        }
        std::string_view hashPart = lineView.substr(0, colonPos);
        std::string_view passwordPart = lineView.substr(colonPos + 1);

        // Detect the hash algorithm
        auto algorithm = DetectHashAlgorithmHex(hashPart.size());
        if (algorithm == HashAlgorithmUndefined)
        {
            std::cout << "Unknown hash algorithm for hash: " << hashPart << std::endl;
            continue;
        }

        auto hash = Util::ParseHex(hashPart);
        std::string password = Util::UnHexlify(passwordPart);
        // Do the hash of the password
        SimdHashSingle(
            algorithm,
            password.size(),
            reinterpret_cast<const uint8_t*>(password.data()),
            hashBuffer.data()
        );

        if (!std::equal(hash.begin(), hash.end(), hashBuffer.begin()))
        {
            std::cout << "Mismatch: " << hashPart << ":" << passwordPart << std::endl;
        }
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_cracked_file;
    
    if (args.size() > 1)
    {
        input_cracked_file = args[1];
    }

    CheckCracked(input_cracked_file);
}