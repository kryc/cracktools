//
//  main.cpp
//  CrackDB++
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "CrackDBCommon.hpp"
#include "CrackDatabase.hpp"
#include "Util.hpp"

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
    if (argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << " database action [--option] [parameter] path" << std::endl;
        return 0;
    }

    std::string database = argv[1];
    CrackDatabase db(database);
    std::vector<std::string> positionals;
    std::vector<HashAlgorithm> hashes;
    bool quiet = false;

    for (int i = 2; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "--md5")
        {
            hashes.push_back(HashAlgorithmMD5);
        }
        else if (arg == "--sha" || arg == "--sha1")
        {
            hashes.push_back(HashAlgorithmSHA1);
        }
        else if (arg == "--sha2" || arg == "--sha256")
        {
            hashes.push_back(HashAlgorithmSHA256);
        }
        else if (arg == "--sha384")
        {
            hashes.push_back(HashAlgorithmSHA384);
        }
        else if (arg == "--sha512")
        {
            hashes.push_back(HashAlgorithmSHA512);
        }
        else if (arg == "--min")
        {
            ARGCHECK();
            db.SetMin(atoi(argv[++i]));
        }
        else if (arg == "--max")
        {
            ARGCHECK();
            db.SetMax(atoi(argv[++i]));
        }
        else if (arg == "-o" || arg == "--output" || arg == "--out")
        {
            ARGCHECK();
            db.SetOutput(argv[++i]);
        }
        else if (arg == "-u" || arg == "--uncrackable")
        {
            ARGCHECK();
            db.SetUncrackable(argv[++i]);
        }
        else if (arg == "-p" || arg == "--passwords" || arg == "--password")
        {
            db.SetPasswordsOnly(true);
        }
        else if (arg == "-s" || arg == "--separator")
        {
            ARGCHECK();
            db.SetSeparator(argv[++i]);
        }
        else if (arg == "-t" || arg == "--threads")
        {
            ARGCHECK();
            db.SetThreads(atoi(argv[++i]));
        }
        else if (arg == "-b" || arg == "--blocksize")
        {
            ARGCHECK();
            db.SetBlockSize(atoi(argv[++i]));
        }
        else if (arg == "--nohex")
        {
            db.SetHex(false);
        }
        else if (arg == "--nocache")
        {
            db.DisableFileHandleCache();
        }
        else if (arg == "--quiet" || arg == "-q")
        {
            quiet = true;
        }
        else if (arg.starts_with("--"))
        {
            std::cerr << "Unknown option " << arg << std::endl;
            return 1;
        }
        else
        {
            positionals.push_back(arg);
        }
    }

    if (!quiet)
    {
        std::cerr << "CrackDB++ by Kryc" << std::endl;
    }

    if (positionals.size() < 2)
    {
        std::cerr << "Not enough arguments" << std::endl;
        return 1;
    }

    if (/*action*/ positionals[0] == "build")
    {
        db.Build(hashes, positionals[1]);
    }
    else if (/*action*/ positionals[0] == "test")
    {
        std::cerr << "Testing " << positionals[1] << std::endl;
        if (hashes.size() == 0)
        {
            std::cerr << "No algorithm specified" << std::endl;
            return 1;
        }

        HashAlgorithm type = hashes[0];
        
        if (std::filesystem::exists(positionals[1]))
        {
            // Read and crack each line
            std::fstream ifs(positionals[1], std::ios::in);
            
            for(std::string line; getline(ifs, line); )
            {
                // Strip cr and nl
                line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
                line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

                if (line.size() == 0)
                {
                    continue;
                }

                auto result = db.Test(type, line);
                if (!result.has_value())
                {
                    std::cout << line << db.GetSeparator() << "***NOT FOUND***" << std::endl;
                }
            }
        }
        else
        {
            auto result = db.Test(type, positionals[1]);
            if (result.has_value())
            {
                std::cout << positionals[1] << " found!" << std::endl;
            }
        }
    }
    else if (/*action*/ positionals[0] == "crack")
    {
        // Check if it is a single hash
        if (Util::IsHex(positionals[1]))
        {
            auto result = db.Lookup(positionals[1]);
            if (result.has_value())
            {
                auto value = db.GetHex() ? AsciiOrHex(result.value()) : result.value();
                std::cout << Util::ToLower(positionals[1]) << db.GetSeparator() << value << std::endl;
            }
        }
        else
        {
            // Try and open the text file
            if (!std::filesystem::exists(positionals[1]))
            {
                std::cerr << "Unable to open hash file" << std::endl;
                return 1;
            }

            db.CrackFile(positionals[1]);
        }
    }
}