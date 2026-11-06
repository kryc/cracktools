//
// Wordlist
//

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "UnsafeBuffer.hpp"
#include "Util.hpp"
#include "Wordlist.hpp"

#define ARGCHECK() \
    if (argc <= i + 1) \
    { \
        std::cerr << "No value specified for " << Arg << std::endl; \
        return 1; \
    }

namespace
{

const std::string HELP_STRING = R"(
Usage: wordlist [options] <operation> <files...>

Operations: union, difference, intersection, symmetric-difference,
            lower, upper, title, clean

Options:
    --threads, -t <count>  Worker count (default: hardware concurrency).
    --include-comments    Keep lines starting with '#'.
    --encoding <name>     ascii, utf8, or utf-8 (byte-preserving ASCII processing).
    --verbose             Print processing summaries to stderr.
    --help, -h            Display this help.
    --                    Treat remaining arguments as positional values.

Reads regular files in parallel; keeps unique decoded words. Difference removes
all later files from the first. Symmetric difference uses odd file membership.
ASCII casing only; other bytes are preserved. Unicode/ICU support is a TODO.
Clean converts possessives, replaces hyphens/spaces with joined and split forms,
and removes empty words and single non-lowercase characters; no transliteration.
Output uses project-standard $HEX[] escaping and is sorted after escaping.
Printable UTF-8 is retained; colons, trailing whitespace and hex-looking words
are escaped. Empty words are written as blank lines.
All records and sorted results must fit in RAM. Do not modify input files.
)";
}

int
main(
    int argc,
    const char* argv[]
)
{
    // Parse the command line arguments.
    const auto Args = cracktools::ParseArgv(argv, argc);
    Wordlist::Options Settings;
    std::optional<Wordlist::Operation> Action;
    std::vector<std::filesystem::path> Files;
    bool Verbose = false;
    bool PositionalOnly = false;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view Arg = Args[i];
        if (!PositionalOnly && Arg == "--")
        {
            PositionalOnly = true;
        }
        else if (!PositionalOnly && (Arg == "--threads" || Arg == "-t"))
        {
            ARGCHECK();
            const std::string_view Value = Args[++i];
            const auto Count = Util::TryParseNumber<size_t>(Value);
            if (!Count || *Count == 0)
            {
                std::cerr << "Thread count must be a positive integer." << std::endl;
                return 1;
            }
            Settings.Threads = *Count;
        }
        else if (!PositionalOnly && Arg == "--encoding")
        {
            ARGCHECK();
            const std::string_view Value = Args[++i];
            if (Value != "ascii" && Value != "utf8" && Value != "utf-8")
            {
                std::cerr << "Unsupported encoding: " << Value
                    << ". Unicode/ICU support is a TODO." << std::endl;
                return 1;
            }
        }
        else if (!PositionalOnly && Arg == "--include-comments")
        {
            Settings.IncludeComments = true;
        }
        else if (!PositionalOnly && Arg == "--verbose")
        {
            Verbose = true;
        }
        else if (!PositionalOnly && (Arg == "--help" || Arg == "-h"))
        {
            std::cout << HELP_STRING;
            return 0;
        }
        else if (!PositionalOnly && Arg.starts_with('-'))
        {
            std::cerr << "Unknown option: " << Arg << std::endl;
            return 1;
        }
        else if (!Action)
        {
            Action = Wordlist::ParseOperation(Arg);
            if (!Action)
            {
                std::cerr << "Unknown operation: " << Arg << std::endl << HELP_STRING;
                return 1;
            }
        }
        else
        {
            Files.emplace_back(Arg);
        }
    }

    // Validate the required operation and input files.
    if (!Action || Files.empty())
    {
        std::cerr << HELP_STRING;
        return 1;
    }
    Settings.Action = *Action;

    // Process the word lists and write the results to stdout.
    if (Verbose)
    {
        for (const auto& File : Files)
        {
            std::cerr << "Reading file: " << File << std::endl;
        }
        std::cerr << "Processing " << Files.size() << " files using "
            << (Settings.Threads == 0 ? "automatic" : std::to_string(Settings.Threads))
            << " worker count." << std::endl;
    }

    std::string Error;
    if (!Wordlist::Process(Files, Settings, std::cout, Error))
    {
        std::cerr << Error << std::endl;
        return 1;
    }
    if (Verbose)
    {
        std::cerr << "Done." << std::endl;
    }

    return 0;
}