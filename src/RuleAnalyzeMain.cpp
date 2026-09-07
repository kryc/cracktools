//
// RuleAnalyze
//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "LineReader.hpp"
#include "RuleAnalysis.hpp"
#include "Rules.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"
#include "WordGenerator.hpp"

#define ARGCHECK() \
    if (argc <= i + 1) \
    { \
        std::cerr << "No value specified for " << Arg << std::endl; \
        return 1; \
    }

namespace
{

const std::string HELP_STRING = R"(
Usage: ruleanalyze [options] <rules_file> <lookup_words_file>

Loads and sorts the lookup word list, then applies every rule to every input
word. By default, the lookup words are also the inputs. A match is counted when
the transformed word differs from the input and exists in the lookup word list.

Options:
  --output, -o <file>       Write the report to a file instead of stdout.
    --input-wordlist, -i <file> Use a separate word list as analysis inputs.
    --min, -m <length>        Ignore lookup words shorter than this length.
    --max, -M <length>        Ignore lookup words longer than this length.
    --generate, -g <length>   Generate analysis inputs through this maximum length.
    --charset, -c <name>      Generated charset (default: alphanumeric).
    --printable, -p           Ignore lookup words containing non-printable UTF-8.
    --input-sample, -S <count> Randomly sample this many analysis inputs.
    --sample <count>          Alias for --input-sample.
    --wordlist-sample, -W <count> Randomly sample this many lookup words.
    --seed <number>           Sampling seed for reproducible reports.
    --sort, -s <key>          Sort by matches, unique, rate, coverage, changed,
                                                     rejected, errors, time, or rule.
    --ascending, --asc, -a    Sort the selected key in ascending order.
    --descending, --desc, -d  Sort the selected key in descending order (default).
    --only-zero               Report only rules with no matches.
    --min-matches <count>     Report rules with at least this many matches.
    --max-matches <count>     Report rules with at most this many matches.
    --min-rate <percent>      Minimum matched/changed percentage to report.
    --errors-only             Report only rules that produced syntax errors.
    --exclude-errors          Exclude rules that produced syntax errors.
    --changed-only            Report only rules that changed at least one input.
    --valuable-rules <file>   Write rules with at least one match as a rule file.
    --match-limit <count>     Stop evaluating each rule after this many matches.
    --threads, -t <count>     Number of analysis threads (default: available CPUs).
    --help, -h                Display this help message.

Report format: Markdown table with per-rule statistics.
)";

std::string
MarkdownTableCell(
    const std::string_view Value
)
{
    std::string Encoded;
    Encoded.reserve(Value.size());
    for (const char Character : Value)
    {
        switch (Character)
        {
            case '&': Encoded += "&amp;"; break;
            case '<': Encoded += "&lt;"; break;
            case '>': Encoded += "&gt;"; break;
            case '|': Encoded += "&#124;"; break;
            case '\t': Encoded += "&#9;"; break;
            default: Encoded.push_back(Character); break;
        }
    }
    return Encoded;
}

std::optional<std::vector<std::string>>
LoadWords(
    const std::filesystem::path& Path,
    const size_t EstimatedLines,
    const size_t MinLength,
    const size_t MaxLength,
    const bool PrintableOnly,
    const std::string_view Description
)
{
    const std::string PathString = Path.string();
    LineReader<> Input(PathString);
    if (!Input.Open())
    {
        std::cerr << "Unable to open " << Description << " file: " << Path << std::endl;
        return std::nullopt;
    }

    std::vector<std::string> Words;
    Words.reserve(EstimatedLines + 1);
    std::string_view Word;
    size_t LinesRead = 0;
    const auto Start = std::chrono::steady_clock::now();
    auto LastStatus = Start;
    while (Input.ReadLine(Word))
    {
        LinesRead++;
        std::string ParsedWord = RuleAnalysis::ParseWordlistLine(Word);
        if (ParsedWord.size() >= MinLength
            && ParsedWord.size() <= MaxLength
            && (!PrintableOnly || Util::IsPrintableUTF8(ParsedWord)))
        {
            Words.push_back(std::move(ParsedWord));
        }

        if (LinesRead % 100000 != 0) continue;

        const auto Now = std::chrono::steady_clock::now();
        if (Now - LastStatus < std::chrono::milliseconds(500)) continue;

        LastStatus = Now;
        const double Seconds = std::chrono::duration<double>(
            Now - Start
        ).count();
        std::string WordFactor;
        std::string RateFactor;
        const double DisplayWords = Util::NumFactor(
            static_cast<double>(LinesRead),
            WordFactor
        );
        const double DisplayRate = Util::NumFactor(
            Seconds > 0.0 ? static_cast<double>(LinesRead) / Seconds : 0.0,
            RateFactor
        );

        if (EstimatedLines > 0)
        {
            const double Percent = std::min(
                100.0,
                static_cast<double>(LinesRead) * 100.0 / static_cast<double>(EstimatedLines)
            );
            std::cerr << '\r' << std::format(
                "Loading {}... #:{:.1f}{} ({:.1f}%) Kept:{} Rate:{:.1f}{}/s",
                Description,
                DisplayWords,
                WordFactor,
                Percent,
                Words.size(),
                DisplayRate,
                RateFactor
            ) << std::flush;
        }
        else
        {
            std::cerr << '\r' << std::format(
                "Loading {}... #:{:.1f}{} Kept:{} Rate:{:.1f}{}/s",
                Description,
                DisplayWords,
                WordFactor,
                Words.size(),
                DisplayRate,
                RateFactor
            ) << std::flush;
        }
    }

    return Words;
}

std::optional<std::vector<RuleAnalysis::RuleStatistic>>
LoadRules(
    const std::filesystem::path& Path
)
{
    const std::string PathString = Path.string();
    LineCounter<> Counter(PathString);
    const size_t EstimatedLines = Counter.CountLines();
    LineReader<> Input(PathString);
    if (!Input.Open())
    {
        std::cerr << "Unable to open rules file: " << Path << std::endl;
        return std::nullopt;
    }

    std::vector<RuleAnalysis::RuleStatistic> Statistics;
    Statistics.reserve(EstimatedLines + 1);
    std::string_view Rule;
    size_t SourceOrder = 0;
    while (Input.ReadLine(Rule))
    {
        if (!Rule.empty() && Rule.back() == '\r') Rule.remove_suffix(1);

        Rules::CompiledRule CompiledRule = Rules::Compile(Rule);
        if (CompiledRule.commands.empty()) continue;

        Statistics.push_back({
            std::string(Rule),
            std::move(CompiledRule),
            SourceOrder++
        });
    }

    if (Statistics.empty())
    {
        std::cerr << "No rules found in: " << Path << std::endl;
        return std::nullopt;
    }

    return Statistics;
}

}

int main(
    int argc,
    const char* argv[]
)
{
    const auto Args = cracktools::ParseArgv(argv, argc);
    std::filesystem::path RulesFile;
    std::filesystem::path WordsFile;
    std::filesystem::path InputWordsFile;
    std::filesystem::path OutputFile;
    RuleAnalysis::SortKey SortKey = RuleAnalysis::SortKey::Matches;
    RuleAnalysis::SortOrder SortOrder = RuleAnalysis::SortOrder::Descending;
    size_t MinLength = 0;
    size_t MaxLength = std::numeric_limits<size_t>::max();
    std::optional<size_t> GenerateMaxLength;
    std::string Charset = ALPHANUMERIC;
    bool PrintableOnly = false;
    std::optional<size_t> InputSampleSize;
    std::optional<size_t> WordlistSampleSize;
    std::optional<uint64_t> ConfiguredSeed;
    bool OnlyZero = false;
    size_t MinimumMatches = 0;
    size_t MaximumMatches = std::numeric_limits<size_t>::max();
    double MinimumRate = 0.0;
    bool ErrorsOnly = false;
    bool ExcludeErrors = false;
    bool ChangedOnly = false;
    std::filesystem::path ValuableRulesFile;
    std::optional<size_t> MatchLimit;
    size_t Threads = std::thread::hardware_concurrency();
    if (Threads == 0) Threads = 1;

    for (int i = 1; i < argc; i++)
    {
        const std::string_view Arg = Args[i];
        if (Arg == "--output" || Arg == "-o")
        {
            ARGCHECK();
            OutputFile = Args[++i];
        }
        else if (Arg == "--input-wordlist" || Arg == "--input" || Arg == "-i")
        {
            ARGCHECK();
            InputWordsFile = Args[++i];
        }
        else if (Arg == "--min" || Arg == "-m")
        {
            ARGCHECK();
            MinLength = Util::ParseNumber<size_t>(Args[++i]);
        }
        else if (Arg == "--max" || Arg == "-M")
        {
            ARGCHECK();
            MaxLength = Util::ParseNumber<size_t>(Args[++i]);
        }
        else if (Arg == "--generate" || Arg == "-g")
        {
            ARGCHECK();
            GenerateMaxLength = Util::ParseNumber<size_t>(Args[++i]);
            if (*GenerateMaxLength == 0
                || *GenerateMaxLength == std::numeric_limits<size_t>::max())
            {
                std::cerr << "Generated maximum length must be greater than zero" << std::endl;
                return 1;
            }
        }
        else if (Arg == "--charset" || Arg == "-c")
        {
            ARGCHECK();
            Charset = ParseCharset(Args[++i]);
        }
        else if (Arg == "--printable" || Arg == "-p")
        {
            PrintableOnly = true;
        }
        else if (Arg == "--input-sample" || Arg == "--sample" || Arg == "-S")
        {
            ARGCHECK();
            InputSampleSize = Util::ParseNumber<size_t>(Args[++i]);
            if (*InputSampleSize == 0)
            {
                std::cerr << "Input sample size must be greater than zero" << std::endl;
                return 1;
            }
        }
        else if (Arg == "--wordlist-sample" || Arg == "-W")
        {
            ARGCHECK();
            WordlistSampleSize = Util::ParseNumber<size_t>(Args[++i]);
            if (*WordlistSampleSize == 0)
            {
                std::cerr << "Word-list sample size must be greater than zero" << std::endl;
                return 1;
            }
        }
        else if (Arg == "--seed")
        {
            ARGCHECK();
            ConfiguredSeed = Util::ParseNumber<uint64_t>(Args[++i]);
        }
        else if (Arg == "--only-zero")
        {
            OnlyZero = true;
        }
        else if (Arg == "--min-matches")
        {
            ARGCHECK();
            MinimumMatches = Util::ParseNumber<size_t>(Args[++i]);
        }
        else if (Arg == "--max-matches")
        {
            ARGCHECK();
            MaximumMatches = Util::ParseNumber<size_t>(Args[++i]);
        }
        else if (Arg == "--min-rate")
        {
            ARGCHECK();
            MinimumRate = Util::ParseNumber<double>(Args[++i]) / 100.0;
            if (MinimumRate < 0.0 || MinimumRate > 1.0)
            {
                std::cerr << "Minimum rate must be between 0 and 100" << std::endl;
                return 1;
            }
        }
        else if (Arg == "--errors-only")
        {
            ErrorsOnly = true;
        }
        else if (Arg == "--exclude-errors")
        {
            ExcludeErrors = true;
        }
        else if (Arg == "--changed-only")
        {
            ChangedOnly = true;
        }
        else if (Arg == "--valuable-rules")
        {
            ARGCHECK();
            ValuableRulesFile = Args[++i];
        }
        else if (Arg == "--match-limit" || Arg == "--stop-after-matches")
        {
            ARGCHECK();
            MatchLimit = Util::ParseNumber<size_t>(Args[++i]);
            if (*MatchLimit == 0)
            {
                std::cerr << "Match limit must be greater than zero" << std::endl;
                return 1;
            }
        }
        else if (Arg == "--sort" || Arg == "-s")
        {
            ARGCHECK();
            const std::string_view Key = Args[++i];
            if (Key == "matches" || Key == "match" || Key == "count")
            {
                SortKey = RuleAnalysis::SortKey::Matches;
            }
            else if (Key == "unique" || Key == "unique-matches")
            {
                SortKey = RuleAnalysis::SortKey::UniqueMatches;
            }
            else if (Key == "rate" || Key == "match-rate")
            {
                SortKey = RuleAnalysis::SortKey::MatchRate;
            }
            else if (Key == "coverage")
            {
                SortKey = RuleAnalysis::SortKey::Coverage;
            }
            else if (Key == "changed")
            {
                SortKey = RuleAnalysis::SortKey::Changed;
            }
            else if (Key == "rejected")
            {
                SortKey = RuleAnalysis::SortKey::Rejected;
            }
            else if (Key == "errors")
            {
                SortKey = RuleAnalysis::SortKey::Errors;
            }
            else if (Key == "time")
            {
                SortKey = RuleAnalysis::SortKey::Time;
            }
            else if (Key == "rule")
            {
                SortKey = RuleAnalysis::SortKey::Rule;
            }
            else
            {
                std::cerr << "Unknown sort key: " << Key << std::endl;
                return 1;
            }
        }
        else if (Arg == "--ascending" || Arg == "--asc" || Arg == "-a")
        {
            SortOrder = RuleAnalysis::SortOrder::Ascending;
        }
        else if (Arg == "--descending" || Arg == "--desc" || Arg == "-d")
        {
            SortOrder = RuleAnalysis::SortOrder::Descending;
        }
        else if (Arg == "--threads" || Arg == "-t")
        {
            ARGCHECK();
            Threads = Util::ParseNumber<size_t>(Args[++i]);
            if (Threads == 0)
            {
                std::cerr << "Thread count must be greater than zero" << std::endl;
                return 1;
            }
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
        else if (RulesFile.empty())
        {
            RulesFile = Arg;
        }
        else if (WordsFile.empty())
        {
            WordsFile = Arg;
        }
        else
        {
            std::cerr << "Unexpected argument: " << Arg << std::endl;
            return 1;
        }
    }

    if (RulesFile.empty() || WordsFile.empty())
    {
        std::cerr << HELP_STRING << std::endl;
        return 1;
    }

    if (MinLength > MaxLength)
    {
        std::cerr << "Minimum word length cannot exceed maximum word length" << std::endl;
        return 1;
    }

    if (MinimumMatches > MaximumMatches)
    {
        std::cerr << "Minimum matches cannot exceed maximum matches" << std::endl;
        return 1;
    }

    if (ErrorsOnly && ExcludeErrors)
    {
        std::cerr << "--errors-only and --exclude-errors cannot be combined" << std::endl;
        return 1;
    }

    if (GenerateMaxLength && !InputWordsFile.empty())
    {
        std::cerr << "--generate and --input-wordlist cannot be combined" << std::endl;
        return 1;
    }

    const std::string WordsPath = WordsFile.string();
    std::cerr << "Counting words..." << std::flush;
    LineCounter<> WordCounter(WordsPath);
    const size_t EstimatedWords = WordCounter.CountLines();
    std::cerr << " " << EstimatedWords << " lines" << std::endl;

    std::cerr << "Loading words..." << std::flush;
    auto LoadedWords = LoadWords(
        WordsFile,
        EstimatedWords,
        MinLength,
        MaxLength,
        PrintableOnly,
        "words"
    );
    if (!LoadedWords) return 1;
    std::vector<std::string> Words = std::move(*LoadedWords);

    std::cerr << '\r' << Words.size() << " words loaded                              " << std::endl;

    std::vector<std::string> InputWords;
    std::span<const std::string> Inputs = Words;
    if (!InputWordsFile.empty())
    {
        const std::string InputWordsPath = InputWordsFile.string();
        std::cerr << "Counting input words..." << std::flush;
        LineCounter<> InputWordCounter(InputWordsPath);
        const size_t EstimatedInputWords = InputWordCounter.CountLines();
        std::cerr << " " << EstimatedInputWords << " lines" << std::endl;

        std::cerr << "Loading input words..." << std::flush;
        auto LoadedInputWords = LoadWords(
            InputWordsFile,
            EstimatedInputWords,
            0,
            std::numeric_limits<size_t>::max(),
            false,
            "input words"
        );
        if (!LoadedInputWords) return 1;
        InputWords = std::move(*LoadedInputWords);
        Inputs = InputWords;
        std::cerr << '\r' << InputWords.size()
                  << " input words loaded                              " << std::endl;
    }
    size_t GeneratedInputCount = 0;
    std::vector<size_t> GeneratedSampleIndices;
    std::vector<std::string> SampledInputs;
    std::vector<std::string> SampledWordlist;
    std::random_device RandomDevice;
    const uint64_t BaseSeed = ConfiguredSeed.value_or(
        (static_cast<uint64_t>(RandomDevice()) << 32)
            ^ static_cast<uint64_t>(RandomDevice())
    );
    const uint64_t InputSampleSeed = BaseSeed;
    const uint64_t WordlistSampleSeed = BaseSeed ^ 0x9e3779b97f4a7c15ULL;

    if (InputSampleSize || WordlistSampleSize)
    {
        std::cerr << "Sampling seed: " << BaseSeed << std::endl;
    }

    if (GenerateMaxLength)
    {
        const mpz_class Count = WordGenerator::WordLengthIndex(
            *GenerateMaxLength + 1,
            Charset
        );
        if (!mpz_fits_ulong_p(Count.get_mpz_t()))
        {
            std::cerr << "Generated input count exceeds the supported index range" << std::endl;
            return 1;
        }

        GeneratedInputCount = Count.get_ui();
        std::cerr << "Using " << GeneratedInputCount << " generated analysis inputs" << std::endl;
        if (InputSampleSize && *InputSampleSize < GeneratedInputCount)
        {
            std::cerr << "Sampling " << *InputSampleSize << " generated input indices..." << std::flush;
            GeneratedSampleIndices = RuleAnalysis::RandomSampleIndices(
                GeneratedInputCount,
                *InputSampleSize,
                InputSampleSeed
            );
            std::cerr << " done" << std::endl;
        }
    }
    else if (InputSampleSize && *InputSampleSize < Inputs.size())
    {
        std::cerr << "Sampling " << *InputSampleSize << " of " << Inputs.size() << " analysis inputs..." << std::flush;
        SampledInputs = RuleAnalysis::RandomSample(
            Inputs,
            *InputSampleSize,
            InputSampleSeed
        );
        Inputs = SampledInputs;
        std::cerr << " done" << std::endl;
    }

    std::span<const std::string> LookupWords = Words;
    if (WordlistSampleSize && *WordlistSampleSize < Words.size())
    {
        std::cerr << "Sampling " << *WordlistSampleSize << " of " << Words.size() << " lookup words..." << std::flush;
        SampledWordlist = RuleAnalysis::RandomSample(
            Words,
            *WordlistSampleSize,
            WordlistSampleSeed
        );
        LookupWords = SampledWordlist;
        std::cerr << " done" << std::endl;
    }

    std::cerr << "Sorting " << LookupWords.size() << " lookup words..." << std::flush;
    if (SampledWordlist.empty())
    {
        std::sort(Words.begin(), Words.end());
    }
    else
    {
        std::sort(SampledWordlist.begin(), SampledWordlist.end());
    }
    std::cerr << " done" << std::endl;

    std::cerr << "Building word lookup table..." << std::flush;
    RuleAnalysis::WordLookup WordLookup;
    WordLookup.Initialize(LookupWords);
    std::cerr << " done" << std::endl;

    const size_t AnalysisInputCount = GenerateMaxLength
        ? (GeneratedSampleIndices.empty() ? GeneratedInputCount : GeneratedSampleIndices.size())
        : Inputs.size();

    std::cerr << "Loading rules..." << std::flush;
    auto LoadedRules = LoadRules(RulesFile);
    if (!LoadedRules) return 1;
    std::vector<RuleAnalysis::RuleStatistic> Statistics = std::move(*LoadedRules);

    std::cerr << " " << Statistics.size() << " loaded" << std::endl;
    std::cerr << "Analyzing with " << Threads << " threads" << std::endl;

    std::atomic<size_t> Completed = 0;
    const auto AnalysisStart = std::chrono::steady_clock::now();
    auto PrintStatus = [&]()
    {
        const size_t Complete = Completed.load(std::memory_order_relaxed);
        const double Percent = Statistics.empty()
            ? 100.0
            : static_cast<double>(Complete) * 100.0 / static_cast<double>(Statistics.size());
        const double Seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - AnalysisStart
        ).count();
        const double Evaluations = static_cast<double>(Complete) * static_cast<double>(AnalysisInputCount);
        std::string EvaluationFactor;
        std::string RateFactor;
        const double DisplayEvaluations = Util::NumFactor(Evaluations, EvaluationFactor);
        const double DisplayRate = Util::NumFactor(
            Seconds > 0.0 ? Evaluations / Seconds : 0.0,
            RateFactor
        );

        std::cerr << '\r' << std::format(
            "R:{}/{} ({:.1f}%) E:{:.1f}{} Rate:{:.1f}{}/s",
            Complete,
            Statistics.size(),
            Percent,
            DisplayEvaluations,
            EvaluationFactor,
            DisplayRate,
            RateFactor
        ) << std::flush;
    };

    std::mutex StatusMutex;
    std::condition_variable StatusChanged;
    std::jthread StatusThread([&](const std::stop_token StopToken)
    {
        while (!StopToken.stop_requested())
        {
            std::unique_lock Lock(StatusMutex);
            StatusChanged.wait_for(
                Lock,
                std::chrono::milliseconds(500),
                [&]() { return StopToken.stop_requested(); }
            );
            if (!StopToken.stop_requested()) PrintStatus();
        }
    });

    if (GenerateMaxLength)
    {
        RuleAnalysis::AnalyzeGenerated(
            GeneratedInputCount,
            GeneratedSampleIndices,
            Charset,
            WordLookup,
            Statistics,
            Threads,
            &Completed,
            MatchLimit
        );
    }
    else
    {
        RuleAnalysis::Analyze(
            Inputs,
            WordLookup,
            Statistics,
            Threads,
            &Completed,
            MatchLimit
        );
    }
    StatusThread.request_stop();
    StatusChanged.notify_all();
    StatusThread.join();
    PrintStatus();
    std::cerr << std::endl;

    if (!ValuableRulesFile.empty())
    {
        std::ofstream ValuableRules(ValuableRulesFile, std::ios::out | std::ios::binary);
        if (!ValuableRules.is_open())
        {
            std::cerr << "Unable to open valuable-rules file: " << ValuableRulesFile << std::endl;
            return 1;
        }

        size_t ValuableCount = 0;
        for (const RuleAnalysis::RuleStatistic& Statistic : Statistics)
        {
            if (Statistic.matches == 0) continue;
            ValuableRules << Statistic.rule << '\n';
            ValuableCount++;
        }
        std::cerr << "Wrote " << ValuableCount << " valuable rules to " << ValuableRulesFile << std::endl;
    }

    RuleAnalysis::Sort(Statistics, SortKey, SortOrder);

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

    *Output << "| Rule | Evaluated | Applied | Changed | Matches | Unique Matches"
        << " | Match Rate (%) | Coverage (%) | Rejected | Errors | Time (ms) |\n"
        << "|:-----|----------:|--------:|--------:|--------:|---------------:"
        << "|---------------:|-------------:|---------:|-------:|----------:|\n";
    size_t Reported = 0;
    for (const RuleAnalysis::RuleStatistic& Statistic : Statistics)
    {
        if (OnlyZero && Statistic.matches != 0) continue;
        if (Statistic.matches < MinimumMatches || Statistic.matches > MaximumMatches) continue;
        if (Statistic.MatchRate() < MinimumRate) continue;
        if (ErrorsOnly && Statistic.syntaxErrors == 0) continue;
        if (ExcludeErrors && Statistic.syntaxErrors != 0) continue;
        if (ChangedOnly && Statistic.changed == 0) continue;

        *Output << std::format(
            "| {} | {} | {} | {} | {} | {} | {:.4f} | {:.4f} | {} | {} | {:.3f} |\n",
            MarkdownTableCell(Statistic.rule),
            Statistic.evaluated,
            Statistic.applied,
            Statistic.changed,
            Statistic.matches,
            Statistic.uniqueMatches,
            Statistic.MatchRate() * 100.0,
            Statistic.Coverage() * 100.0,
            Statistic.rejected,
            Statistic.syntaxErrors,
            static_cast<double>(Statistic.elapsedNanoseconds) / 1000000.0
        );
        Reported++;
    }

    std::cerr << "Reported " << Reported << " of " << Statistics.size() << " rules" << std::endl;

    return 0;
}
