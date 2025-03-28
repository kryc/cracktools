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
		std::cerr << "Usage: " << argv[0] << "[options] <target>" << std::endl;
		std::cerr << "SIMD Lanes: " << SimdLanes() << std::endl;
		return 0;
	}

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--outfile" || arg == "-o")
		{
			ARGCHECK();
			simdcrack.SetOutFile(argv[++i]);
		}
		else if (arg == "--min")
		{
			ARGCHECK();
			simdcrack.SetMin(atoi(argv[++i]));
		}
		else if (arg == "--max")
		{
			ARGCHECK();
			simdcrack.SetMax(atoi(argv[++i]));
		}
		else if (arg == "--resume" || arg == "-r")
		{
			ARGCHECK();
			simdcrack.SetResume(argv[++i]);
		}
		else if (arg == "--blocksize" || arg == "-b")
		{
			ARGCHECK();
			simdcrack.SetBlocksize(atoll(argv[++i]));
		}
		else if (arg == "--threads" || arg == "-t")
		{
			ARGCHECK();
			simdcrack.SetThreads(atoll(argv[++i]));
		}
		else if (arg == "--prefix" || arg == "-f")
		{
			ARGCHECK();
			simdcrack.SetPrefix(argv[++i]);
		}
		else if (arg == "--postfix" || arg == "-a")
		{
			ARGCHECK();
			simdcrack.SetPostfix(argv[++i]);
		}
		else if (arg == "--charset" || arg == "-c")
		{
			ARGCHECK();
			simdcrack.SetCharset(argv[++i]);
		}
		else if (arg == "--extra" || arg == "-e")
		{
			ARGCHECK();
			simdcrack.SetExtra(argv[++i]);
		}
		else if (arg == "--bitmask")
		{
			ARGCHECK();
			simdcrack.SetBitmaskSize(atoi(argv[++i]));
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
			simdcrack.SetAlgorithm(ParseHashAlgorithm(argv[++i]));
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
