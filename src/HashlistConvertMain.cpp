//
//  HashlistConvertMain.cpp
//  HashlistConvert
//
//  Created by Kryc on 02/12/2025.
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
HashlistConvert(
    const std::string_view InputFile,
    const std::string_view OutputFile
)
{
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

    LineReader<> reader(InputFile);
    std::string_view line;
    size_t hashes = 0;
    size_t errors = 0;
    size_t lastlength = 0;
    while (reader.ReadLine(line))
    {
        hashes++;

        // Check if there is a colon, if so, take the first part as the hash and ignore the rest
        size_t colonPos = line.find(':');
        if (colonPos != std::string_view::npos)
        {
            line = line.substr(0, colonPos);
        }

        if (line.length() != lastlength && lastlength != 0)
        {
            errors++;
            continue;
        }
        else if (lastlength == 0)
        {
            lastlength = line.length();
        }

        if (!Util::IsHex(line))
        {
            errors++;
            continue;
        }

        auto bytes = Util::ParseHex(line);
        if (bytes.empty())
        {
            errors++;
            continue;
        }

        output->write(reinterpret_cast<const char*>(bytes.data()), bytes.size());

        if (hashes % 10000 == 0)
        {
            if (totalLines > 0)
            {
                std::cerr << "\r#: " << hashes << "/" << totalLines << "(" << (hashes * 100 / totalLines) << "%) E: " << errors << std::flush;
            }
            else
            {
                std::cerr << "\r#: " << hashes << " E: " << errors << std::flush;
            }
        }
    }

    if (totalLines > 0)
    {
        std::cerr << "\r#: " << hashes << "/" << totalLines << "(" << (hashes * 100 / totalLines) << "%) E: " << errors << std::endl;
    }
    else
    {
        std::cerr << "\r#: " << hashes << " E: " << errors << std::endl;
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_file, output_file;
    std::string temp;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];
        if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_file] [output_file]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --help, -h           Show this help message" << std::endl;
            return 0;
        }
        else if (input_file.empty() && std::filesystem::exists(arg))
        {
            input_file = arg;
        }
        else if (output_file.empty())
        {
            output_file = arg;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    if (input_file.empty())
    {
        std::cerr << "No input file specified" << std::endl;
        return 1;
    }

    if (output_file.empty() && input_file.ends_with(".txt"))
    {
        temp = input_file.substr(0, input_file.length() - 4);
        temp += ".bin";
        output_file = temp;
    }

    HashlistConvert(input_file, output_file);
}