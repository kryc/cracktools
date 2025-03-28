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
#include "Util.hpp"
#include "simdhash.h"

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
        std::cerr << "Usage: " << argv[0] << " hashfile wordlist" << std::endl;
        return 0;
    }

    CrackList cracklist;

    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "--out" || arg == "--outfile" || arg == "-o")
        {
            ARGCHECK();
            cracklist.SetOutFile(argv[++i]);
        }
        else if (arg == "--threads" || arg == "-t")
        {
            ARGCHECK();
            cracklist.SetThreads(atoi(argv[++i]));
        }
        else if (arg == "--blocksize")
        {
            ARGCHECK();
            cracklist.SetBlockSize(atoi(argv[++i]));
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
            cracklist.SetBitmaskSize(atoi(argv[++i]));
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
            cracklist.SetTerminalWidth(atoi(argv[++i]));
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
            std::cerr << "Unrecognised positional argument: " << argv[i] << std::endl;
        }
    }

    cracklist.Crack();

    return 0;
}