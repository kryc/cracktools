//
// WordlistSort
//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "LineReader.hpp"
#include "UnsafeBuffer.hpp"
#include "WordlistSort.hpp"

namespace
{

const std::string HELP_STRING = R"(
Usage: wordlistsort [options] <input_wordlist>

Unhexlifies every word, sorts by its decoded byte value, and writes the sorted
words to stdout using standard $HEX[] encoding where required.
Likely hexadecimal or crypt prefixes in hash:word lines are discarded automatically.

Options:
    --output, -o <file>  Write sorted words to a file instead of stdout.
    --help, -h           Display this help message.
)";

}

int
main(
    int argc,
    const char* argv[]
)
{
    const auto Args = cracktools::ParseArgv(argv, argc);
    std::filesystem::path InputFile;
    std::filesystem::path OutputFile;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view Arg = Args[i];
        if (Arg == "--output" || Arg == "-o")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "No value specified for " << Arg << std::endl;
                return 1;
            }
            OutputFile = Args[++i];
        }
        else if (Arg == "--help" || Arg == "-h")
        {
            std::cout << HELP_STRING << std::endl;
            return 0;
        }
        else if (Arg.starts_with("-"))
        {
            std::cerr << "Unknown option: " << Arg << std::endl;
            return 1;
        }
        else if (InputFile.empty())
        {
            InputFile = Arg;
        }
        else
        {
            std::cerr << "Unexpected argument: " << Arg << std::endl;
            return 1;
        }
    }

    if (InputFile.empty())
    {
        std::cerr << HELP_STRING << std::endl;
        return 1;
    }

    const std::string InputPath = InputFile.string();
    LineReader<> Input(InputPath);
    if (!Input.Open())
    {
        std::cerr << "Unable to open input word list: " << InputFile << std::endl;
        return 1;
    }

    LineCounter<> Counter(InputPath);
    std::vector<std::string> Words;
    Words.reserve(Counter.CountLines());
    std::string_view Line;
    while (Input.ReadLine(Line))
    {
        Words.push_back(WordlistSort::ParseLine(Line));
    }

    WordlistSort::Sort(Words);

    std::ostream* Output = &std::cout;
    std::ofstream OutputStream;
    if (!OutputFile.empty())
    {
        OutputStream.open(OutputFile, std::ios::out | std::ios::binary);
        if (!OutputStream.is_open())
        {
            std::cerr << "Unable to open output file: " << OutputFile << std::endl;
            return 1;
        }
        Output = &OutputStream;
    }

    WordlistSort::Write(Words, *Output);
    return Output->good() ? 0 : 1;
}