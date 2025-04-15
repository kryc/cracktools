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
    size_t max = 9;
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
        else
        {
            std::cerr << "Unknown option " << arg << std::endl;
            return 1;
        }
    }

    // Create a WordGenerator instance
    WordGenerator generator(charset, prefix, postfix);

    if (max <= 9)
    {
        const size_t lowerbound = WordGenerator::WordLengthIndex64(min, charset);
        const size_t upperbound = WordGenerator::WordLengthIndex64(max + 1, charset);

        for (size_t i = lowerbound; i < upperbound; i++)
        {
            std::string word = generator.Generate(i);
            std::cout << word << std::endl;
        }
    }
    else
    {
        mpz_class lowerbound = WordGenerator::WordLengthIndex(min, charset);
        mpz_class upperbound = WordGenerator::WordLengthIndex(max + 1, charset);

        for (mpz_class i = lowerbound; i < upperbound; i++)
        {
            std::string word = generator.Generate(i);
            std::cout << word << std::endl;
        }
    }
    

    return 0;
}