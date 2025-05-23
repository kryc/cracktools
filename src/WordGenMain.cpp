#include <iostream>
#include <limits>
#include <string>

#include "UnsafeBuffer.hpp"
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
  --charset <string>  Set the character set to use.
  --prefix <string>   Set the prefix for generated words.
  --postfix <string>  Set the postfix for generated words.
  --help              Display this help message.
)";

int main(
    const int argc,
    const char* argv[])
{
    // Parse the command line arguments
    auto args = cracktools::ParseArgv(argv, argc);

    size_t min = 1;
    size_t max = std::numeric_limits<size_t>::max();
    std::string restore;
    std::string charset = ASCII;
    std::string prefix;
    std::string postfix;

    for (int i = 1; i < argc; i++)
	{
        std::string arg = args[i];
        if (arg == "--min")
        {
            ARGCHECK();
            min = std::atoi(args[++i].c_str());
        }
        else if (arg == "--max")
        {
            ARGCHECK();
            max = std::atoi(args[++i].c_str());
        }
        else if (arg == "--restore")
        {
            ARGCHECK();
            restore = args[++i];
        }
        else if (arg == "--charset")
        {
            ARGCHECK();
            charset = ParseCharset(args[++i]);
        }
        else if (arg == "--length")
        {
            ARGCHECK();
            max = std::atoi(args[++i].c_str());
        }
        else if (arg == "--prefix")
        {
            ARGCHECK();
            prefix = args[++i];
        }
        else if (arg == "--postfix")
        {
            ARGCHECK();
            postfix = args[++i];
        }
        else if (arg == "--help")
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
    WordGenerator generator(charset, prefix, postfix);

    // Work out the max length for a uint64 in the given charset
    if (max == std::numeric_limits<size_t>::max())
    {
        for (size_t i = 1; i < 64; i++)
        {
            if (WordGenerator::WordLengthIndex(i, charset) > std::numeric_limits<uint64_t>::max())
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
        if (!restore.empty())
        {
            startpoint = WordGenerator::Parse64(restore, charset);
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
        }
    }
    else
    {
        const mpz_class lowerbound = WordGenerator::WordLengthIndex(min, charset);
        const mpz_class upperbound = WordGenerator::WordLengthIndex(max + 1, charset);
        mpz_class startpoint = lowerbound;

        // If we have a restore point, use that now
        if (!restore.empty())
        {
            startpoint = WordGenerator::Parse(restore, charset);
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
        }
    }
    

    return 0;
}