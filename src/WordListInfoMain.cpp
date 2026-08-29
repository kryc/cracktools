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
#include <map>
#include <string>
#include <string_view>

#include "LineReader.hpp"
#include "Util.hpp"
#include "UnsafeBuffer.hpp"

#define ARGCHECK() \
    if (argc <= i + 1) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

void
WordlistInfo(
    const std::string_view InputFile,
    const bool Strip = false
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

    std::map<size_t, size_t> length_counts;
    size_t count = 0;

    while (reader.ReadLine(line))
    {
        count++;
        bool is_hexlified = Util::IsHexlified(line);

        // Strip leading and trailing whitespace if needed
        if (Strip) {
            if (is_hexlified)
            {
                temp = Util::UnHexlify(line);
                line = std::string_view(temp);
                is_hexlified = false;
            }
            // Check if the line is all whitespace
            if (line.find_first_not_of(" \t\r\n") == std::string_view::npos) {
                continue;
            }
            line = line.substr(
                line.find_first_not_of(" \t\r\n"),
                line.find_last_not_of(" \t\r\n") - line.find_first_not_of(" \t\r\n") + 1
            );
        }

        const size_t line_size = is_hexlified? (line.size() - 6) / 2 : line.size();
        
        length_counts[line_size]++;

        if (count % 1000 == 0) {
            if (totalLines > 0)
            {
                std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) " << std::flush;
            }
            else
            {
                std::cerr << "\r#: " << count << " " << std::flush;
            }
        }
    }

    if (totalLines > 0)
    {
        std::cerr << "\r#: " << count << "/" << totalLines << "(" << (count * 100 / totalLines) << "%) " << std::endl;
    }
    else
    {
        std::cerr << "\r#: " << count << " " << std::endl;
    }

    // Output the report
    std::cout << "Length\tCount" << std::endl;
    for (const auto& [length, count] : length_counts) {
        std::cout << length << "\t" << count << std::endl;
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_file;
    bool strip = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--strip" || arg == "-s")
        {
            strip = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_file]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --strip, -s          Strip leading and trailing whitespace from lines" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            return 0;
        }
        else if (input_file.empty())
        {
            input_file = arg;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    if (!input_file.empty() && !std::filesystem::exists(input_file))
    {
        std::cerr << "Input file does not exist: " << input_file << std::endl;
        return 1;
    }

    WordlistInfo(input_file, strip);
}