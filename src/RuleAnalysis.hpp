//
// Hashcat rule effectiveness analysis.
//

#ifndef RuleAnalysis_hpp
#define RuleAnalysis_hpp

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Rules.hpp"

namespace RuleAnalysis
{

enum class SortKey
{
    Matches,
    UniqueMatches,
    MatchRate,
    Coverage,
    Changed,
    Rejected,
    Errors,
    Time,
    Rule
};

enum class SortOrder
{
    Ascending,
    Descending
};

struct RuleStatistic
{
    RuleStatistic(
        std::string Rule,
        Rules::CompiledRule CompiledRule,
        const size_t SourceOrder
    ) :
        rule(std::move(Rule)),
        compiledRule(std::move(CompiledRule)),
        sourceOrder(SourceOrder)
    {
    }

    std::string rule;
    Rules::CompiledRule compiledRule;
    size_t evaluated = 0;
    size_t applied = 0;
    size_t changed = 0;
    size_t matches = 0;
    size_t uniqueMatches = 0;
    size_t rejected = 0;
    size_t syntaxErrors = 0;
    uint64_t elapsedNanoseconds = 0;
    size_t sourceOrder = 0;

    [[nodiscard]] double MatchRate(void) const
    {
        return changed == 0
            ? 0.0
            : static_cast<double>(matches) / static_cast<double>(changed);
    }

    [[nodiscard]] double Coverage(void) const
    {
        return evaluated == 0
            ? 0.0
            : static_cast<double>(matches) / static_cast<double>(evaluated);
    }
};

struct WordRange
{
    size_t offset = 0;
    size_t length = 0;
};

class WordLookup
{
public:
    static constexpr size_t TABLE_SIZE = 65536;

    void Initialize(const std::span<const std::string> SortedWords);
    [[nodiscard]] const bool Contains(const std::string_view Word) const;
    [[nodiscard]] const WordRange& GetRange(const uint16_t Prefix) const;

private:
    [[nodiscard]] static const uint16_t Prefix(const std::string_view Word);

    std::span<const std::string> m_Words;
    std::array<WordRange, TABLE_SIZE> m_Ranges{};
};

std::string
ParseWordlistLine(
    const std::string_view Line
);

[[nodiscard]]
std::vector<std::string>
RandomSample(
    const std::span<const std::string> Words,
    const size_t SampleSize,
    const uint64_t Seed
);

[[nodiscard]]
std::vector<size_t>
RandomSampleIndices(
    const size_t PopulationSize,
    const size_t SampleSize,
    const uint64_t Seed
);

// SortedWords must be sorted lexicographically before calling Analyze.
// MatchLimit stops each rule after that many matches; it must be greater than zero.
// Kept and Dropped count completed rules grouped by whether they matched.
void
Analyze(
    const std::span<const std::string> Inputs,
    const WordLookup& Words,
    const std::span<RuleStatistic> Statistics,
    const size_t Threads,
    std::atomic<size_t>* Completed = nullptr,
    const std::optional<size_t> MatchLimit = std::nullopt,
    std::atomic<size_t>* Kept = nullptr,
    std::atomic<size_t>* Dropped = nullptr
);

void
AnalyzeGenerated(
    const size_t InputCount,
    const std::span<const size_t> SampleIndices,
    const std::string_view Charset,
    const WordLookup& Words,
    const std::span<RuleStatistic> Statistics,
    const size_t Threads,
    std::atomic<size_t>* Completed = nullptr,
    const std::optional<size_t> MatchLimit = std::nullopt,
    std::atomic<size_t>* Kept = nullptr,
    std::atomic<size_t>* Dropped = nullptr
);

void
Sort(
    const std::span<RuleStatistic> Statistics,
    const SortKey Key,
    const SortOrder Order
);

}

#endif
