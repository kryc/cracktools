//
//  RandStringMain.cpp
//  RandString
//
//  Created by Kryc on 04/04/2026.
//  Copyright © 2026 Kryc. All rights reserved.
//

#include <fstream>
#include <iostream>
#include <string_view>

#include "Random.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"
#include "WordGenerator.hpp"

#define ARGCHECK() \
    if (argc <= i + 1) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

const std::string HELP_STRING = R"(
Usage: randstring [options] <charset>
Options:
  --min <value>       Set the minimum password length.
  --max <value>       Set the maximum password length.
  --length <value>    Set the exact password length.
  --charset <string>  Set the character set to use.
  --help              Display this help message.
)";

int main(
    const int argc,
    const char* argv[])
{
    // Parse the command line arguments
    auto args = cracktools::ParseArgv(argv, argc);

    size_t min = 8;
    size_t max = 32;
    std::string_view charset = ALPHANUMERIC;
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
        else if (arg == "--length" || arg == "-l")
        {
            ARGCHECK();
            max = Util::ParseNumber<size_t>(args[++i]);
            min = max;
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

    // Create a WordGenerator instance
    std::string usedCharset = std::string(charset);
    if (!additional.empty())
    {
        // Append additional characters to the charset
        usedCharset += std::string(additional);
    }
    WordGenerator generator(usedCharset);

    // Create a PRNG based on the time
    MiniPRNG64 prng(std::chrono::steady_clock::now().time_since_epoch().count());

    // If min != max, we need to generate a random length
    size_t length = (min == max) ? min : 0;
    if (length == 0)
    {
        uint64_t diff = max - min + 1;
        length = prng.Next() % diff + min;
    }

    // Generate a random string of the given length
    for (size_t i = 0; i < length; i++)
    {
        char c = charset[prng.Next() % charset.size()];
        std::cout << c;
    }
    std::cout << std::endl;

    return 0;
}