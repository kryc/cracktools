//
// RuleAnalyze
//

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "CsvWriter.hpp"
#include "LineReader.hpp"
#include "RuleAnalysis.hpp"
#include "Rules.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"

#define ARGCHECK() \
    if (argc <= i + 1) \
    { \
        std::cerr << "No value specified for " << Arg << std::endl; \
        return 1; \
    }

namespace
{

const std::string HELP_STRING = R"(
Usage: ruleanalyze [options] <rules_file> <target_words_file>

Exhaustively applies every rule to every analysis input, records the distinct
known targets reached by each rule, then greedily selects rules by marginal
coverage. A selected rule must add at least --min-gain targets not already
covered by higher-ranked rules.

The plaintext base list is required because rules transform those words. Target
plaintexts are streamed into length-bucketed FNV-1a 128 fingerprints.

Selection options:
  --top, -n <count>           Select at most this many rules.
  --min-gain, --bar <count>   Minimum new targets added by each rule (default: 1).
  --min-matches <count>       Alias for --min-gain.
  --coverage <percent>        Stop after reaching this target coverage (default: 100).

Quick first pass:
    --qualify <count>           Keep rules reaching this many unique targets, then
                                                            stop processing each qualifying rule early.

Input options:
  --input-wordlist, -i <file> Use a separate base word list as analysis inputs.
  --input-sample, -S <count>  Randomly sample this many analysis inputs.
  --sample <count>            Alias for --input-sample.
  --seed <number>             Sampling seed for reproducible analysis.
  --min, -m <length>          Ignore targets shorter than this length.
  --max, -M <length>          Ignore targets longer than this length.
  --printable, -p             Ignore targets containing non-printable UTF-8.

Output options:
  --output, -o <file>         Write the CSV ranking to a file instead of stdout.
  --valuable-rules <file>     Write selected rules in optimal ranking order.
  --threads, -t <count>       Analysis threads (default: available CPUs).
  --help, -h                  Display this help message.
)";

std::optional<std::vector<std::string>>
LoadInputs(
    const std::filesystem::path& Path,
    const size_t EstimatedLines
)
{
    const std::string PathString = Path.string();
    LineReader<> Input(PathString);
    if (!Input.Open())
    {
        std::cerr << "Unable to open word list: " << Path << std::endl;
        return std::nullopt;
    }

    std::vector<std::string> Words;
    Words.reserve(EstimatedLines);
    std::string_view Line;
    size_t LinesRead = 0;
    const auto Start = std::chrono::steady_clock::now();
    auto LastStatus = Start;
    while (Input.ReadLine(Line))
    {
        LinesRead++;
        Words.push_back(RuleAnalysis::ParseWordlistLine(Line));

        if (LinesRead % 100000 != 0) continue;
        const auto Now = std::chrono::steady_clock::now();
        if (Now - LastStatus < std::chrono::milliseconds(500)) continue;
        LastStatus = Now;

        const double Seconds = std::chrono::duration<double>(Now - Start).count();
        const double Rate = Seconds > 0.0
            ? static_cast<double>(LinesRead) / Seconds
            : 0.0;
        std::string LineFactor;
        std::string RateFactor;
        const double DisplayLines = Util::NumFactor(
            static_cast<double>(LinesRead),
            LineFactor
        );
        const double DisplayRate = Util::NumFactor(Rate, RateFactor);
        const double Percent = EstimatedLines == 0
            ? 0.0
            : std::min(
                100.0,
                static_cast<double>(LinesRead) * 100.0
                    / static_cast<double>(EstimatedLines)
            );
        std::cerr << '\r' << std::format(
            "Loading {}... #:{:.1f}{} ({:.1f}%) Kept:{} Rate:{:.1f}{}/s",
            "inputs",
            DisplayLines,
            LineFactor,
            Percent,
            Words.size(),
            DisplayRate,
            RateFactor
        ) << std::flush;
    }

    return Words;
}

std::optional<size_t>
LoadTargets(
    const std::filesystem::path& Path,
    const size_t EstimatedLines,
    const size_t MinLength,
    const size_t MaxLength,
    const bool PrintableOnly,
    RuleAnalysis::TargetLookup& Targets
)
{
    const std::string PathString = Path.string();
    LineReader<> Input(PathString);
    if (!Input.Open())
    {
        std::cerr << "Unable to open target word list: " << Path << std::endl;
        return std::nullopt;
    }

    size_t LinesRead = 0;
    size_t Retained = 0;
    const auto Start = std::chrono::steady_clock::now();
    auto LastStatus = Start;
    std::string_view Line;
    while (Input.ReadLine(Line))
    {
        LinesRead++;
        const std::string Word = RuleAnalysis::ParseWordlistLine(Line);
        if (Word.size() >= MinLength
            && Word.size() <= MaxLength
            && (!PrintableOnly || Util::IsPrintableUTF8(Word))
            && Targets.Add(Word))
        {
            Retained++;
        }

        if (LinesRead % 100000 != 0) continue;
        const auto Now = std::chrono::steady_clock::now();
        if (Now - LastStatus < std::chrono::milliseconds(500)) continue;
        LastStatus = Now;

        const double Seconds = std::chrono::duration<double>(Now - Start).count();
        std::string LineFactor;
        std::string RateFactor;
        const double DisplayLines = Util::NumFactor(
            static_cast<double>(LinesRead),
            LineFactor
        );
        const double DisplayRate = Util::NumFactor(
            Seconds > 0.0 ? static_cast<double>(LinesRead) / Seconds : 0.0,
            RateFactor
        );
        const double Percent = EstimatedLines == 0
            ? 0.0
            : std::min(
                100.0,
                static_cast<double>(LinesRead) * 100.0
                    / static_cast<double>(EstimatedLines)
            );
        std::cerr << '\r' << std::format(
            "Loading targets... #:{:.1f}{} ({:.1f}%) Kept:{} Rate:{:.1f}{}/s",
            DisplayLines,
            LineFactor,
            Percent,
            Retained,
            DisplayRate,
            RateFactor
        ) << std::flush;
    }
    return Retained;
}

std::optional<std::vector<RuleAnalysis::RuleStatistic>>
LoadRules(
    const std::filesystem::path& Path
)
{
    const std::string PathString = Path.string();
    LineCounter<> Counter(PathString);
    LineReader<> Input(PathString);
    if (!Input.Open())
    {
        std::cerr << "Unable to open rules file: " << Path << std::endl;
        return std::nullopt;
    }

    std::vector<RuleAnalysis::RuleStatistic> Statistics;
    Statistics.reserve(Counter.CountLines());
    std::string_view Rule;
    size_t SourceOrder = 0;
    while (Input.ReadLine(Rule))
    {
        if (!Rule.empty() && Rule.back() == '\r') Rule.remove_suffix(1);
        Rules::CompiledRule CompiledRule = Rules::Compile(Rule);
        if (CompiledRule.commands.empty()) continue;
        Statistics.emplace_back(std::string(Rule), std::move(CompiledRule), SourceOrder++);
    }

    if (Statistics.empty())
    {
        std::cerr << "No rules found in: " << Path << std::endl;
        return std::nullopt;
    }
    return Statistics;
}

size_t
CountLines(
    const std::filesystem::path& Path
)
{
    const std::string PathString = Path.string();
    LineCounter<> Counter(PathString);
    return Counter.CountLines();
}

std::string
FormatCount(
    const long double Value
)
{
    static constexpr std::array<std::pair<long double, std::string_view>, 5> Units = {{
        {1.0e15L, "P"},
        {1.0e12L, "T"},
        {1.0e9L, "G"},
        {1.0e6L, "M"},
        {1.0e3L, "K"}
    }};
    for (const auto& [Scale, Suffix] : Units)
    {
        if (Value >= Scale)
        {
            return std::format("{:.2f}{}", static_cast<double>(Value / Scale), Suffix);
        }
    }
    return std::format("{:.0f}", static_cast<double>(Value));
}

std::string
FormatDuration(
    const double Seconds
)
{
    if (!std::isfinite(Seconds) || Seconds < 0.0) return "unknown";
    const uint64_t TotalSeconds = static_cast<uint64_t>(Seconds);
    const uint64_t Days = TotalSeconds / 86400;
    const uint64_t Hours = (TotalSeconds % 86400) / 3600;
    const uint64_t Minutes = (TotalSeconds % 3600) / 60;
    const uint64_t RemainingSeconds = TotalSeconds % 60;
    if (Days != 0) return std::format("{}d{:02}h", Days, Hours);
    if (Hours != 0) return std::format("{}h{:02}m", Hours, Minutes);
    if (Minutes != 0) return std::format("{}m{:02}s", Minutes, RemainingSeconds);
    return std::format("{}s", RemainingSeconds);
}

}

int
main(
    int argc,
    const char* argv[]
)
{
    const auto Args = cracktools::ParseArgv(argv, argc);
    std::filesystem::path RulesFile;
    std::filesystem::path TargetWordsFile;
    std::filesystem::path InputWordsFile;
    std::filesystem::path OutputFile;
    std::filesystem::path ValuableRulesFile;
    size_t MinimumTargetLength = 0;
    size_t MaximumTargetLength = std::numeric_limits<size_t>::max();
    size_t MaximumRules = std::numeric_limits<size_t>::max();
    size_t MinimumGain = 1;
    double CoverageGoal = 1.0;
    std::optional<size_t> QualificationBar;
    std::optional<size_t> InputSampleSize;
    std::optional<uint64_t> ConfiguredSeed;
    bool PrintableOnly = false;
    bool TopConfigured = false;
    bool BarConfigured = false;
    bool CoverageConfigured = false;
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
        else if (Arg == "--valuable-rules")
        {
            ARGCHECK();
            ValuableRulesFile = Args[++i];
        }
        else if (Arg == "--input-wordlist" || Arg == "--input" || Arg == "-i")
        {
            ARGCHECK();
            InputWordsFile = Args[++i];
        }
        else if (Arg == "--top" || Arg == "--max-rules" || Arg == "-n")
        {
            ARGCHECK();
            TopConfigured = true;
            MaximumRules = Util::ParseNumber<size_t>(Args[++i]);
            if (MaximumRules == 0)
            {
                std::cerr << "Top rule count must be greater than zero" << std::endl;
                return 1;
            }
        }
        else if (Arg == "--min-gain" || Arg == "--bar" || Arg == "--min-matches")
        {
            ARGCHECK();
            BarConfigured = true;
            MinimumGain = Util::ParseNumber<size_t>(Args[++i]);
            if (MinimumGain == 0)
            {
                std::cerr << "Minimum gain must be greater than zero" << std::endl;
                return 1;
            }
        }
        else if (Arg == "--coverage")
        {
            ARGCHECK();
            CoverageConfigured = true;
            CoverageGoal = Util::ParseNumber<double>(Args[++i]) / 100.0;
            if (CoverageGoal <= 0.0 || CoverageGoal > 1.0)
            {
                std::cerr << "Coverage must be greater than 0 and at most 100" << std::endl;
                return 1;
            }
        }
        else if (Arg == "--qualify")
        {
            ARGCHECK();
            QualificationBar = Util::ParseNumber<size_t>(Args[++i]);
            if (*QualificationBar == 0)
            {
                std::cerr << "Qualification count must be greater than zero" << std::endl;
                return 1;
            }
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
        else if (Arg == "--seed")
        {
            ARGCHECK();
            ConfiguredSeed = Util::ParseNumber<uint64_t>(Args[++i]);
        }
        else if (Arg == "--min" || Arg == "-m")
        {
            ARGCHECK();
            MinimumTargetLength = Util::ParseNumber<size_t>(Args[++i]);
        }
        else if (Arg == "--max" || Arg == "-M")
        {
            ARGCHECK();
            MaximumTargetLength = Util::ParseNumber<size_t>(Args[++i]);
        }
        else if (Arg == "--printable" || Arg == "-p")
        {
            PrintableOnly = true;
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
        else if (TargetWordsFile.empty())
        {
            TargetWordsFile = Arg;
        }
        else
        {
            std::cerr << "Unexpected argument: " << Arg << std::endl;
            return 1;
        }
    }

    if (RulesFile.empty() || TargetWordsFile.empty())
    {
        std::cerr << HELP_STRING << std::endl;
        return 1;
    }
    if (MinimumTargetLength > MaximumTargetLength)
    {
        std::cerr << "Minimum target length cannot exceed maximum target length" << std::endl;
        return 1;
    }
    if (InputWordsFile.empty())
    {
        std::cerr << "--input-wordlist is required" << std::endl;
        return 1;
    }
    if (QualificationBar && (TopConfigured || BarConfigured || CoverageConfigured))
    {
        std::cerr << "--qualify cannot be combined with --top, --bar, --min-gain, "
                  << "--min-matches, or --coverage" << std::endl;
        return 1;
    }
    std::cerr << "Worker threads: " << Threads
              << " (target loading is single-threaded; bucket sorting and analysis are parallel)"
              << std::endl;
    const size_t EstimatedInputs = CountLines(InputWordsFile);
    std::cerr << "Loading " << EstimatedInputs << " input lines..." << std::flush;
    auto LoadedInputs = LoadInputs(InputWordsFile, EstimatedInputs);
    if (!LoadedInputs) return 1;
    std::vector<std::string> InputWords = std::move(*LoadedInputs);
    std::span<const std::string> Inputs = InputWords;
    std::cerr << '\r' << Inputs.size()
              << " input words retained                              " << std::endl;

    const size_t EstimatedTargets = CountLines(TargetWordsFile);
    std::cerr << "Loading " << EstimatedTargets
              << " target lines into 128-bit length buckets..." << std::flush;
    RuleAnalysis::TargetLookup TargetLookup;
    const std::optional<size_t> RetainedTargets = LoadTargets(
        TargetWordsFile,
        EstimatedTargets,
        MinimumTargetLength,
        MaximumTargetLength,
        PrintableOnly,
        TargetLookup
    );
    if (!RetainedTargets) return 1;

    std::cerr << "\rSorting and deduplicating target buckets with " << Threads
              << " workers..." << std::flush;
    const auto TargetFinalizeStart = std::chrono::steady_clock::now();
    TargetLookup.Finalize(Threads);
    const double TargetFinalizeSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - TargetFinalizeStart
    ).count();
    std::cerr << std::format(
        " {} unique targets in {:.3f}s                              ",
        TargetLookup.Size(),
        TargetFinalizeSeconds
    ) << std::endl;
    if (TargetLookup.Size() == 0)
    {
        std::cerr << "No target words remain after filtering" << std::endl;
        return 1;
    }
    if (TargetLookup.Size() > std::numeric_limits<uint32_t>::max())
    {
        std::cerr << "Target count exceeds the supported 32-bit target ID range" << std::endl;
        return 1;
    }

    std::cerr << "Excluding direct base-list matches..." << std::flush;
    const auto DirectTargetStart = std::chrono::steady_clock::now();
    const size_t DirectTargets = TargetLookup.RemoveDirect(Inputs);
    const double DirectTargetSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - DirectTargetStart
    ).count();
    std::cerr << std::format(
        " removed {}, {} remain in {:.3f}s",
        DirectTargets,
        TargetLookup.Size(),
        DirectTargetSeconds
    ) << std::endl;
    if (TargetLookup.Size() == 0)
    {
        std::cerr << "All targets are already covered by the base list" << std::endl;
        return 1;
    }
    if (QualificationBar && *QualificationBar > TargetLookup.Size())
    {
        std::cerr << "Qualification count cannot exceed remaining target count" << std::endl;
        return 1;
    }

    std::random_device RandomDevice;
    const uint64_t Seed = ConfiguredSeed.value_or(
        (static_cast<uint64_t>(RandomDevice()) << 32)
            ^ static_cast<uint64_t>(RandomDevice())
    );
    std::vector<std::string> SampledInputs;
    if (InputSampleSize && *InputSampleSize < Inputs.size())
    {
        SampledInputs = RuleAnalysis::RandomSample(Inputs, *InputSampleSize, Seed);
        Inputs = SampledInputs;
    }
    if (InputSampleSize) std::cerr << "Sampling seed: " << Seed << std::endl;

    std::cerr << "Analysis inputs: " << Inputs.size() << std::endl;

    std::cerr << "Loading rules..." << std::flush;
    auto LoadedRules = LoadRules(RulesFile);
    if (!LoadedRules) return 1;
    std::vector<RuleAnalysis::RuleStatistic> Statistics = std::move(*LoadedRules);
    std::cerr << " " << Statistics.size() << " loaded" << std::endl;

    const long double PlannedEvaluations = static_cast<long double>(Statistics.size())
        * static_cast<long double>(Inputs.size());
    std::cerr << (QualificationBar ? "Worst-case workload: " : "Planned workload: ")
              << FormatCount(PlannedEvaluations)
              << " logical evaluations using " << Threads << " workers" << std::endl;
    if (!InputSampleSize && PlannedEvaluations >= 1.0e11L)
    {
        std::cerr << "WARNING: exhaustive analysis is extremely large. "
                  << "Use --input-sample with --seed for a practical ranking run."
                  << std::endl;
    }

    RuleAnalysis::AnalysisProgress Progress;
    const auto AnalysisStart = std::chrono::steady_clock::now();
    auto PrintStatus = [&]()
    {
        const size_t Complete = Progress.completedRules.load(std::memory_order_acquire);
        const size_t Evaluations = Progress.completedEvaluations.load(std::memory_order_relaxed);
        const size_t Qualified = Progress.qualifiedRules.load(std::memory_order_relaxed);
        const size_t Unqualified = Progress.unqualifiedRules.load(std::memory_order_relaxed);
        const double Percent = static_cast<double>(Complete) * 100.0
            / static_cast<double>(Statistics.size());
        const double Seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - AnalysisStart
        ).count();
        std::string EvaluationFactor;
        std::string RateFactor;
        const double DisplayEvaluations = Util::NumFactor(
            static_cast<double>(Evaluations),
            EvaluationFactor
        );
        const double DisplayRate = Util::NumFactor(
            Seconds > 0.0 ? static_cast<double>(Evaluations) / Seconds : 0.0,
            RateFactor
        );
        const std::string Eta = Complete == 0
            ? "unknown"
            : FormatDuration(
                Seconds * static_cast<double>(Statistics.size() - Complete)
                    / static_cast<double>(Complete)
            );
        if (QualificationBar)
        {
            std::cerr << '\r' << std::format(
                "R:{}/{} ({:.1f}%) E:{:.1f}{} Rate:{:.1f}{}/s +{} -{} ETA:{}",
                Complete,
                Statistics.size(),
                Percent,
                DisplayEvaluations,
                EvaluationFactor,
                DisplayRate,
                RateFactor,
                Qualified,
                Unqualified,
                Eta
            ) << std::flush;
        }
        else
        {
            std::cerr << '\r' << std::format(
                "R:{}/{} ({:.1f}%) E:{:.1f}{} Rate:{:.1f}{}/s ETA:{}",
                Complete,
                Statistics.size(),
                Percent,
                DisplayEvaluations,
                EvaluationFactor,
                DisplayRate,
                RateFactor,
                Eta
            ) << std::flush;
        }
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

    RuleAnalysis::Analyze(
        Inputs,
        TargetLookup,
        Statistics,
        Threads,
        &Progress,
        QualificationBar
    );
    StatusThread.request_stop();
    StatusChanged.notify_all();
    StatusThread.join();
    PrintStatus();
    std::cerr << std::endl;

    if (QualificationBar)
    {
        const size_t Qualified = Progress.qualifiedRules.load(std::memory_order_relaxed);
        std::cerr << "Qualified " << Qualified << " rules at "
                  << *QualificationBar << " unique targets; "
                  << Statistics.size() - Qualified << " did not qualify" << std::endl;

        if (!ValuableRulesFile.empty())
        {
            std::ofstream ValuableRules(ValuableRulesFile, std::ios::out | std::ios::binary);
            if (!ValuableRules.is_open())
            {
                std::cerr << "Unable to open qualified-rules file: "
                          << ValuableRulesFile << std::endl;
                return 1;
            }
            for (const RuleAnalysis::RuleStatistic& Statistic : Statistics)
            {
                if (Statistic.qualified) ValuableRules << Statistic.rule << '\n';
            }
        }

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

        CsvWriter Report(*Output);
        const std::array<std::string, 10> Header = {
            "Rule",
            "UniqueTargets",
            "Matches",
            "Evaluated",
            "Applied",
            "Changed",
            "Rejected",
            "Errors",
            "MatchRate",
            "TimeMs"
        };
        Report.WriteRow(Header);
        for (const RuleAnalysis::RuleStatistic& Statistic : Statistics)
        {
            if (!Statistic.qualified) continue;
            const std::array<std::string, 10> Row = {
                Statistic.rule,
                std::to_string(Statistic.UniqueTargets()),
                std::to_string(Statistic.matches),
                std::to_string(Statistic.evaluated),
                std::to_string(Statistic.applied),
                std::to_string(Statistic.changed),
                std::to_string(Statistic.rejected),
                std::to_string(Statistic.syntaxErrors),
                std::format("{:.6f}", Statistic.MatchRate() * 100.0),
                std::format(
                    "{:.3f}",
                    static_cast<double>(Statistic.elapsedNanoseconds) / 1000000.0
                )
            };
            Report.WriteRow(Row);
        }
        return Output->good() ? 0 : 1;
    }

    RuleAnalysis::SelectionOptions SelectionOptions;
    SelectionOptions.maximumRules = MaximumRules;
    SelectionOptions.minimumGain = MinimumGain;
    SelectionOptions.targetCoverage = CoverageGoal;
    RuleAnalysis::SelectionResult Selection = RuleAnalysis::SelectTopRules(
        Statistics,
        TargetLookup.Size(),
        SelectionOptions
    );
    const double SelectedCoverage = static_cast<double>(Selection.coveredTargets) * 100.0
        / static_cast<double>(TargetLookup.Size());
    std::cerr << std::format(
        "Selected {} rules, dropped {}, covered {}/{} targets ({:.4f}%)",
        Selection.ruleIndices.size(),
        Statistics.size() - Selection.ruleIndices.size(),
        Selection.coveredTargets,
        TargetLookup.Size(),
        SelectedCoverage
    ) << std::endl;

    if (!ValuableRulesFile.empty())
    {
        std::ofstream ValuableRules(ValuableRulesFile, std::ios::out | std::ios::binary);
        if (!ValuableRules.is_open())
        {
            std::cerr << "Unable to open selected-rules file: " << ValuableRulesFile << std::endl;
            return 1;
        }
        for (const size_t RuleIndex : Selection.ruleIndices)
        {
            ValuableRules << Statistics[RuleIndex].rule << '\n';
        }
    }

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

    CsvWriter Report(*Output);
    const std::array<std::string, 15> Header = {
        "Rank",
        "Rule",
        "MarginalGain",
        "CumulativeTargets",
        "CumulativeCoverage",
        "UniqueTargets",
        "TargetCoverage",
        "Matches",
        "MatchRate",
        "Evaluated",
        "Applied",
        "Changed",
        "Rejected",
        "Errors",
        "TimeMs"
    };
    Report.WriteRow(Header);
    for (const size_t RuleIndex : Selection.ruleIndices)
    {
        const RuleAnalysis::RuleStatistic& Statistic = Statistics[RuleIndex];
        const std::array<std::string, 15> Row = {
            std::to_string(Statistic.rank),
            Statistic.rule,
            std::to_string(Statistic.marginalGain),
            std::to_string(Statistic.cumulativeTargets),
            std::format(
                "{:.6f}",
                Statistic.CumulativeCoverage(TargetLookup.Size()) * 100.0
            ),
            std::to_string(Statistic.UniqueTargets()),
            std::format("{:.6f}", Statistic.TargetCoverage(TargetLookup.Size()) * 100.0),
            std::to_string(Statistic.matches),
            std::format("{:.6f}", Statistic.MatchRate() * 100.0),
            std::to_string(Statistic.evaluated),
            std::to_string(Statistic.applied),
            std::to_string(Statistic.changed),
            std::to_string(Statistic.rejected),
            std::to_string(Statistic.syntaxErrors),
            std::format(
                "{:.3f}",
                static_cast<double>(Statistic.elapsedNanoseconds) / 1000000.0
            )
        };
        Report.WriteRow(Row);
    }
    return Output->good() ? 0 : 1;
}
