//
//  main.cpp
//  RainbowCrack-
//
//  Created by Kryc on 15/02/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "simdhash.h"
#include "RainbowTableSet.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"

// Define the help string as a global constant
const std::string HELP_STRING = R"(
Usage: simdrainbowcrack action [options] <directory>

Actions:
  build       Build rainbow table(s) in a directory.
  crack       Crack a hash using the rainbow table(s).
  test        Test a password against the rainbow table(s).
  info        Display information about the rainbow table(s).

Options:
  -m, --min <value>       Set the minimum password length.
  -M, --max <value>       Set the maximum password length.
  -c, --charset <string>  Set the character set to use.
  -l, --length <value>    Set the chain length.
  -b, --blocksize <value> Set the block size.
  -F, --flush-size <N>    Pending chains buffered before flushing to a segment (default: auto, sized to available memory).
  -n, --count <value>     Set the number of chains per table.
  -C, --coverage <0-100>  Set the target combined coverage (default: 99).
  -T, --tables <value>    Set the number of tables (default: auto).
  -t, --threads <value>   Set the number of threads.
  -a, --algorithm <name>  Set the hash algorithm (e.g., md5, sha1).
  -s, --separator <char>  Set the output separator (default: ':').
  -h, --help              Display this help message.
)";

#define ARGCHECK() \
    if (argc <= i + 1) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

int
main(
    const int argc,
    const char* argv[]
)
{
    RainbowTableSet tableSet;
    std::string action, target;

    if (argc < 2)
    {
        std::cout << HELP_STRING << std::endl;
        return 0;
    }

    size_t avx = SimdLanes() * 32;
    std::cout << "SimdRainbowCrack (AVX-" << avx << ")" << std::endl;

    // Parse the command line arguments
    auto args = cracktools::ParseArgv(argv, argc);

    action = args[1];

    // Set the default charset
    tableSet.SetCharset("ascii");

    for (int i = 2; i < argc; i++)
	{
		const std::string_view arg = args[i];
        if (arg == "-m" || arg == "--min")
        {
            ARGCHECK();
            tableSet.SetMin(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-M" || arg == "--max")
        {
            ARGCHECK();
            tableSet.SetMax(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "--chars")
        {
            ARGCHECK();
            tableSet.SetMin(Util::ParseNumber<size_t>(args[++i]));
            tableSet.SetMax(tableSet.GetMin());
        }
        else if (arg == "-c" || arg == "--charset")
        {
            ARGCHECK();
            tableSet.SetCharset(args[++i]);
        }
        else if (arg == "-l" || arg == "--length")
        {
            ARGCHECK();
            tableSet.SetLength(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-b" || arg == "--blocksize")
        {
            ARGCHECK();
            tableSet.SetBlocksize(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-F" || arg == "--flush-size")
        {
            ARGCHECK();
            tableSet.SetFlushThreshold(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-n" || arg == "--count")
        {
            ARGCHECK();
            tableSet.SetChainCount(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-C" || arg == "--coverage")
        {
            ARGCHECK();
            tableSet.SetCoverage(Util::ParseNumber<double>(args[++i]) / 100.0);
        }
        else if (arg == "-T" || arg == "--tables")
        {
            ARGCHECK();
            tableSet.SetTableCount(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-t" || arg == "--threads")
        {
            ARGCHECK();
            tableSet.SetThreads(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-a" || arg == "--algorithm")
        {
            ARGCHECK();
            tableSet.SetAlgorithm(args[++i]);
        }
        else if (arg == "--md4")
        {
            tableSet.SetAlgorithm("md4");
        }
        else if (arg == "--md5")
        {
            tableSet.SetAlgorithm("md5");
        }
        else if (arg == "--sha1")
        {
            tableSet.SetAlgorithm("sha1");
        }
        else if (arg == "--sha256")
        {
            tableSet.SetAlgorithm("sha256");
        }
        else if (arg == "--ntlm")
        {
            tableSet.SetAlgorithm("ntlm");
        }
        else if (arg == "-h" || arg == "--help")
        {
            std::cout << HELP_STRING << std::endl;
            return 0;
        }
        else if (arg == "-s" || arg == "--separator")
        {
            ARGCHECK();
            tableSet.SetSeparator(args[++i][0]);
        }
        else if (arg.starts_with("-"))
        {
            std::cerr << "Unknown option " << arg << std::endl;
            return 1;
        }
        else if (tableSet.GetDirectory().empty())
        {
            tableSet.SetDirectory(args[i]);
        }
        else if (action == "crack" || action == "test")
        {
            target = args[i];
        }
    }

    if (tableSet.GetDirectory().empty())
    {
        std::cerr << "No directory specified" << std::endl;
        return 1;
    }

    if (action == "build")
    {
        tableSet.Build();
    }
    else if (action == "crack")
    {
        tableSet.Crack(target);
    }
    else if (action == "info")
    {
        tableSet.Info();
    }
    else if (action == "test")
    {
        tableSet.Test(target);
    }
    else
    {
        std::cerr << "Unknown action: " << action << std::endl;
        return 1;
    }

    return 0;
}