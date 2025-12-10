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
    const std::string_view InputCrackedFile,
    const std::string_view OutputFile = ""
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

    std::ofstream outfile;
    if (!OutputFile.empty())
    {
        outfile.open(OutputFile.data(), std::ios::out | std::ios::binary);
        if (!outfile.is_open())
        {
            std::cerr << "Error opening output file: " << OutputFile << std::endl;
            return;
        }
    }

    LineReader<> reader(input);
    std::string_view line;
    size_t count = 0;
    size_t malformed = 0;
    size_t incorrect = 0;
    std::string temp;   // Temporary buffer for unhexlifying
    while (reader.ReadLine(line))
    {
        size_t colonPos = line.find(':');
        if (colonPos == std::string_view::npos)
        {
            std::cerr << "\rMalformed cracked line: " << line << std::endl;
            malformed++;
            continue;
        }
        std::string_view hashPart = line.substr(0, colonPos);
        std::string_view passwordPart = line.substr(colonPos + 1);
        // Detect the hash algorithm
        auto algorithm = DetectHashAlgorithmHex(hashPart.size());
        if (algorithm == HashAlgorithmUndefined)
        {
            std::cerr << "\rUnknown hash algorithm for hash: " << hashPart << std::endl;
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
            std::cerr << "\rMismatch: " << hashPart << ":" << passwordPart << std::endl;
            incorrect++;
        }
        else if (outfile.is_open())
        {
            outfile << line << std::endl;
        }
        
        count++;
        if (count % 1000 == 0)
        {
            std::cout << "\r#: " << count << " M: " << malformed << " I: " << incorrect << std::flush;
        }
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_cracked, output_file;
    
    if (args.size() > 1)
    {
        input_cracked = args[1];
    }

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--output" || arg == "-o")
        {
            ARGCHECK();
            output_file = args[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_cracked]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -o, --output <file>    Output file (default: stdout)" << std::endl;
            return 0;
        }
        else if (input_cracked.empty() && std::filesystem::exists(arg))
        {
            input_cracked = arg;
        }
    }

    CheckCracked(input_cracked, output_file);
}