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

#include "CrackDatabase.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"

// Define the help string as a global constant
const std::string HELP_STRING = R"(
Usage: crackdb <database> <action> [options] <path>

Actions:
  build                        Build a database from a wordlist.
  test                         Test a hash or file of hashes against the database.
  crack                        Crack a hash or file of hashes using the database.

Options:
  --md5                        Use the MD5 hash algorithm.
  --sha, --sha1                Use the SHA-1 hash algorithm.
  --sha2, --sha256             Use the SHA-256 hash algorithm.
  --sha384                     Use the SHA-384 hash algorithm.
  --sha512                     Use the SHA-512 hash algorithm.
  --min <value>                Set the minimum password length.
  --max <value>                Set the maximum password length.
  -o, --output, --out <file>   Specify the output file for results.
  -u, --uncrackable <file>     Specify a file to store uncrackable hashes.
  -p, --passwords              Output only passwords (no hashes).
  -s, --separator <char>       Set the separator for output (default: ':').
  -t, --threads <value>        Set the number of threads to use.
  -b, --blocksize <value>      Set the block size for processing.
  --nohex                      Disable hexadecimal output for results.
  --nocache                    Disable file handle caching.
  -q, --quiet                  Suppress output messages.
  --help                       Display this help message.

Positional Arguments:
  <database>                   The path to the database file.
  <action>                     The action to perform (build, test, crack).
  <path>                       The path to the wordlist or hash file.

Examples:
  crackdb mydb.db build --min 6 --max 12 wordlist.txt
  crackdb mydb.db test --sha256 hashes.txt
  crackdb mydb.db crack --nohex single_hash.txt
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
    if (argc < 4)
    {
        std::cout << HELP_STRING << std::endl;
        return 0;
    }

    // Parse the command line arguments
    auto args = cracktools::ParseArgv(argv, argc);

    CrackDatabase db(args[1]);
    std::vector<std::string> positionals;
    std::vector<HashAlgorithm> hashes;
    bool quiet = false;

    for (int i = 2; i < argc; i++)
    {
        std::string arg = args[i];

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
            db.SetMin(atoi(args[++i].c_str()));
        }
        else if (arg == "--max")
        {
            ARGCHECK();
            db.SetMax(atoi(args[++i].c_str()));
        }
        else if (arg == "-o" || arg == "--output" || arg == "--out")
        {
            ARGCHECK();
            db.SetOutput(args[++i]);
        }
        else if (arg == "-u" || arg == "--uncrackable")
        {
            ARGCHECK();
            db.SetUncrackable(args[++i]);
        }
        else if (arg == "-p" || arg == "--passwords" || arg == "--password")
        {
            db.SetPasswordsOnly(true);
        }
        else if (arg == "-s" || arg == "--separator")
        {
            ARGCHECK();
            db.SetSeparator(args[++i]);
        }
        else if (arg == "-t" || arg == "--threads")
        {
            ARGCHECK();
            db.SetThreads(atoi(args[++i].c_str()));
        }
        else if (arg == "-b" || arg == "--blocksize")
        {
            ARGCHECK();
            db.SetBlockSize(atoi(args[++i].c_str()));
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
                auto value = db.GetHex() ? Util::Hexlify(result.value()) : result.value();
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