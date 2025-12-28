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
Unhexlify(
    const std::string_view InputFile,
    const std::string_view OutputFile,
    const size_t Min = 1,
    const size_t Max = std::numeric_limits<size_t>::max(),
    const bool Rehexlify = false
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

    // Get the number of lines in the input file
    size_t totalLines = 0;
    if (!InputFile.empty() && std::filesystem::exists(InputFile))
    {
        std::cerr << "Counting input lines..." << std::flush;
        LineCounter<> lineCounter(InputFile);
        totalLines = lineCounter.CountLines();
    }

    LineReader<> reader(input);
    std::string_view line;
    std::string temp;
    size_t count = 0;
    size_t small = 0;
    size_t large = 0;
    while (reader.ReadLine(line))
    {
        count++;
        const bool is_hexlified = Util::IsHexlified(line);
        const size_t line_size = is_hexlified? (line.size() - 6) / 2 : line.size();
        
        if (line_size < Min) {
            small++;
            continue;
        }
        else if (line_size > Max) {
            large++;
            continue;
        }

        if (is_hexlified) {
            temp = Util::UnHexlify(line);
            line = temp;
        }
    
        *output << (Rehexlify ? Util::Hexlify(line) : line) << std::endl;
        
        if (count % 1000 == 0 && !OutputFile.empty()) {
            if (totalLines > 0)
            {
                std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) <: " << small << " >: " << large << std::flush;
            }
            else
            {
                std::cerr << "\r#: " << count << " <: " << small << " >: " << large << std::flush;
            }
        }
    }

    if (!OutputFile.empty()) {
        if (totalLines > 0)
        {
            std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) <: " << small << " >: " << large << std::endl;
        }
        else
        {
            std::cerr << "\r#: " << count << " <: " << small << " >: " << large << std::endl;
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
    size_t min = 0;
    size_t max = std::numeric_limits<size_t>::max();
    bool rehexlify = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--output" || arg == "-o")
        {
            ARGCHECK();
            output_file = args[++i];
        }
        else if (input_file.empty() && std::filesystem::exists(arg))
        {
            input_file = arg;
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
        else if (arg == "--rehexlify" || arg == "-r")
        {
            rehexlify = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_file]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --output, -o <file>  Specify the output file" << std::endl;
            std::cout << "  --min <number>       Specify the minimum number of bytes to unhexlify" << std::endl;
            std::cout << "  --max <number>       Specify the maximum number of bytes to unhexlify" << std::endl;
            std::cout << "  --rehexlify, -r      Re-hexlify words (used for input standardisation)" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    Unhexlify(input_file, output_file, min, max, rehexlify);
}