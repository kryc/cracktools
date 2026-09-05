//
// Hashcat rule effectiveness analysis.
//

#include "RuleAnalysis.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "Util.hpp"
#include "WordGenerator.hpp"

namespace RuleAnalysis
{

const uint16_t
WordLookup::Prefix(
    const std::string_view Word
)
{
    uint16_t Value = 0;
    if (!Word.empty())
    {
        Value = static_cast<uint16_t>(static_cast<uint8_t>(Word[0])) << 8;
    }
    if (Word.size() > 1)
    {
        Value |= static_cast<uint8_t>(Word[1]);
    }
    return Value;
}

void
WordLookup::Initialize(
    const std::span<const std::string> SortedWords
)
{
    m_Words = SortedWords;
    m_Ranges.fill({});

    size_t Offset = 0;
    while (Offset < m_Words.size())
    {
        const uint16_t Value = Prefix(m_Words[Offset]);
        size_t End = Offset + 1;
        while (End < m_Words.size() && Prefix(m_Words[End]) == Value)
        {
            End++;
        }

        m_Ranges[Value] = {Offset, End - Offset};
        Offset = End;
    }
}

const bool
WordLookup::Contains(
    const std::string_view Word
) const
{
    const WordRange& Range = m_Ranges[Prefix(Word)];
    if (Range.length == 0) return false;

    const auto Begin = m_Words.begin() + static_cast<std::ptrdiff_t>(Range.offset);
    const auto End = Begin + static_cast<std::ptrdiff_t>(Range.length);
    return std::binary_search(Begin, End, Word);
}

const WordRange&
WordLookup::GetRange(
    const uint16_t Prefix
) const
{
    return m_Ranges[Prefix];
}

std::string
ParseWordlistLine(
    const std::string_view Line
)
{
    std::string_view Word = Line;
    if (!Word.empty() && Word.back() == '\r') Word.remove_suffix(1);

    const size_t Separator = Word.find(':');
    if (Separator != std::string_view::npos
        && Util::IsHex(Word.substr(0, Separator)))
    {
        Word = Word.substr(Separator + 1);
    }

    if (Util::IsHexlified(Word))
    {
        return Util::UnHexlify(Word);
    }

    return std::string(Word);
}

std::vector<std::string>
RandomSample(
    const std::span<const std::string> Words,
    const size_t SampleSize,
    const uint64_t Seed
)
{
    const size_t Count = std::min(SampleSize, Words.size());
    std::vector<std::string> Sample;
    Sample.reserve(Count);
    Sample.insert(Sample.end(), Words.begin(), Words.begin() + static_cast<std::ptrdiff_t>(Count));

    std::mt19937_64 Generator(Seed);
    for (size_t i = Count; i < Words.size(); i++)
    {
        std::uniform_int_distribution<size_t> Distribution(0, i);
        const size_t Position = Distribution(Generator);
        if (Position < Count) Sample[Position] = Words[i];
    }

    return Sample;
}

std::vector<size_t>
RandomSampleIndices(
    const size_t PopulationSize,
    const size_t SampleSize,
    const uint64_t Seed
)
{
    const size_t Count = std::min(SampleSize, PopulationSize);
    std::vector<size_t> Sample;
    Sample.reserve(Count);
    std::unordered_set<size_t> Selected;
    Selected.reserve(Count);
    std::mt19937_64 Generator(Seed);

    for (size_t i = 0; i < Count; i++)
    {
        const size_t End = PopulationSize - Count + i;
        std::uniform_int_distribution<size_t> Distribution(0, End);
        const size_t Candidate = Distribution(Generator);
        const size_t Selection = Selected.contains(Candidate) ? End : Candidate;
        Selected.insert(Selection);
        Sample.push_back(Selection);
    }

    return Sample;
}

template <typename InputProvider>
void
AnalyzeInternal(
    const size_t InputCount,
    const WordLookup& Words,
    const std::span<RuleStatistic> Statistics,
    const size_t Threads,
    std::atomic<size_t>* Completed,
    const InputProvider& GetInput
)
{
    if (Statistics.empty()) return;

    const size_t ThreadCount = std::max<size_t>(
        1,
        std::min(Threads, Statistics.size())
    );
    std::atomic<size_t> NextRule = 0;

    auto Worker = [&]()
    {
        while (true)
        {
            const size_t RuleIndex = NextRule.fetch_add(1, std::memory_order_relaxed);
            if (RuleIndex >= Statistics.size()) return;

            RuleStatistic& Statistic = Statistics[RuleIndex];
            size_t Matches = 0;
            size_t Applied = 0;
            size_t Changed = 0;
            size_t Rejected = 0;
            size_t SyntaxErrors = 0;
            std::unordered_set<std::string> UniqueMatches;
            std::string InputStorage;
            const auto Start = std::chrono::steady_clock::now();

            for (size_t InputIndex = 0; InputIndex < InputCount; InputIndex++)
            {
                const std::string_view Input = GetInput(InputIndex, InputStorage);
                const Rules::Result Result = Rules::Apply(Input, Statistic.compiledRule);
                if (Result.status == Rules::Status::Rejected)
                {
                    Rejected++;
                    continue;
                }
                if (Result.status == Rules::Status::SyntaxError)
                {
                    SyntaxErrors++;
                    continue;
                }

                Applied++;
                if (Result.word == Input) continue;
                Changed++;

                if (Words.Contains(Result.word))
                {
                    Matches++;
                    UniqueMatches.insert(Result.word);
                }
            }

            Statistic.evaluated = InputCount;
            Statistic.applied = Applied;
            Statistic.changed = Changed;
            Statistic.matches = Matches;
            Statistic.uniqueMatches = UniqueMatches.size();
            Statistic.rejected = Rejected;
            Statistic.syntaxErrors = SyntaxErrors;
            Statistic.elapsedNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - Start
            ).count();
            if (Completed != nullptr)
            {
                Completed->fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> Workers;
    Workers.reserve(ThreadCount);
    for (size_t i = 0; i < ThreadCount; i++)
    {
        Workers.emplace_back(Worker);
    }

    for (std::thread& Thread : Workers)
    {
        Thread.join();
    }
}

void
Analyze(
    const std::span<const std::string> Inputs,
    const WordLookup& Words,
    const std::span<RuleStatistic> Statistics,
    const size_t Threads,
    std::atomic<size_t>* Completed
)
{
    AnalyzeInternal(
        Inputs.size(),
        Words,
        Statistics,
        Threads,
        Completed,
        [Inputs](const size_t Index, std::string&) -> std::string_view
        {
            return Inputs[Index];
        }
    );
}

void
AnalyzeGenerated(
    const size_t InputCount,
    const std::span<const size_t> SampleIndices,
    const std::string_view Charset,
    const WordLookup& Words,
    const std::span<RuleStatistic> Statistics,
    const size_t Threads,
    std::atomic<size_t>* Completed
)
{
    const size_t AnalysisCount = SampleIndices.empty() ? InputCount : SampleIndices.size();
    AnalyzeInternal(
        AnalysisCount,
        Words,
        Statistics,
        Threads,
        Completed,
        [InputCount, SampleIndices, Charset](const size_t Index, std::string& Storage) -> std::string_view
        {
            const size_t GeneratorIndex = SampleIndices.empty() ? Index : SampleIndices[Index];
            if (GeneratorIndex >= InputCount) return {};
            Storage = WordGenerator::GenerateWord(static_cast<uint64_t>(GeneratorIndex), Charset);
            return Storage;
        }
    );
}

void
Sort(
    const std::span<RuleStatistic> Statistics,
    const SortKey Key,
    const SortOrder Order
)
{
    std::sort(
        Statistics.begin(),
        Statistics.end(),
        [Key, Order](const RuleStatistic& Left, const RuleStatistic& Right)
        {
            auto Compare = [Order](const auto& LeftValue, const auto& RightValue)
            {
                return Order == SortOrder::Ascending
                    ? LeftValue < RightValue
                    : LeftValue > RightValue;
            };

            if (Key == SortKey::Matches && Left.matches != Right.matches)
            {
                return Compare(Left.matches, Right.matches);
            }
            if (Key == SortKey::UniqueMatches && Left.uniqueMatches != Right.uniqueMatches)
            {
                return Compare(Left.uniqueMatches, Right.uniqueMatches);
            }
            if (Key == SortKey::MatchRate && Left.MatchRate() != Right.MatchRate())
            {
                return Compare(Left.MatchRate(), Right.MatchRate());
            }
            if (Key == SortKey::Coverage && Left.Coverage() != Right.Coverage())
            {
                return Compare(Left.Coverage(), Right.Coverage());
            }
            if (Key == SortKey::Changed && Left.changed != Right.changed)
            {
                return Compare(Left.changed, Right.changed);
            }
            if (Key == SortKey::Rejected && Left.rejected != Right.rejected)
            {
                return Compare(Left.rejected, Right.rejected);
            }
            if (Key == SortKey::Errors && Left.syntaxErrors != Right.syntaxErrors)
            {
                return Compare(Left.syntaxErrors, Right.syntaxErrors);
            }
            if (Key == SortKey::Time && Left.elapsedNanoseconds != Right.elapsedNanoseconds)
            {
                return Compare(Left.elapsedNanoseconds, Right.elapsedNanoseconds);
            }

            if (Key == SortKey::Rule && Left.rule != Right.rule)
            {
                return Compare(Left.rule, Right.rule);
            }

            return Left.sourceOrder < Right.sourceOrder;
        }
    );
}

}
