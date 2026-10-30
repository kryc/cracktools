//
// Hashcat rule effectiveness analysis and marginal-coverage ranking.
//

#ifndef RuleAnalysis_hpp
#define RuleAnalysis_hpp

#include <array>
#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Rules.hpp"

namespace RuleAnalysis
{

struct Hash128
{
    uint64_t high = 0;
    uint64_t low = 0;

    auto operator<=>(const Hash128&) const = default;
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
    std::vector<uint32_t> matchedTargets;
    size_t evaluated = 0;
    size_t applied = 0;
    size_t changed = 0;
    size_t matches = 0;
    size_t rejected = 0;
    size_t syntaxErrors = 0;
    uint64_t elapsedNanoseconds = 0;
    size_t sourceOrder = 0;
    size_t rank = 0;
    size_t marginalGain = 0;
    size_t cumulativeTargets = 0;
    bool qualified = false;

    [[nodiscard]] size_t UniqueTargets(void) const
    {
        return matchedTargets.size();
    }

    [[nodiscard]] double MatchRate(void) const
    {
        return changed == 0
            ? 0.0
            : static_cast<double>(matches) / static_cast<double>(changed);
    }

    [[nodiscard]] double TargetCoverage(const size_t TargetCount) const
    {
        return TargetCount == 0
            ? 0.0
            : static_cast<double>(UniqueTargets()) / static_cast<double>(TargetCount);
    }

    [[nodiscard]] double CumulativeCoverage(const size_t TargetCount) const
    {
        return TargetCount == 0
            ? 0.0
            : static_cast<double>(cumulativeTargets) / static_cast<double>(TargetCount);
    }
};

struct AnalysisProgress
{
    std::atomic<size_t> completedRules = 0;
    std::atomic<size_t> completedEvaluations = 0;
    std::atomic<size_t> qualifiedRules = 0;
    std::atomic<size_t> unqualifiedRules = 0;
};

struct SelectionOptions
{
    size_t maximumRules = std::numeric_limits<size_t>::max();
    size_t minimumGain = 1;
    double targetCoverage = 1.0;
};

struct SelectionResult
{
    std::vector<size_t> ruleIndices;
    size_t coveredTargets = 0;
};

class TargetLookup
{
public:
    static constexpr size_t BUCKET_COUNT = Rules::MAX_PASSWORD_SIZE + 1;

    [[nodiscard]] static Hash128 Fingerprint(const std::string_view Word);

    [[nodiscard]] bool Add(const std::string_view Word);
    void Finalize(const size_t Threads = 1);
    [[nodiscard]] size_t RemoveDirect(const std::span<const std::string> Inputs);
    [[nodiscard]] std::optional<uint32_t> Find(const std::string_view Word) const;
    [[nodiscard]] bool Contains(const std::string_view Word) const;
    [[nodiscard]] size_t Size(void) const;

private:
    void BuildOffsets(void);

    std::array<std::vector<Hash128>, BUCKET_COUNT> m_Buckets;
    std::array<uint32_t, BUCKET_COUNT + 1> m_Offsets{};
    size_t m_Size = 0;
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

void
Analyze(
    const std::span<const std::string> Inputs,
    const TargetLookup& Targets,
    const std::span<RuleStatistic> Statistics,
    const size_t Threads,
    AnalysisProgress* Progress = nullptr,
    // When set, it must be greater than zero.
    const std::optional<size_t> QualificationBar = std::nullopt
);

[[nodiscard]]
SelectionResult
SelectTopRules(
    const std::span<RuleStatistic> Statistics,
    const size_t TargetCount,
    const SelectionOptions& Options
);

}

#endif
