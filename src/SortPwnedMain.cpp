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
SortPwned(
    const std::string_view InputFile,
    const std::string_view OutputFile,
    const bool NoCount = false,
    const size_t Min = 1,
    const size_t Max = std::numeric_limits<size_t>::max()
)
{
    if (InputFile.empty())
    {
        std::cerr << "No input file specified." << std::endl;
        return;
    }
    if (!std::filesystem::exists(InputFile))
    {
        std::cerr << "Input file does not exist: " << InputFile << std::endl;
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

    // Get the number of lines in the input file
    size_t totalLines = 0;
    if (!NoCount && !InputFile.empty() && std::filesystem::exists(InputFile))
    {
        std::cerr << "Counting input lines..." << std::flush;
        LineCounter<> lineCounter(InputFile);
        totalLines = lineCounter.CountLines();
    }

    std::unordered_map<std::size_t, std::vector<std::string_view>> length_buckets;

    MmapLineReader reader(InputFile);
    std::string_view line;
    size_t count = 0;
    size_t small = 0;
    size_t large = 0;
    while (reader.ReadLine(line))
    {
        count++;
        
        // Split line into hash and count parts
        size_t delimiterPos = line.find(':');
        if (delimiterPos == std::string_view::npos) {
            std::cerr << "Malformed line (missing ':'): " << line << std::endl;
            continue;
        }
        // std::string_view hashPart = line.substr(0, delimiterPos);
        std::string_view countPart = line.substr(delimiterPos + 1);

        size_t hashcount = Util::ParseNumber<size_t>(countPart);
        if (hashcount < Min) {
            small++;
            continue;
        }
        else if (hashcount > Max) {
            large++;
            continue;
        }

        // Add it to the appropriate length bucket
        length_buckets[hashcount].emplace_back(std::move(line));
        
        if (count % 1000 == 0 && !OutputFile.empty()) {
            if (totalLines > 0)
            {
                std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) <: " << small << " >: " << large << " " << std::flush;
            }
            else
            {
                std::cerr << "\r#: " << count << " <: " << small << " >: " << large << " " << std::flush;
            }
        }
    }

    if (!OutputFile.empty()) {
        if (totalLines > 0)
        {
            std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) <: " << small << " >: " << large << " " << std::endl;
        }
        else
        {
            std::cerr << "\r#: " << count << " <: " << small << " >: " << large << " " << std::endl;
        }
    }

    std::cerr << "Sorting " << count - small - large << " entries..." << std::endl;

    // Get a vector of all counts and sort it in descending order
    std::vector<std::size_t> counts;
    for (const auto& [count, vec] : length_buckets) {
        counts.push_back(count);
    }
    std::sort(counts.begin(), counts.end(), std::greater<std::size_t>());

    std::cerr << "Writing sorted entries to output..." << std::endl;
    
    // Write sorted entries to output
    for (const auto& count : counts) {
        auto& vec = length_buckets[count];
        for (const auto& line : vec) {
            *output << line << std::endl;
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
    bool nocount = false;
    size_t min = 1;
    size_t max = std::numeric_limits<size_t>::max();

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
        else if (arg == "--min" || arg == "-m")
        {
            ARGCHECK();
            min = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--max" || arg == "-M")
        {
            ARGCHECK();
            max = Util::ParseNumber<size_t>(args[++i]);
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
            std::cout << "  --min, -m <number>   Specify the minimum count to include" << std::endl;
            std::cout << "  --max, -M <number>   Specify the maximum count to include" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    SortPwned(input_file, output_file, nocount, min, max);
}