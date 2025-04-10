//
//  main.cpp
//  CrackList
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "CrackList.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"
#include "simdhash.h"

// Define the help string as a global constant
const std::string HELP_STRING = R"(
Usage: cracklist [options] hashfile wordlist

Options:
  --out, --outfile, -o <file>   Specify the output file for cracked hashes.
  --threads, -t <value>         Set the number of threads to use.
  --blocksize <value>           Set the block size for processing.
  --sha1, --ntlm, --md5, --md4  Specify the hash algorithm to use.
  --linkedin                    Enable LinkedIn hash processing mode.
  --binary, -b                  Treat input hashes as binary.
  --bitmask, --masksize, -m     Set the bitmask size.
  --autohex, -a                 Automatically convert input to hexadecimal.
  --no-autohex, -A              Disable automatic hexadecimal conversion.
  --parse-hex, -p               Parse input hashes as hexadecimal.
  --text, -T                    Treat input hashes as text.
  --binary, -B                  Treat input hashes as binary.
  --terminal-width, -w <value>  Set the terminal width for output formatting.
  --help                        Display this help message.

Positional Arguments:
  hashfile                      The file containing the hashes to crack.
  wordlist                      The wordlist to use for cracking (default stdin).
)";

#define ARGCHECK() \
    if (argc <= i) \
    { \
        std::cerr << "No value specified for " << arg << std::endl; \
        return 1; \
    }

int main(
    int argc,
    const char * argv[]
)
{
    if (argc < 2)
    {
        std::cout << HELP_STRING << std::endl;
        return 0;
    }

    CrackList cracklist;

    std::cerr << "CrackList Hash Cracker" << std::endl;

    // Parse the command line arguments
    auto args = cracktools::ParseArgv(argv, argc);

    for (int i = 1; i < argc; i++)
    {
        std::string arg = args[i];

        if (arg == "--out" || arg == "--outfile" || arg == "-o")
        {
            ARGCHECK();
            cracklist.SetOutFile(args[++i]);
        }
        else if (arg == "--threads" || arg == "-t")
        {
            ARGCHECK();
            cracklist.SetThreads(atoi(args[++i].c_str()));
        }
        else if (arg == "--blocksize")
        {
            ARGCHECK();
            cracklist.SetBlockSize(atoi(args[++i].c_str()));
        }
        else if (arg == "--sha1" || arg == "--ntlm" || arg == "--md5" || arg == "--md4")
        {
            auto algoStr = arg.substr(2);
            auto algorithm = ParseHashAlgorithm(algoStr.c_str());
            if (algorithm == HashAlgorithmUndefined)
            {
                std::cerr << "Unrecognised hash algorithm \"" << algoStr << "\"" << std::endl;
                return 1;
            }
            cracklist.SetAlgorithm(algorithm);
        }
        else if (arg == "--linkedin")
        {
            cracklist.SetLinkedIn(true);
        }
        else if (arg == "--binary" || arg == "-b")
        {
            cracklist.SetBinary(true);
        }
        else if (arg == "--bitmask" || arg == "--masksize" || arg == "-m")
        {
            ARGCHECK();
            cracklist.SetBitmaskSize(atoi(args[++i].c_str()));
        }
        else if (arg == "--autohex" || arg == "-a")
        {
            cracklist.SetAutohex(true);
        }
        else if (arg == "--no-autohex" || arg == "-A")
        {
            cracklist.DisableAutohex();
        }
        else if (arg == "--parse-hex" || arg == "-p")
        {
            cracklist.SetParseHexInput(true);
        }
        else if (arg == "--text" || arg == "-T")
        {
            cracklist.SetBinary(false);
        }
        else if (arg == "--binary" || arg == "-B")
        {
            cracklist.SetBinary(true);
        }
        else if (arg == "--terminal-width" || arg == "-w")
        {
            ARGCHECK();
            cracklist.SetTerminalWidth(atoi(args[++i].c_str()));
        }
        else if (arg == "--help")
        {
            std::cout << HELP_STRING << std::endl;
            return 0;
        }
        else if (arg.starts_with("--"))
        {
            std::cerr << "Unknown option " << arg << std::endl;
            return 1;
        }
        else if (cracklist.GetHashFile() == "")
        {
            cracklist.SetHashFile(arg);
        }
        else if (cracklist.GetWordlist() == "")
        {
            cracklist.SetWordlist(arg);
        }
        else
        {
            std::cerr << "Unrecognised positional argument: " << args[i] << std::endl;
        }
    }

    cracklist.Crack();

    return 0;
}