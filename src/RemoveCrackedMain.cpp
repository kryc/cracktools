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

inline static uint64_t
GetSignatureInlineFast(
    const std::string_view HashString
)
{
    uint64_t Signature = 0;
    for (size_t i = 0; i < sizeof(Signature) * 2; i++)
    {
        uint8_t value;
        char c = HashString[i];
        if (c <= '9') {
            value = c - '0';
        }
        else if (c <= 'f')
        {
            value = c - 'a' + 10;
        }
        else
        {
            value = c - 'A' + 10;
        }
        Signature = (Signature << 4) | (value & 0xF);
    }
    return Signature;
}

void
RemoveCracked(
    const std::string_view InputHashesFile,
    const std::string_view InputCrackedFile,
    const std::string_view CrackedOutputFile,
    const std::string_view UncrackedOutputFile
)
{
    std::unordered_map<uint64_t, std::string_view> crackedMap;
    
    auto mapping = cracktools::MmapFileSpan<const uint8_t>(
        InputCrackedFile.data(),
        PROT_READ,
        MAP_SHARED,
        false
    );
    if (!mapping.has_value())
    {
        std::cerr << "Error memory-mapping input file: " << InputCrackedFile << std::endl;
        return;
    }
    auto [span, fd] = mapping.value();
    // Reserve some space in the map
    // Sha1 hash size is 40 characters, plus average length pwd is ~8, plus colon and newline
    crackedMap.reserve(span.size() / (40 + 8 + 2)); 

    // Convert the span to a string view
    std::string_view fileView = cracktools::AsStringView(span);
    size_t line_start = 0;
    for (;;)
    {
        size_t line_end = fileView.find('\n', line_start);
        // If the line end is npos, we have one last line or are done
        if (line_end == std::string_view::npos)
        {
            line_end = fileView.size();
        }
        if (line_end == line_start)
        {
            break;
        }
        std::string_view lineView = fileView.substr(line_start, line_end - line_start);
        size_t pos = lineView.find(':');
        if (pos == std::string_view::npos)
        {
            std::cerr << "Invalid line in cracked file (no colon): " << lineView << std::endl;
            continue;
        }

        std::string_view hashPart = lineView.substr(0, pos);
        std::string_view passwordPart = lineView.substr(pos + 1);
        uint64_t key = GetSignatureInlineFast(hashPart);
        crackedMap[key] = passwordPart;

        if (crackedMap.size() % 100000 == 0)
        {
            std::cout << "\rLoaded " << crackedMap.size()/1000 << "k cracked hashes." << std::flush;
        }
        
        // Update line start for next iteration
        line_start = line_end + 1;
        if (line_start >= fileView.size())
        {
            break;
        }
    }

    std::cout << "Loaded total " << crackedMap.size() << " cracked hashes." << std::endl;
    
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

    std::string line;
    for(; std::getline(hashesFile, line); )
    {
        std::string_view lineView(line);
        if (lineView.size() != sizeof(uint64_t) * 2)
        {
            uncrackedOutFile << line << std::endl;
            uncrackedCount++;
            continue;
        }
        uint64_t key = GetSignatureInlineFast(lineView);
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
    
    std::cout << std::endl;

    hashesFile.close();
    crackedOutFile.close();
    uncrackedOutFile.close();
    cracktools::UnmapFileSpan<const uint8_t>(span, fd);
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