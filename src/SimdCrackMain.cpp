//
//  main.cpp
//  SimdCrack
//
//  Created by Kryc on 13/09/2020.
//  Copyright © 2020 Kryc. All rights reserved.
//

#include <map>
#include <vector>
#include <cstdint>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <filesystem>
#include <assert.h>
#include <gmpxx.h>

#include "Util.hpp"
#include "WordGenerator.hpp"
#include "DispatchQueue.hpp"
#include "SimdCrack.hpp"

// Define the help string as a global constant
const std::string HELP_STRING = R"(
Usage: simdcrack [options] <target>

Options:
  --outfile, -o <file>          Specify the output file for cracked hashes.
  --min <value>                 Set the minimum password length.
  --max <value>                 Set the maximum password length.
  --resume, -r <file>           Resume from a previous cracking session.
  --blocksize, -b <value>       Set the block size for processing.
  --threads, -t <value>         Set the number of threads to use.
  --prefix, -f <string>         Add a prefix to all generated passwords.
  --postfix, -a <string>        Add a postfix to all generated passwords.
  --charset, -c <string>        Specify the character set to use.
  --extra, -e <string>          Add extra characters to the character set.
  --bitmask <value>             Set the bitmask size.
  --sha256                      Use the SHA-256 hash algorithm.
  --sha1                        Use the SHA-1 hash algorithm.
  --md5                         Use the MD5 hash algorithm.
  --md4                         Use the MD4 hash algorithm.
  --algorithm <name>            Specify the hash algorithm (e.g., sha256, sha1, md5, md4).
  --help                        Display this help message.

Positional Arguments:
  <target>                      The hash or target to crack.
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
	SimdCrack simdcrack;

	std::cerr << "SIMDCrack Hash Cracker" << std::endl;

	if (argc < 2)
	{
		std::cerr << "SIMD Lanes: " << SimdLanes() << std::endl;
		std::cout << HELP_STRING << std::endl;
		return 0;
	}

	// Parse the command line arguments
	auto args = cracktools::ParseArgv(argv, argc);

	for (int i = 1; i < argc; i++)
	{
		std::string& arg = args[i];
		if (arg == "--outfile" || arg == "-o")
		{
			ARGCHECK();
			simdcrack.SetOutFile(args[++i]);
		}
		else if (arg == "--min")
		{
			ARGCHECK();
			simdcrack.SetMin(atoi(args[++i].c_str()));
		}
		else if (arg == "--max")
		{
			ARGCHECK();
			simdcrack.SetMax(atoi(args[++i].c_str()));
		}
		else if (arg == "--resume" || arg == "-r")
		{
			ARGCHECK();
			simdcrack.SetResume(args[++i]);
		}
		else if (arg == "--blocksize" || arg == "-b")
		{
			ARGCHECK();
			simdcrack.SetBlocksize(atoll(args[++i].c_str()));
		}
		else if (arg == "--threads" || arg == "-t")
		{
			ARGCHECK();
			simdcrack.SetThreads(atoll(args[++i].c_str()));
		}
		else if (arg == "--prefix" || arg == "-f")
		{
			ARGCHECK();
			simdcrack.SetPrefix(args[++i]);
		}
		else if (arg == "--postfix" || arg == "-a")
		{
			ARGCHECK();
			simdcrack.SetPostfix(args[++i]);
		}
		else if (arg == "--charset" || arg == "-c")
		{
			ARGCHECK();
			simdcrack.SetCharset(args[++i]);
		}
		else if (arg == "--extra" || arg == "-e")
		{
			ARGCHECK();
			simdcrack.SetExtra(args[++i]);
		}
		else if (arg == "--bitmask")
		{
			ARGCHECK();
			simdcrack.SetBitmaskSize(atoi(args[++i].c_str()));
		}
		else if (arg == "--sha256")
		{
			simdcrack.SetAlgorithm(HashAlgorithmSHA256);
		}
		else if (arg == "--sha1")
		{
			simdcrack.SetAlgorithm(HashAlgorithmSHA1);
		}
		else if (arg == "--md5")
		{
			simdcrack.SetAlgorithm(HashAlgorithmMD5);
		}
		else if (arg == "--md4")
		{
			simdcrack.SetAlgorithm(HashAlgorithmMD4);
		}
		else if (arg == "--algorithm")
		{
			ARGCHECK();
			simdcrack.SetAlgorithm(ParseHashAlgorithm(args[++i].c_str()));
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
		else
		{
			assert(arg[0] != '-');
			simdcrack.AddTarget(arg);
		}
	}

	//
	// Create the main dispatcher
	//
	auto mainDispatcher = dispatch::CreateAndEnterDispatcher(
		"main",
		dispatch::bind(
			&SimdCrack::InitAndRun,
			&simdcrack
		)
	);

	return 0;
}
