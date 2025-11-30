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
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "LineReader.hpp"
#include "Util.hpp"

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

void
MaskGen(
    const std::string_view InputFile,
    const std::string_view OutputFile,
    const size_t MinWordLength = 0,
    const size_t MaxWordLength = std::numeric_limits<size_t>::max(),
    const __uint128_t KeyspaceMax = std::numeric_limits<__uint128_t>::max(),
    const bool Counts = false
)
{
    // Check if the input file exists, if not read from stdin
    std::istream* input = &std::cin;
    std::ifstream infile;
    if (!InputFile.empty())
    {
        infile.open(InputFile.data(), std::ios::in | std::ios::binary);
        if (!infile.is_open())
        {
            std::cerr << "Error opening input file: " << InputFile << std::endl;
            return;
        }
        input = &infile;
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

    std::unordered_map<std::string, int> word_count;

    LineReader<> reader(input);
    auto line = reader.readLine();
    std::string unhexlified;
    while (line.has_value())
    {
        if (Util::IsHexlified(*line)) {
            unhexlified = Util::UnHexlify(*line);
            line = unhexlified;
        }
        
        if (line->size() >= MinWordLength && line->size() <= MaxWordLength) {
            auto mask = Util::GetMask(*line);
            if (mask.has_value()) {
                auto keyspace = Util::CalculateKeyspaceForMask(mask.value());
                if (keyspace <= KeyspaceMax && keyspace != 0) {
                    word_count[mask.value()]++;
                }
            }
        }
        line = reader.readLine();
    }

    // Sort the map by value in descending order
    std::vector<std::pair<std::string, int>> sorted_word_count(word_count.begin(), word_count.end());
    std::sort(sorted_word_count.begin(), sorted_word_count.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    // Print the sorted map to the output file
    for (const auto& [mask, count] : sorted_word_count) {
        if (Counts) {
            *output << mask << " " << count << std::endl;
        } else {
            *output << mask << std::endl;
        }
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_file, output_file;
    size_t min_word_length = 0;
    size_t max_word_length = std::numeric_limits<size_t>::max();
    __uint128_t keyspace_max = std::numeric_limits<__uint128_t>::max();
    bool counts = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--output" || arg == "-o")
        {
            ARGCHECK();
            output_file = args[++i];
        }
        else if (arg == "--min" || arg == "-m")
        {
            ARGCHECK();
            min_word_length = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--max" || arg == "-M")
        {
            ARGCHECK();
            max_word_length = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--counts" || arg == "-c")
        {
            counts = true;
        }
        else if (arg == "--ks-max" || arg == "-K")
        {
            // Parse the keyspace of the provided argument
            ARGCHECK();
            auto arg = args[++i];
            if (Util::IsNumeric(arg))
            {
                keyspace_max = Util::ParseNumber<size_t>(arg);
            }
            else if(Util::IsMask(arg))
            {
                keyspace_max = Util::CalculateKeyspaceForMask(arg);
            }
            else
            {
                std::cerr << "Invalid keyspace argument: " << arg << std::endl;
                return 1;
            }
        }
        else if (input_file.empty() && std::filesystem::exists(arg))
        {
            input_file = arg;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_file]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --output, -o <file>  Specify the output file" << std::endl;
            std::cout << "  --min, -m <#>      Specify the minimum word length" << std::endl;
            std::cout << "  --max, -M <#>      Specify the maximum word length" << std::endl;
            std::cout << "  --ks-max, -K <#|m> Specify the maximum keyspace" << std::endl;
            std::cout << "  --counts, -c       Output the count of each mask" << std::endl;
            std::cout << "  --help, -h         Show this help message" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    MaskGen(input_file, output_file, min_word_length, max_word_length, keyspace_max, counts);
}