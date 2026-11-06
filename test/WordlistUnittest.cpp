#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "DispatchQueue.hpp"
#include "Util.hpp"
#include "Wordlist.hpp"

namespace
{
class WordlistTest : public testing::Test
{
protected:
    void SetUp() override
    {
        static std::atomic<size_t> Next = 0;
        Directory = std::filesystem::temp_directory_path()
            / ("cracktools-wordlist-" + std::to_string(getpid()) + "-" + std::to_string(Next++));
        std::error_code Error;
        ASSERT_TRUE(std::filesystem::create_directory(Directory, Error));
        ASSERT_FALSE(Error);
    }

    void TearDown() override
    {
        std::error_code Error;
        std::filesystem::remove_all(Directory, Error);
    }

    void Add(const std::string& Contents)
    {
        const auto Path = Directory / std::to_string(Files.size());
        std::ofstream Output(Path, std::ios::binary);
        Output << Contents;
        Output.close();
        ASSERT_TRUE(Output.good());
        Files.push_back(Path);
    }

    std::string Run(const Wordlist::Operation Action, const size_t Threads = 4,
        const bool IncludeComments = false)
    {
        std::ostringstream Output;
        std::string Error;
        EXPECT_TRUE(Wordlist::Process(Files, {Action, Threads, IncludeComments}, Output, Error)) << Error;
        EXPECT_TRUE(Error.empty());
        return Output.str();
    }

    std::filesystem::path Directory;
    std::vector<std::filesystem::path> Files;
};
}

TEST_F(WordlistTest, SetOperationsCountDistinctFilesNotDuplicateLines)
{
    Add("a\nb\nb\nc\n$HEX[61]\n");
    Add("b\nc\nd\nd\n");
    Add("c\ne\ne\n");
    for (const size_t Threads : {size_t{1}, size_t{4}, size_t{0}})
    {
        EXPECT_EQ(Run(Wordlist::Operation::Union, Threads), "a\nb\nc\nd\ne\n");
        EXPECT_EQ(Run(Wordlist::Operation::Difference, Threads), "a\n");
        EXPECT_EQ(Run(Wordlist::Operation::Intersection, Threads), "c\n");
        EXPECT_EQ(Run(Wordlist::Operation::SymmetricDifference, Threads), "a\nc\nd\ne\n");
    }
}

TEST_F(WordlistTest, SingleFileSetOperationsDeduplicate)
{
    Add("b\na\na\n");
    for (const auto Action : {Wordlist::Operation::Union, Wordlist::Operation::Difference,
        Wordlist::Operation::Intersection, Wordlist::Operation::SymmetricDifference})
        EXPECT_EQ(Run(Action), "a\nb\n");
}

TEST_F(WordlistTest, ConcurrentInvocationsUseIndependentPools)
{
    Add("CAT'S\nRED-BLUE\n");
    std::vector<std::future<std::string>> Results;
    for (size_t i = 0; i < 8; ++i)
    {
        Results.push_back(std::async(std::launch::async, [&, i] {
            return Run(Wordlist::Operation::Clean, i % 2 == 0 ? 1 : 3);
        }));
    }
    for (auto& Result : Results)
    {
        ASSERT_EQ(Result.wait_for(std::chrono::seconds(10)), std::future_status::ready);
        EXPECT_EQ(Result.get(), "blue\ncats\nred\nredblue\n");
    }
}

TEST_F(WordlistTest, WorksInsideAnotherDispatcherWithTheSameName)
{
    Add("b\na\na\n");
    auto Existing = dispatch::CreateDispatcher("wordlist");
    ASSERT_TRUE(Existing);
    std::promise<std::string> Result;
    auto Done = Result.get_future();
    Existing->PostTask([&] { Result.set_value(Run(Wordlist::Operation::Union, 2)); });
    ASSERT_EQ(Done.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    EXPECT_EQ(Done.get(), "a\nb\n");
    EXPECT_EQ(dispatch::GetDispatcher("wordlist"), Existing);
    Existing->Stop();
    EXPECT_TRUE(Existing->Wait());
    dispatch::GlobalDispatcherWait();
}

TEST_F(WordlistTest, RepeatedInputPathsAreSeparateSetOperands)
{
    Add("a\nb\na\n");
    Files.push_back(Files.front());
    EXPECT_EQ(Run(Wordlist::Operation::Union), "a\nb\n");
    EXPECT_EQ(Run(Wordlist::Operation::Difference), "");
    EXPECT_EQ(Run(Wordlist::Operation::Intersection), "a\nb\n");
    EXPECT_EQ(Run(Wordlist::Operation::SymmetricDifference), "");
    Files.push_back(Files.front());
    EXPECT_EQ(Run(Wordlist::Operation::SymmetricDifference), "a\nb\n");
}

TEST_F(WordlistTest, MembershipWorksWithMoreThanSixtyFourInputs)
{
    for (size_t i = 0; i < 70; ++i)
        Add("common\ncommon\n" + std::string(i % 2 == 0 ? "even\neven\n" : "odd\nodd\n"));
    EXPECT_EQ(Run(Wordlist::Operation::Intersection, 16), "common\n");
    EXPECT_EQ(Run(Wordlist::Operation::SymmetricDifference, 16), "even\nodd\n");
    EXPECT_EQ(Run(Wordlist::Operation::Difference, 16), "");
}

TEST_F(WordlistTest, EmptyFileParticipatesInMembership)
{
    Add("a\nb\n");
    Add("");
    EXPECT_EQ(Run(Wordlist::Operation::Intersection), "");
    EXPECT_EQ(Run(Wordlist::Operation::Difference), "a\nb\n");
    EXPECT_EQ(Run(Wordlist::Operation::SymmetricDifference), "a\nb\n");
    Files.erase(Files.begin());
    EXPECT_EQ(Run(Wordlist::Operation::Union), "");
}

TEST_F(WordlistTest, NewlineFormsCommentsAndUnterminatedLastLine)
{
    Add("#skip\r\na\rb\n\r\nc\r#skip-too\r\nfinal");
    EXPECT_EQ(Run(Wordlist::Operation::Union), "\na\nb\nc\nfinal\n");
    EXPECT_EQ(Run(Wordlist::Operation::Union, 4, true),
        "\n#skip\n#skip-too\na\nb\nc\nfinal\n");
}

TEST_F(WordlistTest, EscapesBeforeSortingAndDoesNotStripHashesOrColons)
{
    Add("z\n$HEX[6100]\na:\n$HEX[ff]\n$HEX[zz]\n$HEX[0]\n$HEX[]\n\n"
        "5f4dcc3b5aa765d61d8327deb882cf99:password\n$HEX[23]\n$HEX[410A42]\n");
    EXPECT_EQ(Run(Wordlist::Operation::Union),
        "\n#\n$HEX[244845585b305d]\n$HEX[244845585b5d]\n$HEX[244845585b7a7a5d]\n"
        + Util::ForceHexlify("5f4dcc3b5aa765d61d8327deb882cf99:password")
        + "\n$HEX[410a42]\n$HEX[6100]\n$HEX[613a]\n$HEX[ff]\nz\n");
}

TEST_F(WordlistTest, CaseTransformsCombineFilesAndDeduplicateAfterTransform)
{
    Add("HELLO\nhello\n#COMMENT\n");
    Add("WORLD\n$HEX[68656c6c6f]\n");
    EXPECT_EQ(Run(Wordlist::Operation::Lower), "hello\nworld\n");
    EXPECT_EQ(Run(Wordlist::Operation::Upper), "HELLO\nWORLD\n");
    EXPECT_EQ(Run(Wordlist::Operation::Title), "Hello\nWorld\n");
    EXPECT_EQ(Run(Wordlist::Operation::Lower, 4, true), "#comment\nhello\nworld\n");
    EXPECT_EQ(Run(Wordlist::Operation::Upper, 4, true), "#COMMENT\nHELLO\nWORLD\n");
    EXPECT_EQ(Run(Wordlist::Operation::Title, 4, true), "#Comment\nHello\nWorld\n");
}

TEST_F(WordlistTest, UsesProjectStandardEscapingPolicy)
{
    Add("a \na:\n$HEX[4142]\nAB\n$HEX[zz]\n$HEX[]\n$HEX[7f]\n$HEX[ff]\n");
    EXPECT_EQ(Run(Wordlist::Operation::Union),
        "$HEX[244845585b5d]\n$HEX[244845585b7a7a5d]\n$HEX[6120]\n$HEX[613a]\n"
        "$HEX[7f]\n$HEX[ff]\nAB\n");
}

TEST_F(WordlistTest, TitleUsesAsciiLetterRuns)
{
    Add("hELLO-wORLD\no'NEILL\n123aBC\n");
    EXPECT_EQ(Run(Wordlist::Operation::Title), "123Abc\nHello-World\nO'Neill\n");
}

TEST_F(WordlistTest, ProjectEscapingAppliesToEveryOperation)
{
    Add("a:b\n$HEX[613a62]\n");
    for (const auto Action : {Wordlist::Operation::Union, Wordlist::Operation::Difference,
        Wordlist::Operation::Intersection, Wordlist::Operation::SymmetricDifference,
        Wordlist::Operation::Lower, Wordlist::Operation::Upper,
        Wordlist::Operation::Title, Wordlist::Operation::Clean})
    {
        const bool Upper = Action == Wordlist::Operation::Upper || Action == Wordlist::Operation::Title;
        EXPECT_EQ(Run(Action), Upper ? "$HEX[413a42]\n" : "$HEX[613a62]\n");
    }
}

TEST_F(WordlistTest, CleanPreservesStageOrderAndSingleCharacterFilter)
{
    Add("CAT'S\nRED-BLUE GREEN\n--\nX\n1\n12\n!\n!!\n\n#COMMENT\n");
    EXPECT_EQ(Run(Wordlist::Operation::Clean),
        "!!\n12\nblue\nbluegreen\ncats\ngreen\nred\nredblue\nredbluegreen\nx\n");
    EXPECT_EQ(Run(Wordlist::Operation::Clean, 4, true),
        "!!\n#comment\n12\nblue\nbluegreen\ncats\ngreen\nred\nredblue\nredbluegreen\nx\n");
}

TEST_F(WordlistTest, NonAsciiBytesArePreservedWithoutTransliteration)
{
    Add("$HEX[c389434f4c45]\n$HEX[ff4142]\n");
    EXPECT_EQ(Run(Wordlist::Operation::Lower), "$HEX[ff6162]\nÉcole\n");
    EXPECT_EQ(Run(Wordlist::Operation::Clean), "$HEX[ff6162]\nÉcole\n");
    EXPECT_EQ(Run(Wordlist::Operation::Upper), "$HEX[ff4142]\nÉCOLE\n");
}

TEST_F(WordlistTest, CleanDoesNotRepeatPossessiveConversionAfterSplitting)
{
    Add("FOO'S-BAR\nfoo's bar\nfoo bar\nfoo-bar\nfoo's\n");
    EXPECT_EQ(Run(Wordlist::Operation::Clean), "bar\nfoo\nfoo's\nfoo'sbar\nfoobar\nfoos\n");
}

TEST_F(WordlistTest, CrLfAtExactChunkBoundariesDoesNotCreateBlankLines)
{
    // With four workers this file uses the minimum 64 KiB chunk size.
    // Exercise boundaries on CR, on LF, and immediately after the CRLF pair.
    for (const size_t Padding : {size_t{65534}, size_t{65535}, size_t{65536}})
    {
        Files.clear();
        std::string Contents(Padding, 'a');
        Contents += "\r\n";
        for (size_t i = 0; i < 40000; ++i) Contents += "b\r\n";
        Contents += "last";
        Add(Contents);
        const auto Expected = std::string(Padding, 'a') + "\nb\nlast\n";
        EXPECT_EQ(Run(Wordlist::Operation::Union, 4), Expected);
        EXPECT_EQ(Run(Wordlist::Operation::Union, 1), Expected);
    }
}

TEST_F(WordlistTest, LargeSingleFileChunksLongLinesAndBatchTransfers)
{
    // A line longer than any chunk is followed by enough records to flush batches.
    std::string Contents(65535, 'a');
    Contents += "\r\n";
    Contents += std::string(4 * 1024 * 1024 + 5, 'z') + "\n";
    for (size_t i = 0; i < 20000; ++i) Contents += "word" + std::to_string(i % 1000) + "\n";
    Contents += "last";
    Add(Contents);
    const auto Reference = Run(Wordlist::Operation::Union, 1);
    EXPECT_EQ(Run(Wordlist::Operation::Union, 8), Reference);
    EXPECT_EQ(Run(Wordlist::Operation::Union, 3), Reference);
    EXPECT_EQ(static_cast<size_t>(std::count(Reference.begin(), Reference.end(), '\n')), 1003u);
    EXPECT_TRUE(Reference.starts_with(std::string(65535, 'a') + "\nlast\n"));
}

TEST_F(WordlistTest, MissingFilesDirectoriesAndMissingInputsFailWithoutOutput)
{
    std::ostringstream Output;
    std::string Error;
    EXPECT_FALSE(Wordlist::Process(Files, {}, Output, Error));
    EXPECT_FALSE(Error.empty());
    Files.push_back(Directory / "missing");
    EXPECT_FALSE(Wordlist::Process(Files, {}, Output, Error));
    EXPECT_NE(Error.find("missing"), std::string::npos);
    Files.front() = Directory;
    EXPECT_FALSE(Wordlist::Process(Files, {}, Output, Error));
    EXPECT_TRUE(Output.str().empty());
}

TEST_F(WordlistTest, FailedOutputStreamIsReported)
{
    Add("a\n");
    std::ostringstream Output;
    Output.setstate(std::ios::badbit);
    std::string Error;
    EXPECT_FALSE(Wordlist::Process(Files, {}, Output, Error));
    EXPECT_FALSE(Error.empty());
}

TEST_F(WordlistTest, NamedPipesAreRejectedWithoutWaitingForAWriter)
{
    const auto Path = Directory / "pipe";
    ASSERT_EQ(mkfifo(Path.c_str(), 0600), 0);
    Files.push_back(Path);
    std::ostringstream Output;
    std::string Error;
    EXPECT_FALSE(Wordlist::Process(Files, {}, Output, Error));
    EXPECT_FALSE(Error.empty());
    EXPECT_TRUE(Output.str().empty());
}

TEST(Wordlist, ParsesAllOperations)
{
    for (const std::string_view Name : {"union", "difference", "intersection", "symmetric-difference",
        "lower", "upper", "title", "clean"}) EXPECT_TRUE(Wordlist::ParseOperation(Name));
    EXPECT_FALSE(Wordlist::ParseOperation("unknown"));
}