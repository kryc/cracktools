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
    std::ifstream crackedFile(InputCrackedFile.data(), std::ios::in);
    if (!crackedFile.is_open())
    {
        std::cerr << "Error opening input file: " << InputCrackedFile << std::endl;
        return;
    }   
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
    size_t missingCount = 0;

    // Loop through the hashes file and see if it matches the next line
    // of the cracked file
    std::string crackedLine;
    std::string hashLine;

    // Prime the cracked file
    bool haveCracked = static_cast<bool>(std::getline(crackedFile, crackedLine));
    std::string_view crackedLineView;
    std::string_view crackedHashPart;
    std::string_view crackedPasswordPart;

    if (haveCracked)
    {
        crackedLineView = crackedLine;
        size_t crackedPos = crackedLineView.find(':');
        if (crackedPos == std::string_view::npos)
        {
            std::cerr << "Malformed cracked line: " << crackedLine << std::endl;
            return;
        }
        crackedHashPart = crackedLineView.substr(0, crackedPos);
        crackedPasswordPart = crackedLineView.substr(crackedPos + 1);
    }

    while (std::getline(hashesFile, hashLine))
    {
        totalHashes++;
        std::string_view hashLineView = hashLine;

        // If there are no cracked lines left, everything else is uncracked
        if (!haveCracked)
        {
            uncrackedOutFile << hashLineView << std::endl;
            uncrackedCount++;
            continue;
        }

        int cmp = hashLineView.compare(crackedHashPart); // all hashes same length

        if (cmp > 0)
        {
            // Advance the cracked file until we find a match or surpass the hash
            while (cmp > 0 && haveCracked)
            {
                missingCount++;
                if (std::getline(crackedFile, crackedLine))
                {
                    crackedLineView = crackedLine;
                    size_t crackedPos = crackedLineView.find(':');
                    if (crackedPos == std::string_view::npos)
                    {
                        std::cerr << "Malformed cracked line: " << crackedLine << std::endl;
                        return;
                    }
                    crackedHashPart = crackedLineView.substr(0, crackedPos);
                    crackedPasswordPart = crackedLineView.substr(crackedPos + 1);
                    cmp = hashLineView.compare(crackedHashPart);
                }
                else
                {
                    haveCracked = false;
                }
            }

            if (!haveCracked)
            {
                // No more cracked lines; current and remaining hashes are uncracked
                uncrackedOutFile << hashLineView << std::endl;
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
            if (std::getline(crackedFile, crackedLine))
            {
                crackedLineView = crackedLine;
                size_t crackedPos = crackedLineView.find(':');
                if (crackedPos == std::string_view::npos)
                {
                    std::cerr << "Malformed cracked line: " << crackedLine << std::endl;
                    return;
                }
                crackedHashPart = crackedLineView.substr(0, crackedPos);
                crackedPasswordPart = crackedLineView.substr(crackedPos + 1);
            }
            else
            {
                haveCracked = false;
            }
        }
        else if (cmp < 0)
        {
            // Uncracked
            uncrackedOutFile << hashLineView << std::endl;
            uncrackedCount++;
        }
        else
        {
            std::cerr << "Logic error in hash comparison" << std::endl;
        }

        if (totalHashes % 1000 == 0)
        {
            std::cout << "\rH: " << totalHashes
                    << " C: " << crackedCount
                    << " U: " << uncrackedCount
                    << " M: " << missingCount
                    << std::flush;
        }
    }

    std::cout << std::endl;
    
    crackedFile.close();
    hashesFile.close();
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