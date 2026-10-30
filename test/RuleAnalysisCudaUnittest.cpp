#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "RuleAnalysisCuda.hpp"
#include "Rules.hpp"

namespace
{

void CompareBackends(const std::optional<size_t> Bar)
{
    std::string Error;
    if (!RuleAnalysis::CudaAvailable(Error)) GTEST_SKIP() << Error;

    const std::vector<std::string> RuleTexts = {
        ":", "l", "u", "c", "C", "t", "T0", "r", "d", "p2", "f", "{", "}",
        "$1$2", "^a^b", "$\\x00", "^  ^ ", "[", "]", "D1", "x12", "O12",
        "i1!", "o1!", "'2", "sa!", "@a", "z2", "Z2", "q", "k", "K", "*01",
        "L0", "R0", "+0", "-0", ".0", ",1", "y2", "Y2", "E", "e-", "30-",
        "<3", ">3", "_3", "!a", "/a", "(a", ")a", "=1a", "%0a", "%2a",
        "/a Tp", "M$1X011", "~s?d!", "$", "unknown", "l$1r", "!x$1", "E$!"
    };
    std::vector<std::string> Inputs = {
        "", "a", "abc", "abc", "a-b-a", " a WORD", "aa", "123", "XYZ",
        std::string("a\0b", 3), std::string("\xff\x80", 2),
        std::string(127, 'a'), std::string(128, 'a'), std::string(253, 'a'),
        std::string(254, 'a'), std::string(255, 'a'), std::string(256, 'a'),
        std::string(257, 'a')
    };
    RuleAnalysis::TargetLookup Targets;
    std::vector<RuleAnalysis::RuleStatistic> Cpu;
    std::vector<RuleAnalysis::RuleStatistic> Gpu;
    for (size_t i = 0; i < RuleTexts.size(); ++i)
    {
        const auto Compiled = Rules::Compile(RuleTexts[i]);
        Cpu.emplace_back(RuleTexts[i], Compiled, i);
        Gpu.emplace_back(RuleTexts[i], Compiled, i);
        for (const auto& Input : Inputs)
        {
            const auto Result = Rules::Apply(Input, Compiled);
            if (Result.Succeeded()) (void)Targets.Add(Result.word);
        }
    }
    Targets.Finalize(4);
    RuleAnalysis::AnalysisProgress CpuProgress;
    RuleAnalysis::AnalysisProgress GpuProgress;
    RuleAnalysis::Analyze(Inputs, Targets, Cpu, 4, &CpuProgress, Bar);
    ASSERT_TRUE(RuleAnalysis::AnalyzeCuda(Inputs, Targets, Gpu, 4, &GpuProgress, Bar, Error)) << Error;
    for (size_t i = 0; i < Cpu.size(); ++i)
    {
        SCOPED_TRACE(RuleTexts[i]);
        EXPECT_EQ(Gpu[i].matchedTargets, Cpu[i].matchedTargets);
        EXPECT_EQ(Gpu[i].evaluated, Cpu[i].evaluated);
        EXPECT_EQ(Gpu[i].applied, Cpu[i].applied);
        EXPECT_EQ(Gpu[i].changed, Cpu[i].changed);
        EXPECT_EQ(Gpu[i].matches, Cpu[i].matches);
        EXPECT_EQ(Gpu[i].rejected, Cpu[i].rejected);
        EXPECT_EQ(Gpu[i].syntaxErrors, Cpu[i].syntaxErrors);
        EXPECT_EQ(Gpu[i].qualified, Cpu[i].qualified);
    }
    EXPECT_EQ(GpuProgress.completedRules.load(), CpuProgress.completedRules.load());
    EXPECT_EQ(GpuProgress.completedEvaluations.load(), CpuProgress.completedEvaluations.load());
    EXPECT_EQ(GpuProgress.qualifiedRules.load(), CpuProgress.qualifiedRules.load());
}

}

TEST(RuleAnalysisCuda, MatchesCpuExhaustive)
{
    CompareBackends(std::nullopt);
}

TEST(RuleAnalysisCuda, MatchesCpuQualification)
{
    CompareBackends(2);
}

TEST(RuleAnalysisCuda, QualificationCrossesInputChunkBoundary)
{
    std::string Error;
    if (!RuleAnalysis::CudaAvailable(Error)) GTEST_SKIP() << Error;
    std::vector<std::string> Inputs(32769, "a");
    Inputs.back() = "b";
    RuleAnalysis::TargetLookup Targets;
    ASSERT_TRUE(Targets.Add("a1"));
    ASSERT_TRUE(Targets.Add("b1"));
    Targets.Finalize();
    std::vector<RuleAnalysis::RuleStatistic> Statistics;
    Statistics.emplace_back("$1", Rules::Compile("$1"), 0);
    ASSERT_TRUE(RuleAnalysis::AnalyzeCuda(Inputs, Targets, Statistics, 1, nullptr, 2, Error)) << Error;
    EXPECT_TRUE(Statistics[0].qualified);
    EXPECT_EQ(Statistics[0].evaluated, Inputs.size());
    EXPECT_EQ(Statistics[0].matches, Inputs.size());
    EXPECT_EQ(Statistics[0].UniqueTargets(), 2);
}

TEST(RuleAnalysisCuda, BloomFalsePositivesDoNotCountAsMatches)
{
    std::string Error;
    if (!RuleAnalysis::CudaAvailable(Error)) GTEST_SKIP() << Error;
    RuleAnalysis::TargetLookup Targets;
    std::vector<std::string> Inputs;
    for (size_t i = 0; i < 2048; ++i)
    {
        const std::string Number = std::to_string(10000 + i);
        ASSERT_TRUE(Targets.Add("t" + Number + "!"));
        Inputs.push_back("m" + Number);
    }
    // Missing candidates come first; duplicate true hits must count once
    // toward qualification, regardless of filter saturation.
    Inputs.push_back("t10000");
    Inputs.push_back("t10000");
    Inputs.push_back("t10001");
    Targets.Finalize(4);
    RuleAnalysis::CudaOptions Options;
    Options.bloomBitsPerTarget = 1; // Eight probes make this almost saturated.
    for (const std::optional<size_t> Bar : {std::optional<size_t>{}, std::optional<size_t>{2}})
    {
        std::vector<RuleAnalysis::RuleStatistic> Cpu;
        Cpu.emplace_back("$!", Rules::Compile("$!"), 0);
        auto Gpu = Cpu;
        RuleAnalysis::Analyze(Inputs, Targets, Cpu, 1, nullptr, Bar);
        ASSERT_TRUE(RuleAnalysis::AnalyzeCuda(
            Inputs, Targets, Gpu, 1, nullptr, Bar, Error, Options
        )) << Error;
        EXPECT_EQ(Gpu[0].matches, 3);
        EXPECT_EQ(Gpu[0].UniqueTargets(), 2);
        EXPECT_EQ(Gpu[0].evaluated, Cpu[0].evaluated);
        EXPECT_EQ(Gpu[0].matchedTargets, Cpu[0].matchedTargets);
        EXPECT_EQ(Gpu[0].qualified, Cpu[0].qualified);
    }
}

TEST(RuleAnalysisCuda, StreamsTargetConstructionAcrossUploadChunks)
{
    std::string Error;
    if (!RuleAnalysis::CudaAvailable(Error)) GTEST_SKIP() << Error;
    constexpr size_t TargetCount = 1048577;
    RuleAnalysis::TargetLookup Targets;
    // Fixed-width numbers put all fingerprints into the same length bucket,
    // ensuring construction crosses the one-million-element staging boundary.
    for (size_t i = 0; i < TargetCount; ++i)
    {
        ASSERT_TRUE(Targets.Add("word" + std::to_string(10000000 + i) + "!"));
    }
    Targets.Finalize(4);
    const std::vector<std::string> Inputs = {
        "word10000000", "word11048576", "missing"
    };
    std::vector<RuleAnalysis::RuleStatistic> Cpu;
    Cpu.emplace_back("$!", Rules::Compile("$!"), 0);
    auto Gpu = Cpu;
    RuleAnalysis::Analyze(Inputs, Targets, Cpu, 1);
    ASSERT_TRUE(RuleAnalysis::AnalyzeCuda(
        Inputs, Targets, Gpu, 1, nullptr, std::nullopt, Error
    )) << Error;
    EXPECT_EQ(Gpu[0].matches, 2);
    EXPECT_EQ(Gpu[0].matchedTargets, Cpu[0].matchedTargets);
}

TEST(RuleAnalysisCuda, PipelinedBatchesMatchCpuWithMixedQualificationTimes)
{
    std::string Error;
    if (!RuleAnalysis::CudaAvailable(Error)) GTEST_SKIP() << Error;
    std::vector<std::string> Inputs;
    for (size_t i = 0; i < 65539; ++i) Inputs.push_back(std::to_string(i));
    Inputs[0] = "aa";
    Inputs[1] = "aa";
    Inputs[2] = "bb";
    Inputs[32765] = std::string(257, 'x');

    RuleAnalysis::TargetLookup Targets;
    std::vector<RuleAnalysis::RuleStatistic> Initial;
    for (size_t r = 0; r < 35; ++r)
    {
        const std::string Rule = "!0$" + std::string(1, static_cast<char>('!' + r));
        Initial.emplace_back(Rule, Rules::Compile(Rule), r);
        if (r % 3 == 2) continue; // Never qualifies.
        const size_t First = r % 3 == 0 ? 0 : 32767;
        const size_t Last = r % 3 == 0 ? 2 : 65538;
        ASSERT_TRUE(Targets.Add(Rules::Apply(Inputs[First], Rule).word));
        ASSERT_TRUE(Targets.Add(Rules::Apply(Inputs[Last], Rule).word));
    }
    Initial.emplace_back("M$!", Rules::Compile("M$!"), Initial.size());
    Targets.Finalize(4);
    for (const std::optional<size_t> Bar : {std::optional<size_t>{}, std::optional<size_t>{2}})
    {
        auto Cpu = Initial;
        auto Gpu = Initial;
        RuleAnalysis::AnalysisProgress CpuProgress;
        RuleAnalysis::AnalysisProgress GpuProgress;
        RuleAnalysis::Analyze(Inputs, Targets, Cpu, 4, &CpuProgress, Bar);
        ASSERT_TRUE(RuleAnalysis::AnalyzeCuda(
            Inputs, Targets, Gpu, 4, &GpuProgress, Bar, Error
        )) << Error;
        for (size_t r = 0; r < Cpu.size(); ++r)
        {
            SCOPED_TRACE(Cpu[r].rule);
            EXPECT_EQ(Gpu[r].matchedTargets, Cpu[r].matchedTargets);
            EXPECT_EQ(Gpu[r].evaluated, Cpu[r].evaluated);
            EXPECT_EQ(Gpu[r].applied, Cpu[r].applied);
            EXPECT_EQ(Gpu[r].changed, Cpu[r].changed);
            EXPECT_EQ(Gpu[r].matches, Cpu[r].matches);
            EXPECT_EQ(Gpu[r].rejected, Cpu[r].rejected);
            EXPECT_EQ(Gpu[r].syntaxErrors, Cpu[r].syntaxErrors);
            EXPECT_EQ(Gpu[r].qualified, Cpu[r].qualified);
        }
        EXPECT_EQ(GpuProgress.completedRules.load(), CpuProgress.completedRules.load());
        EXPECT_EQ(GpuProgress.completedEvaluations.load(), CpuProgress.completedEvaluations.load());
        EXPECT_EQ(GpuProgress.qualifiedRules.load(), CpuProgress.qualifiedRules.load());
        EXPECT_EQ(GpuProgress.unqualifiedRules.load(), CpuProgress.unqualifiedRules.load());
    }
}

TEST(RuleAnalysisCuda, CompactBuffersHandleEveryPairMatching)
{
    std::string Error;
    if (!RuleAnalysis::CudaAvailable(Error)) GTEST_SKIP() << Error;
    const std::vector<std::string> Inputs(4096, "a");
    RuleAnalysis::TargetLookup Targets;
    ASSERT_TRUE(Targets.Add("a!"));
    Targets.Finalize();
    std::vector<RuleAnalysis::RuleStatistic> Statistics;
    for (size_t r = 0; r < 35; ++r)
        Statistics.emplace_back("$!", Rules::Compile("$!"), r);
    ASSERT_TRUE(RuleAnalysis::AnalyzeCuda(
        Inputs, Targets, Statistics, 4, nullptr, std::nullopt, Error
    )) << Error;
    for (const auto& Result : Statistics)
    {
        EXPECT_EQ(Result.evaluated, Inputs.size());
        EXPECT_EQ(Result.matches, Inputs.size());
        EXPECT_EQ(Result.UniqueTargets(), 1);
    }
}
