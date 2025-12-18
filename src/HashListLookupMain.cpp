//
//  HashListLookupMain.cpp
//  HashListLookup
//
//  Created by Kryc on 18/12/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "LineReader.hpp"
#include "Util.hpp"
#include "UnsafeBuffer.hpp"

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

void
HashListLookup(
    const std::string_view InputHashes,
    const std::string_view InputCracked,
    const std::string_view OutputFile,
    const bool NoCount = false
)
{    
    if (InputHashes.empty())
    {
        std::cerr << "No input hashes file specified." << std::endl;
        return;
    }
    if (!std::filesystem::exists(InputHashes))
    {
        std::cerr << "Input hashes file does not exist: " << InputHashes << std::endl;
        return;
    }

    std::ifstream hashes_infile;
    hashes_infile.open(InputHashes.data(), std::ios::in | std::ios::binary);
    if (!hashes_infile.is_open())
    {
        std::cerr << "Error opening input file: " << InputHashes << std::endl;
        return;
    }
    
    if (InputCracked.empty())
    {
        std::cerr << "No input cracked file specified." << std::endl;
        return;
    }
    if (!std::filesystem::exists(InputCracked))
    {
        std::cerr << "Input cracked file does not exist: " << InputCracked << std::endl;
        return;
    }

    // Open the output file if specified, otherwise write to stdout
    std::ostream* output = &std::cout;
    std::ofstream outfile;
    if (!OutputFile.empty())
    {
        outfile.open(OutputFile.data(), std::ios::out | std::ios::binary);
        if (!outfile.is_open())
        {
            std::cerr << "Error opening output file: " << OutputFile << std::endl;
            return;
        }
        output = &outfile;
    }

    // Get the number of lines in the input files
    size_t totalLinesHashes = 0;
    size_t totalLinesCracked = 0;
    if (!NoCount)
    {
        std::cerr << "\rCounting input lines..." << std::flush;
        LineCounter<> lineCounterHashes(InputHashes);
        totalLinesHashes = lineCounterHashes.CountLines();

        std::cerr << "\rCounting cracked lines..." << std::flush;
        LineCounter<> lineCounterCracked(InputCracked);
        totalLinesCracked = lineCounterCracked.CountLines();
    }

    std::cerr << "\rLoading cracked hashes into memory..." << std::endl;

    std::unordered_map<uint16_t, std::vector<std::string_view>> hashLookup;
    hashLookup.reserve(65536);

    MmapLineReader reader(InputCracked);
    std::string_view line;
    size_t count = 0;
    while (reader.ReadLine(line))
    {
        count++;
        
        // Split line into hash and count parts
        size_t delimiterPos = line.find(':');
        if (delimiterPos == std::string_view::npos) {
            std::cerr << "Malformed line (missing ':'): " << line << std::endl;
            continue;
        }
        std::string_view hashPart = line.substr(0, delimiterPos);
        // std::string_view passwordPart = line.substr(delimiterPos + 1);
        const uint16_t hashKey = Util::ParseHexUint16(hashPart.substr(0, 4));

        hashLookup[hashKey].push_back(std::move(line));
        
        if (count % 1000 == 0 && !OutputFile.empty()) {
            if (totalLinesCracked > 0)
            {
                std::cerr << "\r#: " << count << "/" << totalLinesCracked << "(" << (count * 100 / totalLinesCracked) << "%)" << std::flush;
            }
            else
            {
                std::cerr << "\r#: " << count << " " << std::flush;
            }
        }
    }

    if (!OutputFile.empty()) {
        if (totalLinesCracked > 0)
        {
            std::cerr << "\r#: " << count << "/" << totalLinesCracked << "(" << (count * 100 / totalLinesCracked) << "%)" << std::endl;
        }
        else
        {
            std::cerr << "\r#: " << count << std::endl;
        }
    }

    // Now process the hashes and output results
    std::cerr << "Performing hash lookups..." << std::endl;

    LineReader<> hashes_reader(&hashes_infile);
    count = 0;
    size_t found = 0;
    size_t missing = 0;
    while (hashes_reader.ReadLine(line))
    {
        count++;

        // If there is a ':' in the line, use only the part before it
        size_t colonPos = line.find(':');
        if (colonPos != std::string_view::npos)
        {
            line = line.substr(0, colonPos);
        }

        const uint16_t hashKey = Util::ParseHexUint16(line.substr(0, 4));

        auto it = hashLookup.find(hashKey);
        if (it != hashLookup.end()) {
            // Use binary search to find the hash in the vector
            auto& vec = it->second;
            ssize_t low = 0;
            ssize_t high = vec.size() - 1;
            bool foundHash = false;
            while (low <= high) {
                const ssize_t mid = (low + high) / 2;
                const std::string_view midHash = vec[mid].substr(0, vec[mid].find(':'));
                const int cmp = line.compare(midHash);
                if (cmp == 0) {
                    // Found the hash
                    *output << vec[mid] << std::endl;
                    found++;
                    foundHash = true;
                    break;
                } else if (cmp < 0) {
                    high = mid - 1;
                } else {
                    low = mid + 1;
                }
            }
            if (!foundHash) {
                missing++;
            }
        } else {
            missing++;
        }
        
        if (count % 1000 == 0 && !OutputFile.empty()) {
            if (totalLinesHashes > 0)
            {
                std::cerr << "\r#: " << count << "/" << totalLinesHashes << "(" << (count * 100 / totalLinesHashes) << "%) ✓: " << found << " ✗: " << missing << std::flush;
            }
            else
            {
                std::cerr << "\r#: " << count << " ✓: " << found << " ✗: " << missing << std::flush;
            }
        }
    }

    if (!OutputFile.empty()) {
        if (totalLinesHashes > 0)
        {
            std::cerr << "\r#: " << count << "/" << totalLinesHashes << "(" << (count * 100 / totalLinesHashes) << "%) ✓: " << found << " ✗: " << missing << std::endl;
        }
        else
        {
            std::cerr << "\r#: " << count << " ✓: " << found << " ✗: " << missing << std::endl;
        }
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_hashes, input_cracked, output_file;
    bool nocount = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--output" || arg == "-o")
        {
            ARGCHECK();
            output_file = args[++i];
        }
        else if (arg == "--nocount" || arg == "-n")
        {
            nocount = true;
        }
        else if (input_hashes.empty() && std::filesystem::exists(arg))
        {
            input_hashes = arg;
        }
        else if (input_cracked.empty() && std::filesystem::exists(arg))
        {
            input_cracked = arg;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_hashes] [input_cracked]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --output, -o <file>  Specify the output file" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    HashListLookup(input_hashes, input_cracked, output_file, nocount);
}