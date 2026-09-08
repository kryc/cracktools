#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
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
    return {
        Rule,
        Rules::Compile(Rule),
        SourceOrder
    };
}

void
Analyze(
    const std::span<const std::string> Words,
    const std::span<RuleAnalysis::RuleStatistic> Statistics,
    const size_t Threads
)
{
    RuleAnalysis::WordLookup Lookup;
    Lookup.Initialize(Words);
    RuleAnalysis::Analyze(Words, Lookup, Statistics, Threads);
}

}

TEST(RuleAnalysis, ParsesWordlistFormats)
{
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("password"), "password");
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("deadbeef:password"), "password");
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("deadbeef:password\r"), "password");
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("$HEX[70617373]"), "pass");
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("deadbeef:$HEX[70617373]"), "pass");
    EXPECT_EQ(RuleAnalysis::ParseWordlistLine("user:password"), "user:password");
    EXPECT_EQ(
        RuleAnalysis::ParseWordlistLine("deadbeef:$HEX[610062]"),
        std::string("a\0b", 3)
    );
}

TEST(RuleAnalysis, SelectsRandomSampleWithoutReplacement)
{
    const std::vector<std::string> Words = {"a", "b", "c", "d", "e", "f"};

    std::vector<std::string> First = RuleAnalysis::RandomSample(Words, 3, 12345);
    std::vector<std::string> Second = RuleAnalysis::RandomSample(Words, 3, 12345);

    EXPECT_EQ(First, Second);
    EXPECT_EQ(First.size(), 3);

    std::vector<std::string> Sample = First;
    std::sort(Sample.begin(), Sample.end());
    EXPECT_EQ(std::unique(Sample.begin(), Sample.end()), Sample.end());
}

TEST(RuleAnalysis, SelectsGeneratedIndicesWithoutReplacement)
{
    const std::vector<size_t> First = RuleAnalysis::RandomSampleIndices(1000000, 1000, 12345);
    const std::vector<size_t> Second = RuleAnalysis::RandomSampleIndices(1000000, 1000, 12345);

    EXPECT_EQ(First, Second);
    EXPECT_EQ(First.size(), 1000);
    EXPECT_TRUE(std::all_of(First.begin(), First.end(), [](const size_t Index)
    {
        return Index < 1000000;
    }));

    std::vector<size_t> Sorted = First;
    std::sort(Sorted.begin(), Sorted.end());
    EXPECT_EQ(std::unique(Sorted.begin(), Sorted.end()), Sorted.end());
}

TEST(RuleAnalysis, BuildsTwoByteLookupRanges)
{
    std::vector<std::string> Words = {
        "",
        "a",
        "aa",
        "aa",
        "aaa",
        "aab",
        "ab",
        "b"
    };
    RuleAnalysis::WordLookup Lookup;
    Lookup.Initialize(Words);

    EXPECT_TRUE(Lookup.Contains(""));
    EXPECT_TRUE(Lookup.Contains("a"));
    EXPECT_TRUE(Lookup.Contains("aa"));
    EXPECT_TRUE(Lookup.Contains("aaa"));
    EXPECT_TRUE(Lookup.Contains("aab"));
    EXPECT_FALSE(Lookup.Contains("aac"));
    EXPECT_FALSE(Lookup.Contains("z"));

    const RuleAnalysis::WordRange& AaRange = Lookup.GetRange(0x6161);
    EXPECT_EQ(AaRange.offset, 2);
    EXPECT_EQ(AaRange.length, 4);
    EXPECT_EQ(Lookup.GetRange(0x6100).length, 1);
    EXPECT_EQ(Lookup.GetRange(0xffff).length, 0);
}

TEST(RuleAnalysis, IndexesBinaryPrefixes)
{
    std::vector<std::string> Words = {
        std::string("\x01\x02", 2),
        std::string("\xff\xfe", 2),
        std::string("\xff\xff", 2)
    };
    std::sort(Words.begin(), Words.end());
    RuleAnalysis::WordLookup Lookup;
    Lookup.Initialize(Words);

    EXPECT_TRUE(Lookup.Contains(std::string("\xff\xff", 2)));
    EXPECT_FALSE(Lookup.Contains(std::string("\xff\xfd", 2)));
    EXPECT_EQ(Lookup.GetRange(0xffff).length, 1);
}

TEST(RuleAnalysis, CountsChangedOutputsFoundInWordList)
{
    std::vector<std::string> Words = {"dog", "cats", "Cat", "dogs", "cat"};
    std::sort(Words.begin(), Words.end());

    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("$s", 0),
        Statistic("l", 1),
        Statistic("u", 2),
        Statistic(":", 3),
        Statistic("!a", 4)
    };

    Analyze(Words, Statistics, 3);

    EXPECT_EQ(Statistics[0].matches, 2);
    EXPECT_EQ(Statistics[0].evaluated, 5);
    EXPECT_EQ(Statistics[0].applied, 5);
    EXPECT_EQ(Statistics[0].changed, 5);
    EXPECT_EQ(Statistics[0].uniqueMatches, 2);
    EXPECT_EQ(Statistics[1].matches, 1);
    EXPECT_EQ(Statistics[1].changed, 1);
    EXPECT_EQ(Statistics[2].matches, 0);
    EXPECT_EQ(Statistics[3].matches, 0);
    EXPECT_EQ(Statistics[4].matches, 0);
    EXPECT_EQ(Statistics[4].rejected, 3);
    EXPECT_EQ(Statistics[4].applied, 2);
}

TEST(RuleAnalysis, UsesSeparateInputsAndLookupWords)
{
    const std::vector<std::string> Inputs = {"cat"};
    const std::vector<std::string> Words = {"cats"};
    RuleAnalysis::WordLookup Lookup;
    Lookup.Initialize(Words);
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("$s", 0)
    };

    RuleAnalysis::Analyze(Inputs, Lookup, Statistics, 1);

    EXPECT_EQ(Statistics[0].matches, 1);
}

TEST(RuleAnalysis, StopsEachRuleAfterConfiguredMatchLimit)
{
    const std::vector<std::string> Inputs = {"cat", "dog", "bird"};
    std::vector<std::string> Words = {"cats", "dogs", "birds"};
    std::sort(Words.begin(), Words.end());
    RuleAnalysis::WordLookup Lookup;
    Lookup.Initialize(Words);
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("$s", 0),
        Statistic("u", 1)
    };
    std::atomic<size_t> Completed = 0;
    std::atomic<size_t> Kept = 0;
    std::atomic<size_t> Dropped = 0;

    RuleAnalysis::Analyze(Inputs, Lookup, Statistics, 2, &Completed, 2, &Kept, &Dropped);

    EXPECT_EQ(Completed.load(), 2);
    EXPECT_EQ(Kept.load(), 1);
    EXPECT_EQ(Dropped.load(), 1);
    EXPECT_EQ(Statistics[0].evaluated, 2);
    EXPECT_EQ(Statistics[0].matches, 2);
    EXPECT_EQ(Statistics[0].uniqueMatches, 2);
    EXPECT_EQ(Statistics[1].evaluated, 3);
    EXPECT_EQ(Statistics[1].matches, 0);
}

TEST(RuleAnalysis, GeneratesInputsOnDemand)
{
    const std::vector<std::string> Words = {"a1"};
    RuleAnalysis::WordLookup Lookup;
    Lookup.Initialize(Words);
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("$1", 0)
    };

    RuleAnalysis::AnalyzeGenerated(2, {}, "ab", Lookup, Statistics, 1);

    EXPECT_EQ(Statistics[0].matches, 1);
}

TEST(RuleAnalysis, StopsGeneratedInputsAtConfiguredMatchLimit)
{
    std::vector<std::string> Words = {"a1", "b1"};
    std::sort(Words.begin(), Words.end());
    RuleAnalysis::WordLookup Lookup;
    Lookup.Initialize(Words);
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("$1", 0)
    };

    RuleAnalysis::AnalyzeGenerated(2, {}, "ab", Lookup, Statistics, 1, nullptr, 1);

    EXPECT_EQ(Statistics[0].evaluated, 1);
    EXPECT_EQ(Statistics[0].matches, 1);
}

TEST(RuleAnalysis, CountsDuplicateInputWords)
{
    std::vector<std::string> Words = {"cat", "cat", "cats"};
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("$s", 0)
    };

    Analyze(Words, Statistics, 2);

    EXPECT_EQ(Statistics[0].matches, 2);
    EXPECT_EQ(Statistics[0].uniqueMatches, 1);
}

TEST(RuleAnalysis, FindsPlainAndHexlifiedWords)
{
    std::vector<std::string> HexlifiedTarget = {
        "a",
        RuleAnalysis::ParseWordlistLine("$HEX[6101]")
    };
    std::sort(HexlifiedTarget.begin(), HexlifiedTarget.end());
    std::vector<RuleAnalysis::RuleStatistic> TargetStatistics = {
        Statistic("$\\x01", 0)
    };
    Analyze(HexlifiedTarget, TargetStatistics, 1);
    EXPECT_EQ(TargetStatistics[0].matches, 1);

    std::vector<std::string> HexlifiedInput = {
        RuleAnalysis::ParseWordlistLine("$HEX[636174]"),
        "cats"
    };
    std::sort(HexlifiedInput.begin(), HexlifiedInput.end());
    std::vector<RuleAnalysis::RuleStatistic> InputStatistics = {
        Statistic("$s", 0)
    };
    Analyze(HexlifiedInput, InputStatistics, 1);
    EXPECT_EQ(InputStatistics[0].matches, 1);
}

TEST(RuleAnalysis, SortsByMatches)
{
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("zero-a", 0),
        Statistic("two", 1),
        Statistic("one", 2),
        Statistic("zero-b", 3)
    };
    Statistics[0].matches = 0;
    Statistics[1].matches = 2;
    Statistics[2].matches = 1;
    Statistics[3].matches = 0;

    RuleAnalysis::Sort(
        Statistics,
        RuleAnalysis::SortKey::Matches,
        RuleAnalysis::SortOrder::Descending
    );
    EXPECT_EQ(Statistics[0].rule, "two");
    EXPECT_EQ(Statistics[1].rule, "one");
    EXPECT_EQ(Statistics[2].rule, "zero-a");
    EXPECT_EQ(Statistics[3].rule, "zero-b");

    RuleAnalysis::Sort(
        Statistics,
        RuleAnalysis::SortKey::Matches,
        RuleAnalysis::SortOrder::Ascending
    );
    EXPECT_EQ(Statistics[0].rule, "zero-a");
    EXPECT_EQ(Statistics[1].rule, "zero-b");
    EXPECT_EQ(Statistics[2].rule, "one");
    EXPECT_EQ(Statistics[3].rule, "two");
}

TEST(RuleAnalysis, SortsByRule)
{
    std::vector<RuleAnalysis::RuleStatistic> Statistics = {
        Statistic("u", 0),
        Statistic("$1", 1),
        Statistic("l", 2)
    };

    RuleAnalysis::Sort(
        Statistics,
        RuleAnalysis::SortKey::Rule,
        RuleAnalysis::SortOrder::Ascending
    );
    EXPECT_EQ(Statistics[0].rule, "$1");
    EXPECT_EQ(Statistics[1].rule, "l");
    EXPECT_EQ(Statistics[2].rule, "u");

    RuleAnalysis::Sort(
        Statistics,
        RuleAnalysis::SortKey::Rule,
        RuleAnalysis::SortOrder::Descending
    );
    EXPECT_EQ(Statistics[0].rule, "u");
    EXPECT_EQ(Statistics[1].rule, "l");
    EXPECT_EQ(Statistics[2].rule, "$1");
}
