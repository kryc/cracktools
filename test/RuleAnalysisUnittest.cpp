#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "RuleAnalysis.hpp"
#include "Rules.hpp"

namespace
{

RuleAnalysis::RuleStatistic
Statistic(
    const std::string& Rule,
    const size_t SourceOrder
)
{
    return {Rule, Rules::Compile(Rule), SourceOrder};
}

}

TEST(RuleAnalysis, ParsesWordlistFormats)
{
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("password"), "password");
    EXPECT_EQ(
        RuleAnalysis::ParseWordlistLine("5f4dcc3b5aa765d61d8327deb882cf99:password"),
        "password"
    );
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("user:password"), "user:password");
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("$HEX[70617373]"), "pass");
    EXPECT_EQ(
        RuleAnalysis::ParseWordlistLine("$HEX[610062]"),
        std::string("a\0b", 3)
    );
}

TEST(RuleAnalysis, SelectsRandomSamplesWithoutReplacement)
{
    const std::vector<std::string> Words = {"a", "b", "c", "d", "e", "f"};
    const std::vector<std::string> First = RuleAnalysis::RandomSample(Words, 3, 12345);
    const std::vector<std::string> Second = RuleAnalysis::RandomSample(Words, 3, 12345);

    EXPECT_EQ(First, Second);
    EXPECT_EQ(First.size(), 3);
    std::vector<std::string> Sorted = First;
    std::sort(Sorted.begin(), Sorted.end());
    EXPECT_EQ(std::unique(Sorted.begin(), Sorted.end()), Sorted.end());
}

TEST(RuleAnalysis, FindsStableTargetIdentifiers)
{
    const std::vector<std::string> Targets = {"", "a", "aa", "ab", "b"};
    RuleAnalysis::TargetLookup Lookup;
    for (const std::string& Target : Targets) EXPECT_TRUE(Lookup.Add(Target));
    EXPECT_TRUE(Lookup.Add("aa"));
    Lookup.Finalize(4);

    std::unordered_set<uint32_t> Identifiers;
    for (const std::string& Target : Targets)
    {
        const std::optional<uint32_t> Identifier = Lookup.Find(Target);
        ASSERT_TRUE(Identifier.has_value());
        Identifiers.insert(*Identifier);
        EXPECT_EQ(Lookup.Find(Target), Identifier);
    }
    EXPECT_EQ(Lookup.Size(), Targets.size());
    EXPECT_EQ(Identifiers.size(), Targets.size());
    EXPECT_EQ(Lookup.Find("missing"), std::nullopt);
    EXPECT_TRUE(Lookup.Contains("aa"));
    EXPECT_FALSE(Lookup.Contains("z"));
    EXPECT_NE(
        RuleAnalysis::TargetLookup::Fingerprint("aa"),
        RuleAnalysis::TargetLookup::Fingerprint("ab")
    );

    const RuleAnalysis::Hash128 Empty = RuleAnalysis::TargetLookup::Fingerprint("");
    EXPECT_EQ(Empty.high, 0x6c62272e07bb0142ULL);
    EXPECT_EQ(Empty.low, 0x62b821756295c58dULL);
    EXPECT_FALSE(Lookup.Add(std::string(Rules::MAX_PASSWORD_SIZE + 1, 'x')));
}

TEST(RuleAnalysis, RemovesTargetsAlreadyPresentInBaseInputs)
{
    const std::vector<std::string> Targets = {"cat", "cats", "dog", "dogs"};
    const std::vector<std::string> Inputs = {"cat", "dog", "dog"};
    RuleAnalysis::TargetLookup Lookup;
    for (const std::string& Target : Targets) EXPECT_TRUE(Lookup.Add(Target));
    Lookup.Finalize();

    const size_t Removed = Lookup.RemoveDirect(Inputs);

    EXPECT_EQ(Removed, 2);
    EXPECT_EQ(Lookup.Size(), 2);
    EXPECT_FALSE(Lookup.Contains("cat"));
    EXPECT_FALSE(Lookup.Contains("dog"));
    EXPECT_TRUE(Lookup.Contains("cats"));
    EXPECT_TRUE(Lookup.Contains("dogs"));
}

TEST(RuleAnalysis, CollectsUniqueTargetsAndOccurrenceStatistics)
{
    const std::vector<std::string> Inputs = {"cat", "dog", "cat"};
    const std::vector<std::string> Targets = {"cats", "dogs"};
    RuleAnalysis::TargetLookup Lookup;
    for (const std::string& Target : Targets) EXPECT_TRUE(Lookup.Add(Target));
    Lookup.Finalize();
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("$s", 0),
        Statistic("u", 1),
        Statistic("!a", 2)
    };
    RuleAnalysis::AnalysisProgress Progress;

    RuleAnalysis::Analyze(Inputs, Lookup, Statistics, 2, &Progress);

    EXPECT_EQ(Progress.completedRules.load(), 3);
    EXPECT_EQ(Progress.completedEvaluations.load(), 9);
    EXPECT_EQ(Statistics[0].evaluated, 3);
    EXPECT_EQ(Statistics[0].changed, 3);
    EXPECT_EQ(Statistics[0].matches, 3);
    EXPECT_EQ(Statistics[0].UniqueTargets(), 2);
    ASSERT_TRUE(Lookup.Find("cats").has_value());
    ASSERT_TRUE(Lookup.Find("dogs").has_value());
    EXPECT_NE(
        std::find(
            Statistics[0].matchedTargets.begin(),
            Statistics[0].matchedTargets.end(),
            *Lookup.Find("cats")
        ),
        Statistics[0].matchedTargets.end()
    );
    EXPECT_NE(
        std::find(
            Statistics[0].matchedTargets.begin(),
            Statistics[0].matchedTargets.end(),
            *Lookup.Find("dogs")
        ),
        Statistics[0].matchedTargets.end()
    );
    EXPECT_EQ(Statistics[1].matches, 0);
    EXPECT_EQ(Statistics[2].rejected, 2);
    EXPECT_EQ(Statistics[2].applied, 1);
}

TEST(RuleAnalysis, QualifiesEarlyUsingUniqueTargets)
{
    const std::vector<std::string> Inputs = {"cat", "cat", "dog", "bird"};
    const std::vector<std::string> Targets = {"cats", "dogs", "birds"};
    RuleAnalysis::TargetLookup Lookup;
    for (const std::string& Target : Targets) EXPECT_TRUE(Lookup.Add(Target));
    Lookup.Finalize();
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("$s", 0),
        Statistic("u", 1)
    };
    RuleAnalysis::AnalysisProgress Progress;

    RuleAnalysis::Analyze(Inputs, Lookup, Statistics, 2, &Progress, 2);

    EXPECT_TRUE(Statistics[0].qualified);
    EXPECT_EQ(Statistics[0].UniqueTargets(), 2);
    EXPECT_EQ(Statistics[0].matches, 3);
    EXPECT_EQ(Statistics[0].evaluated, 3);
    EXPECT_FALSE(Statistics[1].qualified);
    EXPECT_EQ(Statistics[1].evaluated, 4);
    EXPECT_EQ(Progress.completedRules.load(), 2);
    EXPECT_EQ(Progress.completedEvaluations.load(), 7);
    EXPECT_EQ(Progress.qualifiedRules.load(), 1);
    EXPECT_EQ(Progress.unqualifiedRules.load(), 1);
}

TEST(RuleAnalysis, SelectsRulesByMarginalTargetGain)
{
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("first", 0),
        Statistic("overlap", 1),
        Statistic("last", 2),
        Statistic("duplicate", 3)
    };
    Statistics[0].matchedTargets = {0, 1, 2};
    Statistics[1].matchedTargets = {1, 2, 3};
    Statistics[2].matchedTargets = {4};
    Statistics[3].matchedTargets = {0, 1, 2};

    RuleAnalysis::SelectionOptions Options;
    const RuleAnalysis::SelectionResult Result = RuleAnalysis::SelectTopRules(
        Statistics,
        5,
        Options
    );

    ASSERT_EQ(Result.ruleIndices.size(), 3);
    EXPECT_EQ(Result.ruleIndices[0], 0);
    EXPECT_EQ(Result.ruleIndices[1], 1);
    EXPECT_EQ(Result.ruleIndices[2], 2);
    EXPECT_EQ(Result.coveredTargets, 5);
    EXPECT_EQ(Statistics[0].marginalGain, 3);
    EXPECT_EQ(Statistics[0].cumulativeTargets, 3);
    EXPECT_EQ(Statistics[1].marginalGain, 1);
    EXPECT_EQ(Statistics[1].cumulativeTargets, 4);
    EXPECT_EQ(Statistics[2].marginalGain, 1);
    EXPECT_EQ(Statistics[2].cumulativeTargets, 5);
    EXPECT_EQ(Statistics[3].rank, 0);
}

TEST(RuleAnalysis, HonorsTopRuleGainAndCoverageBars)
{
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("first", 0),
        Statistic("overlap", 1),
        Statistic("last", 2)
    };
    Statistics[0].matchedTargets = {0, 1, 2};
    Statistics[1].matchedTargets = {1, 2, 3};
    Statistics[2].matchedTargets = {4};

    RuleAnalysis::SelectionOptions Options;
    Options.minimumGain = 2;
    RuleAnalysis::SelectionResult Result = RuleAnalysis::SelectTopRules(Statistics, 5, Options);
    ASSERT_EQ(Result.ruleIndices.size(), 1);
    EXPECT_EQ(Result.ruleIndices[0], 0);

    Options.minimumGain = 1;
    Options.maximumRules = 2;
    Result = RuleAnalysis::SelectTopRules(Statistics, 5, Options);
    EXPECT_EQ(Result.ruleIndices.size(), 2);
    EXPECT_EQ(Result.coveredTargets, 4);

    Options.maximumRules = 10;
    Options.targetCoverage = 0.6;
    Result = RuleAnalysis::SelectTopRules(Statistics, 5, Options);
    EXPECT_EQ(Result.ruleIndices.size(), 1);
    EXPECT_EQ(Result.coveredTargets, 3);
}
