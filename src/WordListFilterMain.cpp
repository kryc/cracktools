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

static inline const bool
HexlifyContainsUpperCaseHex(
    const std::string_view Line
)
{
    std::string_view hexpart = Line.substr(5, Line.size() - 6); // Skip the "$HEX[" prefix and "]" suffix
    return std::any_of(
        hexpart.begin(),
        hexpart.end(),
        [](const char c) {
            return (c >= 'A' && c <= 'F');
        }
    );
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
    size_t nonprintable = 0;
    size_t lowered = 0;
    while (reader.ReadLine(line))
    {
        count++;
        const size_t line_size = line.size();
        if (line_size < Min) {
            small++;
            continue;
        }
        const bool is_hexlified = Util::IsHexlified(line);
        if (is_hexlified) {
            const size_t unhex_size = (line_size - 6) / 2;
            if (unhex_size < Min) {
                small++;
                continue;
            }
            else if (unhex_size > Max) {
                large++;
                continue;
            }
        } else if(line_size > Max) {
            large++;
            continue;
        }
        // PrintableASCII is a subset of PrintableUTF8 so check that first
        // and we can skip the second check if ASCIIOnly is set.
        if (ASCIIOnly && !Util::IsPrintableASCIIHexlified(line)) {
            nonprintable++;
            continue;
        }
        else if (PrintableOnly && !Util::IsPrintableUTF8Hexlified(line)) {
            nonprintable++;
            continue;
        }

        // Check if we need to normalize the hexlified line
        if (is_hexlified && HexlifyContainsUpperCaseHex(line)) {
            temp = "$HEX[" + Util::ToLower(line.substr(5, line.size() - 6)) + "]";
            line = std::string_view(temp);
            lowered++;
        }

        *output << line << std::endl;
        if (count % 1000 == 0 && !OutputFile.empty()) {
            if (totalLines > 0)
            {
                std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) "
                          << "<: " << small << " >: " << large << " NP: " << nonprintable << " L: " << lowered << std::flush;
            }
            else
            {
                std::cerr << "\r#: " << count << " <: " << small << " >: " << large << " NP: " << nonprintable << " L: " << lowered << std::flush;
            }
        }
    }

    if (!OutputFile.empty()) {
        if (totalLines > 0)
        {
            std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) "
                      << "<: " << small << " >: " << large << " NP: " << nonprintable << " L: " << lowered << std::endl;
        }
        else
        {
            std::cerr << "\r#: " << count << " <: " << small << " >: " << large << " NP: " << nonprintable << " L: " << lowered << std::endl;
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
        else if (arg == "--length" || arg == "-l")
        {
            ARGCHECK();
            const size_t length = Util::ParseNumber<size_t>(args[++i]);
            min = length;
            max = length;
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