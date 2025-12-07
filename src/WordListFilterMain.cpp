//
//  WordListFilterMain.cpp
//  WordListFilter
//
//  Created by Kryc on 07/12/2025.
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
WordlistFilter(
    const std::string_view InputFile,
    const std::string_view OutputFile,
    const size_t Min = 1,
    const size_t Max = std::numeric_limits<size_t>::max(),
    const bool PrintableOnly = false,
    const bool ASCIIOnly = false
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


    LineReader<> reader(input);
    std::string_view line;
    while (reader.ReadLine(line))
    {
        const size_t line_size = line.size();
        if (line_size < Min) {
            continue;
        }
        if (Util::IsHexlified(line)) {
            const size_t unhex_size = (line_size - 6) / 2;
            if (unhex_size < Min || unhex_size > Max) {
                continue;
            }
        } else if(line_size > Max) {
            continue;
        }
        // PrintableASCII is a subset of PrintableUTF8 so check that first
        // and we can skip the second check if ASCIIOnly is set.
        if (ASCIIOnly && !Util::IsPrintableASCIIHexlified(line)) {
            continue;
        }
        else if (PrintableOnly && !Util::IsPrintableUTF8Hexlified(line)) {
            continue;
        }
        *output << line << std::endl;
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_file, output_file;
    size_t min = 1;
    size_t max = std::numeric_limits<size_t>::max();
    bool printable_only = false;
    bool ascii_only = false;

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
        else if (arg == "--printable" || arg == "-p")
        {
            printable_only = true;
        }
        else if (arg == "--ascii" || arg == "-a")
        {
            ascii_only = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_file]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --output, -o <file>  Specify the output file" << std::endl;
            std::cout << "  --min, -m <number>   Specify the minimum number of bytes" << std::endl;
            std::cout << "  --max, -M <number>   Specify the maximum number of bytes" << std::endl;
            std::cout << "  --printable, -p      Only include lines with printable UTF-8 characters" << std::endl;
            std::cout << "  --ascii, -a          Only include lines with printable ASCII characters" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    WordlistFilter(input_file, output_file, min, max, printable_only, ascii_only);
}