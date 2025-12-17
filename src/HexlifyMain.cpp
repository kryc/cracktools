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
Hexlify(
    const std::string_view InputFile,
    const std::string_view OutputFile
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
    size_t count = 0;
    while (reader.ReadLine(line))
    {
        count++;
        // If it is already a strictly valid $HEX[] line then pass through
        if (!Util::NeedsHexlify(line) || Util::IsHexlified(line))
        {
            *output << line << std::endl;
        }
        else
        {
            *output << Util::Hexlify(line) << std::endl;
        }

        if (count % 1000 == 0 && !OutputFile.empty()) {
            if (totalLines > 0)
            {
                std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) " << std::flush;
            }
            else
            {
                std::cerr << "\r#: " << count << std::flush;
            }
        }
    }

    if (!OutputFile.empty()) {
        if (totalLines > 0)
        {
            std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%)" << std::endl;
        }
        else
        {
            std::cerr << "\r#: " << count << std::endl;
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
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: hexlify [options] [input_file]\n";
            std::cout << "Options:\n";
            std::cout << "  --output, -o <file>  Output file (default: stdout)\n";
            std::cout << "  --help, -h           Display this help message\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    Hexlify(input_file, output_file);

}