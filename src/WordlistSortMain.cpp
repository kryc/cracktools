//
// WordlistSort
//

#include <chrono>
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
    --output, -o <file>  Write to a file, with an updating status line on stderr.
    --length, -l         Sort by decoded byte length, then by content.
    --unique, -u         Write each decoded word only once.
    --mmap               Force memory-mapped input and string-view sorting.
    --help, -h           Display this help message.

Memory-mapped sorting is automatic when estimated input storage reaches 256 MiB.
The view index and decoded $HEX[] words still require memory. Do not modify the
input file while sorting.
)";

class StatusLine
{
public:
    explicit StatusLine(const bool Enabled) : m_Enabled(Enabled) {}
    ~StatusLine() { End(); }

    void Update(const std::string_view Message, const bool Force = true)
    {
        if (!m_Enabled) return;
        const auto Now = std::chrono::steady_clock::now();
        if (!Force && Now - m_LastUpdate < std::chrono::milliseconds(200)) return;
        std::cerr << '\r' << Message;
        if (Message.size() < m_Width)
        {
            std::cerr << std::string(m_Width - Message.size(), ' ');
        }
        std::cerr << std::flush;
        m_Width = Message.size();
        m_LastUpdate = Now;
        m_Active = true;
    }

    void End()
    {
        if (m_Active)
        {
            std::cerr << std::endl;
            m_Active = false;
        }
    }

private:
    bool m_Enabled;
    bool m_Active = false;
    size_t m_Width = 0;
    std::chrono::steady_clock::time_point m_LastUpdate;
};

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
    bool LengthFirst = false;
    bool Unique = false;
    bool UseMmap = false;

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
        else if (Arg == "--length" || Arg == "-l")
        {
            LengthFirst = true;
        }
        else if (Arg == "--unique" || Arg == "-u")
        {
            Unique = true;
        }
        else if (Arg == "--mmap")
        {
            UseMmap = true;
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

    std::error_code Error;
    const auto FileSize = std::filesystem::file_size(InputFile, Error);
    if (Error)
    {
        std::cerr << "Unable to read input file size: " << Error.message() << std::endl;
        return 1;
    }
    if (!OutputFile.empty())
    {
        const bool OutputExists = std::filesystem::exists(OutputFile, Error);
        if (!Error && OutputExists)
        {
            const bool SameFile = std::filesystem::equivalent(InputFile, OutputFile, Error);
            if (!Error && SameFile)
            {
                std::cerr << "Input and output must be different files." << std::endl;
                return 1;
            }
        }
        if (Error)
        {
            std::cerr << "Unable to check output file: " << Error.message() << std::endl;
            return 1;
        }
    }

    const std::string InputPath = InputFile.string();
    StatusLine Status(!OutputFile.empty());
    Status.Update("Counting input lines...");
    LineCounter<> Counter(InputPath);
    const size_t LineCount = Counter.CountLines();
    constexpr size_t MmapThreshold = 256u * 1024u * 1024u;
    // Avoid overflow while estimating file bytes plus string-object storage.
    UseMmap = UseMmap || FileSize >= MmapThreshold
        || LineCount >= (MmapThreshold - FileSize) / sizeof(std::string);

    const auto WriteOutput = [&](const auto& Words)
    {
        std::ostream* Output = &std::cout;
        std::ofstream OutputStream;
        if (!OutputFile.empty())
        {
            OutputStream.open(OutputFile, std::ios::out | std::ios::binary);
            if (!OutputStream.is_open())
            {
                Status.End();
                std::cerr << "Unable to open output file: " << OutputFile << std::endl;
                return 1;
            }
            Output = &OutputStream;
        }
        size_t Written = 0;
        WordlistSort::WriteProgress Progress;
        if (!OutputFile.empty())
        {
            Status.Update("Writing: 0/" + std::to_string(Words.size()) + " words checked, 0 written");
            Progress = [&](const size_t Processed, const size_t OutputCount)
            {
                Written = OutputCount;
                Status.Update("Writing: " + std::to_string(Processed) + "/"
                    + std::to_string(Words.size()) + " words checked, "
                    + std::to_string(Written) + " written", Processed == Words.size());
            };
        }
        WordlistSort::Write(Words, *Output, Unique, Progress);
        Output->flush();
        if (!Output->good())
        {
            Status.End();
            std::cerr << "Unable to write sorted output." << std::endl;
            return 1;
        }
        if (!OutputFile.empty())
        {
            Status.Update("Done: " + std::to_string(Written) + " words written.");
        }
        return 0;
    };

    // mmap cannot map an empty file. Both modes produce an empty output for it.
    if (UseMmap && FileSize != 0)
    {
        MmapLineReader Input(InputPath);
        if (!Input.Open())
        {
            Status.End();
            std::cerr << "Unable to map input word list: " << InputFile << std::endl;
            return 1;
        }
        std::deque<std::string> DecodedWords;
        std::vector<std::string_view> Words;
        Status.Update("Reading input (mmap)...");
        // Include room for an unterminated final line to avoid index doubling.
        Words.reserve(LineCount + 1);
        std::string_view Line;
        while (Input.ReadLine(Line))
        {
            Words.push_back(WordlistSort::ParseLineView(Line, DecodedWords));
            if (!OutputFile.empty() && Words.size() % 16384 == 0)
            {
                Status.Update("Reading (mmap): " + std::to_string(Words.size()) + " words", false);
            }
        }
        Status.Update("Sorting " + std::to_string(Words.size()) + " words...");
        WordlistSort::Sort(Words, LengthFirst);
        return WriteOutput(Words);
    }

    LineReader<> Input(InputPath);
    if (!Input.Open())
    {
        Status.End();
        std::cerr << "Unable to open input word list: " << InputFile << std::endl;
        return 1;
    }

    std::vector<std::string> Words;
    Status.Update("Reading input...");
    Words.reserve(LineCount + 1);
    std::string_view Line;
    while (Input.ReadLine(Line))
    {
        Words.push_back(WordlistSort::ParseLine(Line));
        if (!OutputFile.empty() && Words.size() % 16384 == 0)
        {
            Status.Update("Reading: " + std::to_string(Words.size()) + " words", false);
        }
    }

    Status.Update("Sorting " + std::to_string(Words.size()) + " words...");
    WordlistSort::Sort(Words, LengthFirst);

    return WriteOutput(Words);
}