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

#include "UnsafeBuffer.hpp"
#include "Util.hpp"

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

void
RemoveCracked(
    const std::string_view InputHashesFile,
    const std::string_view InputCrackedFile,
    const std::string_view CrackedOutputFile,
    const std::string_view UncrackedOutputFile
)
{
    std::unordered_map<__uint128_t, std::string> crackedMap;
    // Parse the cracked file into memory
    std::ifstream crackedFile(InputCrackedFile.data(), std::ios::in);
    if (!crackedFile.is_open())
    {
        std::cerr << "Error opening input file: " << InputCrackedFile << std::endl;
        return;
    }
    std::string line;
    for (; std::getline(crackedFile, line); )
    {
        size_t pos = line.find(':');
        if (pos == std::string::npos)
            continue;
        std::string_view lineView(line);
        std::string_view hashPart = lineView.substr(0, pos);
        std::string_view passwordPart = lineView.substr(pos + 1);
        auto bytes = Util::ParseHex(hashPart, sizeof(__uint128_t));
        __uint128_t key = cracktools::LoadUint128Native(bytes);
        crackedMap[key] = std::string(passwordPart);
        if (crackedMap.size() % 100000 == 0)
        {
            std::cout << "\rLoaded " << crackedMap.size() << " cracked hashes..." << std::flush;
        }
    }
    crackedFile.close();
    std::cout << "\rLoaded total " << crackedMap.size() << " cracked hashes." << std::endl;
    
    // Read the hashes file line by line and separate cracked and uncracked
    std::ifstream hashesFile(InputHashesFile.data(), std::ios::in);
    if (!hashesFile.is_open())
    {
        std::cerr << "Error opening input file: " << InputHashesFile << std::endl;
        return;
    }

    std::ofstream crackedOutFile(CrackedOutputFile.data(), std::ios::out);
    if (!crackedOutFile.is_open())
    {
        std::cerr << "Error opening output file: " << CrackedOutputFile << std::endl;
        return;
    }
    std::ofstream uncrackedOutFile(UncrackedOutputFile.data(), std::ios::out);
    if (!uncrackedOutFile.is_open())
    {
        std::cerr << "Error opening output file: " << UncrackedOutputFile << std::endl;
        return;
    }

    size_t totalHashes = 0;
    size_t crackedCount = 0;
    size_t uncrackedCount = 0;

    for(; std::getline(hashesFile, line); )
    {
        std::string_view lineView(line);
        auto bytes = Util::ParseHex(lineView, sizeof(__uint128_t));
        if (bytes.size() != sizeof(__uint128_t))
        {
            uncrackedOutFile << line << std::endl;
            continue;
        }
        __uint128_t key = cracktools::LoadUint128Native(bytes);
        auto it = crackedMap.find(key);
        if (it != crackedMap.end())
        {
            crackedOutFile << line << ":" << it->second << std::endl;
            crackedCount++;
        }
        else
        {
            uncrackedOutFile << line << std::endl;
            uncrackedCount++;
        }
        totalHashes++;
        if (totalHashes % 100000 == 0)
        {
            std::cout << "\rH:" << totalHashes/1000 << "k C:" << crackedCount << " U:" << uncrackedCount << std::flush;
        }
    }

    hashesFile.close();
    crackedOutFile.close();
    uncrackedOutFile.close();
    std::cout << std::endl;
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    
    if (args.size() < 5)
    {
        std::cerr << "Usage: RemoveCracked <input_hashes_file> <input_cracked_file> <cracked_output_file> <uncracked_output_file>" << std::endl;
        return 1;
    }

    RemoveCracked(args[1], args[2], args[3], args[4]);
}