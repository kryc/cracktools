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

#include "LineReader.hpp"
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

    LineReader<> reader(input);
    auto line = reader.ReadLine();
    size_t count = 0;
    size_t malformed = 0;
    size_t incorrect = 0;
    std::string temp;   // Temporary buffer for unhexlifying
    while (line.has_value())
    {
        size_t colonPos = line->find(':');
        if (colonPos == std::string_view::npos)
        {
            std::cerr << "Malformed cracked line: " << *line << std::endl;
            malformed++;
            line = reader.ReadLine();
            continue;
        }
        std::string_view hashPart = line->substr(0, colonPos);
        std::string_view passwordPart = line->substr(colonPos + 1);
        // Detect the hash algorithm
        auto algorithm = DetectHashAlgorithmHex(hashPart.size());
        if (algorithm == HashAlgorithmUndefined)
        {
            std::cout << "Unknown hash algorithm for hash: " << hashPart << std::endl;
            line = reader.ReadLine();
            continue;
        }

        auto hash = Util::ParseHex(hashPart);
        if (Util::IsHexlified(passwordPart))
        {
             temp = Util::UnHexlify(passwordPart);
             passwordPart = temp;
        }

        // Do the hash of the password
        SimdHashSingle(
            algorithm,
            passwordPart.size(),
            reinterpret_cast<const uint8_t*>(passwordPart.data()),
            hashBuffer.data()
        );

        if (!std::equal(hash.begin(), hash.end(), hashBuffer.begin()))
        {
            std::cout << "Mismatch: " << hashPart << ":" << passwordPart << std::endl;
            incorrect++;
        }
        
        count++;
        if (count % 1000 == 0)
        {
            std::cout << "\r#: " << count << " M: " << malformed << " I: " << incorrect << std::flush;
        }
        
        line = reader.ReadLine();
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