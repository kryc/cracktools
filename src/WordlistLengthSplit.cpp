//
//  WordlistLengthSplit.cpp
//  WordlistLengthSplit
//
//  Created by Kryc on 10/12/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <string>
#include <string_view>

#include "LineReader.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

void
CheckCracked(
    const std::string_view InputWords,
    const std::string_view OutputDirectory = "",
    const size_t MinLength = 1,
    const size_t MaxLength = std::numeric_limits<size_t>::max(),
    const bool AsciiOnly = false,
    const bool PrintableOnly = false
)
{
    // Check if the input file exists, if not read from stdin
    std::istream* input = &std::cin;
    std::ifstream infile;
    if (!InputWords.empty())
    {
        infile.open(InputWords.data(), std::ios::in | std::ios::binary);
        if (!infile.is_open())
        {
            std::cerr << "Error opening input file: " << InputWords << std::endl;
            return;
        }
        input = &infile;
    }

    if (!OutputDirectory.empty() && !std::filesystem::exists(OutputDirectory))
    {
        std::cerr << "Output directory does not exist: " << OutputDirectory << std::endl;
        return;
    } else if (OutputDirectory.empty())
    {
        std::cerr << "No output directory specified." << std::endl;
        return;
    }

    std::unordered_map<size_t, std::ofstream> outputFiles;

    LineReader<> reader(input);
    std::string_view line;
    size_t count = 0;
    size_t toosmall = 0;
    size_t toobig = 0;
    size_t filtered = 0;
    std::string temp;   // Temporary buffer for unhexlifying
    while (reader.ReadLine(line))
    {
        count++;
        size_t length = line.size();
        if (Util::IsHexlified(line))
        {
            length = (line.size() - 6) / 2;
        }

        if (length < MinLength)
        {
            toosmall++;
            continue;
        }
        if (length > MaxLength)
        {
            toobig++;
            continue;
        }

        // PrintableASCII is a subset of PrintableUTF8 so check that first
        // and we can skip the second check if ASCIIOnly is set.
        if (AsciiOnly && !Util::IsPrintableASCIIHexlified(line)) {
            filtered++;
            continue;
        }
        else if (PrintableOnly && !Util::IsPrintableUTF8Hexlified(line)) {
            filtered++;
            continue;
        }

        // Open output file for this length if not already opened
        if (outputFiles.find(length) == outputFiles.end())
        {
            std::string outputFileName;
            outputFileName = OutputDirectory;;
            if (outputFileName.back() != '/' && outputFileName.back() != '\\')
            {
                outputFileName += '/';
            }
            outputFileName += "word_" + std::to_string(length) + ".txt";
            outputFiles[length].open(outputFileName, std::ios::out | std::ios::binary);
            if (!outputFiles[length].is_open())
            {
                std::cerr << "Error opening output file: " << outputFileName << std::endl;
                return;
            }
        }

        outputFiles[length] << line << std::endl;

        if (count % 1000 == 0)
        {
            std::cout << "\r#: " << count << " <: " << toosmall << " >: " << toobig << " F: " << filtered << std::flush;
        }
    }
}

int main(
	int argc,
	const char * argv[]
)
{
    auto args = cracktools::ParseArgv(argv, argc);
    std::string_view input_words, output_directory;
    size_t min_length = 1;
    size_t max_length = std::numeric_limits<size_t>::max();
    bool ascii_only = false;
    bool printable_only = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view arg = args[i];

        if (arg == "--min" || arg == "-m")
        {
            ARGCHECK();
            min_length = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--max" || arg == "-M")
        {
            ARGCHECK();
            max_length = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--length" || arg == "-l")
        {
            ARGCHECK();
            min_length = max_length = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--ascii" || arg == "-a")
        {
            ascii_only = true;
        }
        else if (arg == "--printable" || arg == "-p")
        {
            printable_only = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: " << args[0] << " [options] [input_words]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -o, --output <file>    Output file (default: stdout)" << std::endl;
            return 0;
        }
        else if (input_words.empty() && std::filesystem::exists(arg))
        {
            input_words = arg;
        }
        else if (!input_words.empty() && output_directory.empty())
        {
            output_directory = arg;
        }
    }

    if (!output_directory.empty() && !std::filesystem::exists(output_directory))
    {
        std::cerr << "Output directory does not exist: " << output_directory << std::endl;
        return 1;
    }

    CheckCracked(input_words, output_directory, min_length, max_length, ascii_only, printable_only);
}