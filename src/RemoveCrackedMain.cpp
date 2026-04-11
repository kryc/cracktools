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

#include "LineReader.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"

#define ARGCHECK() \
    if (argc <= i + 1) \
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
    LineReader<> crackedReader(InputCrackedFile);
    LineReader<> hashesReader(InputHashesFile);

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

    // Get the number of lines in the input hashes file
    size_t totalHashes = 0;
    if (!InputHashesFile.empty() && std::filesystem::exists(InputHashesFile))
    {
        std::cerr << "Counting input lines..." << std::flush;
        LineCounter<> lineCounter(InputHashesFile);
        totalHashes = lineCounter.CountLines();
    }

    size_t parsedHashes = 0;
    size_t crackedCount = 0;
    size_t uncrackedCount = 0;
    size_t missingCount = 0;

    // Prime the cracked file
    std::string_view crackedLine;
    bool haveCracked = crackedReader.ReadLine(crackedLine);
    std::string_view crackedHashPart;
    std::string_view crackedPasswordPart;

    if (haveCracked)
    {
        size_t crackedPos = crackedLine.find(':');
        if (crackedPos == std::string_view::npos)
        {
            std::cerr << "Malformed cracked line: " << crackedLine << std::endl;
            return;
        }
        crackedHashPart = crackedLine.substr(0, crackedPos);
        crackedPasswordPart = crackedLine.substr(crackedPos + 1);
    }

    std::string_view hashLine;
    while (hashesReader.ReadLine(hashLine))
    {
        parsedHashes++;

        // If there are no cracked lines left, everything else is uncracked
        if (!haveCracked)
        {
            uncrackedOutFile << hashLine << std::endl;
            uncrackedCount++;
            continue;
        }

        // The line may contain a hash and a count in the format hash:count
        size_t colonPos = hashLine.find(':');
        if (colonPos != std::string_view::npos)
        {
            hashLine = hashLine.substr(0, colonPos);
        }

        int cmp = hashLine.compare(crackedHashPart); // all hashes same length

        if (cmp > 0)
        {
            // Advance cracked lines until we reach current hash or run out
            while (cmp > 0 && haveCracked)
            {
                // current cracked hash is missing from input hashes
                missingCount++;

                if (!crackedReader.ReadLine(crackedLine))
                {
                    haveCracked = false;
                    break;
                }

                size_t crackedPos = crackedLine.find(':');
                if (crackedPos == std::string_view::npos)
                {
                    std::cerr << "Malformed cracked line: " << crackedLine << std::endl;
                    return;
                }
                crackedHashPart = crackedLine.substr(0, crackedPos);
                crackedPasswordPart = crackedLine.substr(crackedPos + 1);
                cmp = hashLine.compare(crackedHashPart);
            }

            if (!haveCracked)
            {
                // No more cracked lines; current and remaining hashes are uncracked
                uncrackedOutFile << hashLine << std::endl;
                uncrackedCount++;
                continue;
            }
        }

        if (cmp == 0)
        {
            // Cracked
            crackedOutFile << crackedHashPart << ":" << crackedPasswordPart << std::endl;
            crackedCount++;

            // Move to next cracked line for next iteration
            if (crackedReader.ReadLine(crackedLine))
            {
                size_t crackedPos = crackedLine.find(':');
                if (crackedPos == std::string_view::npos)
                {
                    std::cerr << "Malformed cracked line: " << crackedLine << std::endl;
                    return;
                }
                crackedHashPart = crackedLine.substr(0, crackedPos);
                crackedPasswordPart = crackedLine.substr(crackedPos + 1);
            }
            else
            {
                haveCracked = false;
            }
        }
        else if (cmp < 0)
        {
            // Uncracked
            uncrackedOutFile << hashLine << std::endl;
            uncrackedCount++;
        }
        else
        {
            std::cerr << "Logic error in hash comparison" << std::endl;
        }

        if (parsedHashes % 1000 == 0)
        {
            std::cout << "\rH: " << parsedHashes
                    << "/" << totalHashes
                    << "(" << (parsedHashes * 100 / totalHashes) << "%)"
                    << " C: " << crackedCount
                    << " U: " << uncrackedCount
                    << " M: " << missingCount
                    << std::flush;
        }
    }

    std::cout << std::endl;
    
    crackedOutFile.close();
    uncrackedOutFile.close();

    std::cout << "Total hashes processed: " << totalHashes << std::endl;
    std::cout << "Cracked hashes: " << crackedCount << std::endl;
    std::cout << "Uncracked hashes: " << uncrackedCount << std::endl;
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