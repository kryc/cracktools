//
//  WordGenMain.cpp
//  WordGen
//
//  Created by Kryc on 29/08/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>


#include "UnsafeBuffer.hpp"
#include "Util.hpp"
#include "WordGenerator.hpp"

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

const std::string HELP_STRING = R"(
Usage: wordgen [options] <charset>
Options:
  --min <value>       Set the minimum password length.
  --max <value>       Set the maximum password length.
  --length <value>    Set the exact password length.
  --charset <string>  Set the character set to use.
  --prefix <string>   Set the prefix for generated words.
  --postfix <string>  Set the postfix for generated words.
  --restore <string>  Restore from the given word.
  --help              Display this help message.
)";

static void
MaybeStore(
    const std::string_view RestoreFile,
    const std::string_view Word
)
{
    if (RestoreFile.empty())
    {
        return;
    }

    // Open the file
    std::ofstream restoreFile;
    restoreFile.open(std::string(RestoreFile), std::ios::trunc);
    if (!restoreFile.is_open())
    {
        std::cerr << "Error opening restore file: " << RestoreFile << std::endl;
        return;
    }
    restoreFile << Word << std::endl;
    restoreFile.close();
}

int main(
    const int argc,
    const char* argv[])
{
    // Parse the command line arguments
    auto args = cracktools::ParseArgv(argv, argc);

    if (argc < 2)
    {
        std::cout << HELP_STRING;
        return 0;
    }

    size_t min = 1;
    size_t max = std::numeric_limits<size_t>::max();
    std::string_view restore;
    std::string_view charset = ASCII;
    std::string_view prefix;
    std::string_view postfix;
    std::string_view additional;

    for (int i = 1; i < argc; i++)
	{
        const std::string_view arg = args[i];
        if (arg == "--min" || arg == "-m")
        {
            ARGCHECK();
            min = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--max" || arg == "-M")
        {
            ARGCHECK();
            max = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--restore" || arg == "-r")
        {
            ARGCHECK();
            restore = args[++i];
        }
        else if (arg == "--charset" || arg == "-c")
        {
            ARGCHECK();
            charset = ParseCharset(args[++i]);
        }
        else if (arg == "--additional" || arg == "-a")
        {
            ARGCHECK();
            additional = ParseCharset(args[++i]);
        }
        else if (arg == "--length" || arg == "-l")
        {
            ARGCHECK();
            max = Util::ParseNumber<size_t>(args[++i]);
        }
        else if (arg == "--prefix" || arg == "-p")
        {
            ARGCHECK();
            prefix = args[++i];
        }
        else if (arg == "--postfix" || arg == "-P")
        {
            ARGCHECK();
            postfix = args[++i];
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << HELP_STRING;
            return 0;
        }
        else
        {
            std::cerr << "Unknown option " << arg << std::endl;
            return 1;
        }
    }

    std::string restorePoint;

    // If there is a restore file, open it and read the state
    if (!restore.empty() && std::filesystem::exists(restore))
    {
        // Open the file
        std::ifstream restoreFile;
        restoreFile.open(std::string(restore));
        if (!restoreFile.is_open())
        {
            std::cerr << "Error opening restore file: " << restore << std::endl;
            return 1;
        }
        std::getline(restoreFile, restorePoint);
        restoreFile.close();
    }

    // Check that all characters in the restore point are in the charset
    for (const char c : restorePoint)
    {
        if (charset.find(c) == std::string_view::npos)
        {
            std::cerr << "Restore point contains character not in charset: " << c << std::endl;
            return 1;
        }
    }

    // Create a WordGenerator instance
    
    std::string usedCharset = std::string(charset);
    if (!additional.empty())
    {
        // Append additional characters to the charset
        usedCharset += std::string(additional);
    }
    WordGenerator generator(usedCharset, prefix, postfix);

    // Work out the max length for a uint64 in the given charset
    if (max == std::numeric_limits<size_t>::max())
    {
        for (size_t i = 1; i < 64; i++)
        {
            const auto wordIndex = WordGenerator::WordLengthIndex64(i, charset);
            if (wordIndex > std::numeric_limits<uint64_t>::max())
            {
                max = i - 1;
                break;
            }
        }
    }

    if (max <= 9)
    {
        const size_t lowerbound = WordGenerator::WordLengthIndex64(min, charset);
        const size_t upperbound = WordGenerator::WordLengthIndex64(max + 1, charset);
        size_t startpoint = lowerbound;

        // If we have a restore point, use that now
        if (!restorePoint.empty())
        {
            startpoint = WordGenerator::Parse64(restorePoint, charset);
        }

        if (startpoint > upperbound)
        {
            std::cerr << "Restore point is out of range" << std::endl;
            return 1;
        }

        for (size_t i = startpoint; i < upperbound; i++)
        {
            std::string word = generator.Generate(i);
            std::cout << word << std::endl;
            if ((i & 0xFFFF) == 0)
            {
                MaybeStore(restore, word);
            }
        }
    }
    else
    {
        const mpz_class lowerbound = WordGenerator::WordLengthIndex(min, charset);
        const mpz_class upperbound = WordGenerator::WordLengthIndex(max + 1, charset);
        mpz_class startpoint = lowerbound;

        // If we have a restore point, use that now
        if (!restorePoint.empty())
        {
            startpoint = WordGenerator::Parse(restorePoint, charset);
        }

        if (startpoint > upperbound)
        {
            std::cerr << "Restore point is out of range" << std::endl;
            return 1;
        }

        for (mpz_class i = startpoint; i < upperbound; i++)
        {
            std::string word = generator.Generate(i);
            std::cout << word << std::endl;
            if ((i & 0xFFFF) == 0)
            {
                MaybeStore(restore, word);
            }
        }
    }

    // We got to the end so we can delete any restore file
    if (!restore.empty() && std::filesystem::exists(restore))
    {
        std::filesystem::remove(restore);
    }

    return 0;
}