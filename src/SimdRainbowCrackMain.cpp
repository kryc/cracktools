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
#include "RainbowTable.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"

// Define the help string as a global constant
const std::string HELP_STRING = R"(
Usage: simdrainbowcrack action [options] table

Actions:
  build       Build a rainbow table.
  resume      Resume building a rainbow table.
  crack       Crack a hash using the rainbow table.
  test        Test a password against the rainbow table.
  info        Display information about the rainbow table.

Options:
  -m, --min <value>       Set the minimum password length.
  -M, --max <value>       Set the maximum password length.
  -c, --charset <string>  Set the character set to use.
  -l, --length <value>    Set the chain length.
  -b, --blocksize <value> Set the block size.
  -n, --count <value>     Set the number of chains.
  -C, --coverage <0-100>  Set the target coverage percentage (default: 99).
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
VerifyAndLoad(
    RainbowTable& Table
)
{
    if (!Table.ValidTable())
    {
        std::cerr << "Provided table not found or invalid" << std::endl;
        return 1;
    }

    if (!Table.LoadTable())
    {
        std::cerr << "Error loading table file" << std::endl;
        return 1;
    }

    return 0;
}

int
main(
    const int argc,
    const char* argv[]
)
{
    RainbowTable rainbow;
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
    rainbow.SetCharset("ascii");

    for (int i = 2; i < argc; i++)
	{
		const std::string_view arg = args[i];
        if (arg == "-m" || arg == "--min")
        {
            ARGCHECK();
            rainbow.SetMin(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-M" || arg == "--max")
        {
            ARGCHECK();
            rainbow.SetMax(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "--chars")
        {
            ARGCHECK();
            rainbow.SetMin(Util::ParseNumber<size_t>(args[++i]));
            rainbow.SetMax(rainbow.GetMin());
        }
        else if (arg == "-c" || arg == "--charset")
        {
            ARGCHECK();
            rainbow.SetCharset(args[++i]);
        }
        else if (arg == "-l" || arg == "--length")
        {
            ARGCHECK();
            rainbow.SetLength(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-b" || arg == "--blocksize")
        {
            ARGCHECK();
            rainbow.SetBlocksize(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-n" || arg == "--count")
        {
            ARGCHECK();
            rainbow.SetCount(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-C" || arg == "--coverage")
        {
            ARGCHECK();
            rainbow.SetCoverage(Util::ParseNumber<double>(args[++i]) / 100.0);
        }
        else if (arg == "-t" || arg == "--threads")
        {
            ARGCHECK();
            rainbow.SetThreads(Util::ParseNumber<size_t>(args[++i]));
        }
        else if (arg == "-a" || arg == "--algorithm")
        {
            ARGCHECK();
            rainbow.SetAlgorithm(args[++i]);
        }
        else if (arg == "--md4")
        {
            rainbow.SetAlgorithm("md4");
        }
        else if (arg == "--md5")
        {
            rainbow.SetAlgorithm("md5");
        }
        else if (arg == "--sha1")
        {
            rainbow.SetAlgorithm("sha1");
        }
        else if (arg == "--sha256")
        {
            rainbow.SetAlgorithm("sha256");
        }
        else if (arg == "--ntlm")
        {
            rainbow.SetAlgorithm("ntlm");
        }
        else if (arg == "-h" || arg == "--help")
        {
            std::cout << HELP_STRING << std::endl;
            return 0;
        }
        else if (arg == "-s" || arg == "--separator")
        {
            ARGCHECK();
            rainbow.SetSeparator(args[++i][0]);
        }
        else if (arg.starts_with("-"))
        {
            std::cerr << "Unknown option " << arg << std::endl;
            return 1;
        }
        else if (rainbow.GetPath().empty())
        {
            rainbow.SetPath(args[i]);
        }
        else if (action == "crack" || action == "test")
        {
            target = args[i];
        }
    }

    if (action == "build" || action == "resume")
    {
        if (!rainbow.ValidateConfig())
        {
            std::cerr << "Invalid configuration. Exiting" << std::endl;
            return 1;
        }

        rainbow.InitAndRunBuild();
    }
    else if (action == "crack")
    {
        int check = VerifyAndLoad(rainbow);
        if (check != 0)
        {
            return check;
        }

        rainbow.Crack(target);
    }
    else if (action == "info")
    {
        if (!rainbow.TableExists())
        {
            std::cerr << "Rainbow table not found" << std::endl;
            return 1;
        }

        if (!rainbow.IsTableFile())
        {
            std::cerr << "Invalid rainbow table file" << std::endl;
            return 1;
        }

        if (!rainbow.LoadTable())
        {
            std::cerr << "Error loading table file" << std::endl;
            return 1;
        }

        std::cout << "Algorithm:   " << rainbow.GetAlgorithmString() << std::endl;
        std::cout << "Min:         " << rainbow.GetMin() << std::endl;
        std::cout << "Max:         " << rainbow.GetMax() << std::endl;
        std::cout << "Length:      " << rainbow.GetLength() << std::endl;
        std::cout << "Count:       " << rainbow.GetCount() << std::endl;
        std::cout << "Charset:     \"" << rainbow.GetCharset() << "\"" << std::endl;
        std::cout << "Charset Len: " << rainbow.GetCharset().size() << std::endl;
        std::cout << "KS Coverage: " << rainbow.GetCoverageEstimate() << std::endl;

        size_t total = rainbow.GetCount();
        size_t unique = rainbow.CountUniqueEndpoints();
        size_t dupes = total - unique;
        std::cout << "Endpoints:   " << unique << " unique / " << total
                  << " total (" << std::fixed << std::setprecision(1)
                  << (100.0 * dupes / total) << "% merged)" << std::endl;
    }
    else if (action == "test")
    {
        int check = VerifyAndLoad(rainbow);
        if (check != 0)
        {
            return check;
        }

        if (std::filesystem::exists(target))
        {
            // Read passwords, hash them, write a temp hash file, then crack
            std::ifstream infile(target);
            std::string line;
            std::vector<std::string> passwords;
            std::filesystem::path hashFile = std::filesystem::temp_directory_path() /
                ("rt_test_" + std::to_string(::getpid()) + ".hashes");
            {
                std::ofstream hfs(hashFile);
                while (std::getline(infile, line))
                {
                    if (line.empty()) continue;
                    passwords.push_back(line);
                    auto hash = rainbow.DoHashHex((uint8_t*)&line[0], line.size());
                    hfs << hash << "\n";
                }
            }

            auto results = rainbow.Crack(hashFile.string());
            size_t found = results.size();

            std::filesystem::remove(hashFile);

            std::cout << "Found " << found << "/" << passwords.size()
                      << " (" << std::fixed << std::setprecision(1)
                      << (100.0 * found / passwords.size()) << "%)" << std::endl;
        }
        else
        {
            auto hash = rainbow.DoHashHex((uint8_t*)&target[0], target.size());
            std::cout << "Testing for password \"" << target << "\": " << hash << std::endl;
            rainbow.Crack(hash);
        }
    }

    return 0;
}