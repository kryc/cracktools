//
// Hashcat rule effectiveness analysis and marginal-coverage ranking.
//

#include "RuleAnalysis.hpp"

#include <algorithm>
#include <chrono>
#include <queue>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>

#include "Util.hpp"

namespace RuleAnalysis
{
namespace
{

constexpr __uint128_t FNV128_OFFSET_BASIS =
    (static_cast<__uint128_t>(0x6c62272e07bb0142ULL) << 64)
    | static_cast<__uint128_t>(0x62b821756295c58dULL);
constexpr __uint128_t FNV128_PRIME =
    (static_cast<__uint128_t>(0x0000000001000000ULL) << 64)
    | static_cast<__uint128_t>(0x000000000000013bULL);

struct Candidate
{
    size_t gain;
    size_t ruleIndex;
    size_t sourceOrder;
};

struct CandidateLess
{
    bool operator()(const Candidate& Left, const Candidate& Right) const
    {
        if (Left.gain != Right.gain) return Left.gain < Right.gain;
        return Left.sourceOrder > Right.sourceOrder;
    }
};

size_t
CountUncovered(
    const RuleStatistic& Statistic,
    const std::span<const uint8_t> Covered
)
{
    return static_cast<size_t>(std::count_if(
        Statistic.matchedTargets.begin(),
        Statistic.matchedTargets.end(),
        [Covered](const uint32_t Target)
        {
            return Target < Covered.size() && Covered[Target] == 0;
        }
    ));
}

}

Hash128
TargetLookup::Fingerprint(
    const std::string_view Word
)
{
    __uint128_t Hash = FNV128_OFFSET_BASIS;
    for (const char Character : Word)
    {
        Hash ^= static_cast<uint8_t>(Character);
        Hash *= FNV128_PRIME;
    }
    return {
        static_cast<uint64_t>(Hash >> 64),
        static_cast<uint64_t>(Hash)
    };
}

bool
TargetLookup::Add(
    const std::string_view Word
)
{
    if (Word.size() >= BUCKET_COUNT) return false;
    m_Buckets[Word.size()].push_back(Fingerprint(Word));
    return true;
}

void
TargetLookup::BuildOffsets(void)
{
    size_t Offset = 0;
    for (size_t Length = 0; Length < BUCKET_COUNT; Length++)
    {
        m_Offsets[Length] = Offset;
        Offset += m_Buckets[Length].size();
    }
    m_Offsets[BUCKET_COUNT] = Offset;
    m_Size = Offset;
}

void
TargetLookup::Finalize(
    const size_t Threads
)
{
    const size_t ThreadCount = std::max<size_t>(
        1,
        std::min(Threads, BUCKET_COUNT)
    );
    std::atomic<size_t> NextBucket = 0;
    auto Worker = [&]()
    {
        while (true)
        {
            const size_t BucketIndex = NextBucket.fetch_add(1, std::memory_order_relaxed);
            if (BucketIndex >= BUCKET_COUNT) return;

            std::vector<Hash128>& Bucket = m_Buckets[BucketIndex];
            std::sort(Bucket.begin(), Bucket.end());
            Bucket.erase(std::unique(Bucket.begin(), Bucket.end()), Bucket.end());
            Bucket.shrink_to_fit();
        }
    };

    std::vector<std::thread> Workers;
    Workers.reserve(ThreadCount);
    for (size_t i = 0; i < ThreadCount; i++) Workers.emplace_back(Worker);
    for (std::thread& WorkerThread : Workers) WorkerThread.join();
    BuildOffsets();
}

size_t
TargetLookup::RemoveDirect(
    const std::span<const std::string> Inputs
)
{
    std::array<std::vector<Hash128>, BUCKET_COUNT> InputBuckets;
    for (const std::string& Input : Inputs)
    {
        if (Input.size() < BUCKET_COUNT)
        {
            InputBuckets[Input.size()].push_back(Fingerprint(Input));
        }
    }

    const size_t OriginalSize = m_Size;
    for (size_t Length = 0; Length < BUCKET_COUNT; Length++)
    {
        std::vector<Hash128>& InputsAtLength = InputBuckets[Length];
        std::sort(InputsAtLength.begin(), InputsAtLength.end());
        InputsAtLength.erase(
            std::unique(InputsAtLength.begin(), InputsAtLength.end()),
            InputsAtLength.end()
        );

        std::vector<Hash128>& TargetsAtLength = m_Buckets[Length];
        size_t InputIndex = 0;
        size_t WriteIndex = 0;
        for (const Hash128& Target : TargetsAtLength)
        {
            while (InputIndex < InputsAtLength.size()
                && InputsAtLength[InputIndex] < Target)
            {
                InputIndex++;
            }
            if (InputIndex < InputsAtLength.size()
                && InputsAtLength[InputIndex] == Target)
            {
                continue;
            }
            TargetsAtLength[WriteIndex++] = Target;
        }
        TargetsAtLength.resize(WriteIndex);
    }
    BuildOffsets();
    return OriginalSize - m_Size;
}

std::optional<uint32_t>
TargetLookup::Find(
    const std::string_view Word
) const
{
    if (Word.size() >= BUCKET_COUNT) return std::nullopt;
    const std::vector<Hash128>& Bucket = m_Buckets[Word.size()];
    const Hash128 Hash = Fingerprint(Word);
    const auto Found = std::lower_bound(Bucket.begin(), Bucket.end(), Hash);
    if (Found == Bucket.end() || *Found != Hash) return std::nullopt;

    const size_t TargetId = m_Offsets[Word.size()]
        + static_cast<size_t>(std::distance(Bucket.begin(), Found));
    if (TargetId > std::numeric_limits<uint32_t>::max()) return std::nullopt;
    return static_cast<uint32_t>(TargetId);
}

bool
TargetLookup::Contains(
    const std::string_view Word
) const
{
    return Find(Word).has_value();
}

size_t
TargetLookup::Size(void) const
{
    return m_Size;
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
        && Util::IsLikelyValidHash(Word.substr(0, Separator)))
    {
        Word.remove_prefix(Separator + 1);
    }
    return Util::UnHexlify(Word);
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

void
Analyze(
    const std::span<const std::string> Inputs,
    const TargetLookup& Targets,
    const std::span<RuleStatistic> Statistics,
    const size_t Threads,
    AnalysisProgress* Progress,
    const std::optional<size_t> QualificationBar
)
{
    if (Statistics.empty()) return;

    const size_t ThreadCount = std::max<size_t>(1, std::min(Threads, Statistics.size()));
    std::atomic<size_t> NextRule = 0;
    auto Worker = [&]()
    {
        while (true)
        {
            const size_t RuleIndex = NextRule.fetch_add(1, std::memory_order_relaxed);
            if (RuleIndex >= Statistics.size()) return;

            RuleStatistic& Statistic = Statistics[RuleIndex];
            Statistic.matchedTargets.clear();
            Statistic.evaluated = 0;
            Statistic.applied = 0;
            Statistic.changed = 0;
            Statistic.matches = 0;
            Statistic.rejected = 0;
            Statistic.syntaxErrors = 0;
            Statistic.elapsedNanoseconds = 0;
            Statistic.rank = 0;
            Statistic.marginalGain = 0;
            Statistic.cumulativeTargets = 0;
            Statistic.qualified = false;
            std::vector<uint32_t> MatchedTargets;
            std::unordered_set<uint32_t> QualificationTargets;
            if (QualificationBar)
            {
                const size_t InitialCapacity = std::min<size_t>(*QualificationBar, 4096);
                QualificationTargets.reserve(InitialCapacity);
                MatchedTargets.reserve(InitialCapacity);
            }
            const auto Start = std::chrono::steady_clock::now();

            for (const std::string& Input : Inputs)
            {
                Statistic.evaluated++;
                const Rules::Result Result = Rules::Apply(Input, Statistic.compiledRule);
                if (Result.status == Rules::Status::Rejected)
                {
                    Statistic.rejected++;
                    continue;
                }
                if (Result.status == Rules::Status::SyntaxError)
                {
                    Statistic.syntaxErrors++;
                    continue;
                }

                Statistic.applied++;
                if (Result.word == Input) continue;
                Statistic.changed++;
                const std::optional<uint32_t> Target = Targets.Find(Result.word);
                if (!Target) continue;
                Statistic.matches++;
                if (QualificationBar)
                {
                    if (QualificationTargets.insert(*Target).second)
                    {
                        MatchedTargets.push_back(*Target);
                        if (MatchedTargets.size() >= *QualificationBar)
                        {
                            Statistic.qualified = true;
                            break;
                        }
                    }
                }
                else
                {
                    MatchedTargets.push_back(*Target);
                }
            }

            std::sort(MatchedTargets.begin(), MatchedTargets.end());
            MatchedTargets.erase(
                std::unique(MatchedTargets.begin(), MatchedTargets.end()),
                MatchedTargets.end()
            );
            Statistic.matchedTargets = std::move(MatchedTargets);
            Statistic.elapsedNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - Start
            ).count();

            if (Progress != nullptr)
            {
                if (QualificationBar)
                {
                    std::atomic<size_t>& ResultCount = Statistic.qualified
                        ? Progress->qualifiedRules
                        : Progress->unqualifiedRules;
                    ResultCount.fetch_add(1, std::memory_order_relaxed);
                }
                Progress->completedEvaluations.fetch_add(
                    Statistic.evaluated,
                    std::memory_order_relaxed
                );
                Progress->completedRules.fetch_add(1, std::memory_order_release);
            }
        }
    };

    std::vector<std::thread> Workers;
    Workers.reserve(ThreadCount);
    for (size_t i = 0; i < ThreadCount; i++) Workers.emplace_back(Worker);
    for (std::thread& Thread : Workers) Thread.join();
}

SelectionResult
SelectTopRules(
    const std::span<RuleStatistic> Statistics,
    const size_t TargetCount,
    const SelectionOptions& Options
)
{
    SelectionResult Result;
    if (Statistics.empty() || TargetCount == 0 || Options.maximumRules == 0) return Result;

    for (RuleStatistic& Statistic : Statistics)
    {
        Statistic.rank = 0;
        Statistic.marginalGain = 0;
        Statistic.cumulativeTargets = 0;
    }

    std::priority_queue<Candidate, std::vector<Candidate>, CandidateLess> Candidates;
    for (size_t i = 0; i < Statistics.size(); i++)
    {
        const size_t Gain = Statistics[i].UniqueTargets();
        if (Gain >= Options.minimumGain)
        {
            Candidates.push({Gain, i, Statistics[i].sourceOrder});
        }
    }

    std::vector<uint8_t> Covered(TargetCount, 0);
    while (!Candidates.empty() && Result.ruleIndices.size() < Options.maximumRules)
    {
        const Candidate Current = Candidates.top();
        Candidates.pop();
        RuleStatistic& Statistic = Statistics[Current.ruleIndex];
        const size_t ActualGain = CountUncovered(Statistic, Covered);
        if (ActualGain < Options.minimumGain) continue;
        if (ActualGain != Current.gain)
        {
            Candidates.push({ActualGain, Current.ruleIndex, Current.sourceOrder});
            continue;
        }

        for (const uint32_t Target : Statistic.matchedTargets)
        {
            if (Target < Covered.size()) Covered[Target] = 1;
        }
        Result.coveredTargets += ActualGain;
        Statistic.rank = Result.ruleIndices.size() + 1;
        Statistic.marginalGain = ActualGain;
        Statistic.cumulativeTargets = Result.coveredTargets;
        Result.ruleIndices.push_back(Current.ruleIndex);

        const double Coverage = static_cast<double>(Result.coveredTargets)
            / static_cast<double>(TargetCount);
        if (Coverage >= Options.targetCoverage) break;
    }
    return Result;
}

}
