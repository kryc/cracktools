#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "WordlistSort.hpp"

TEST(WordlistSort, ParsesPlainAndHexlifiedWords)
{
    EXPECT_EQ(WordlistSort::ParseLine("password"), "password");
    EXPECT_EQ(WordlistSort::ParseLine("$HEX[70617373]"), "pass");
    EXPECT_EQ(
        WordlistSort::ParseLine("$HEX[610062]"),
        std::string("a\0b", 3)
    );
    EXPECT_EQ(WordlistSort::ParseLine("password\r"), "password");
}

TEST(WordlistSort, AutomaticallyDropsLikelyHashPrefix)
{
    EXPECT_EQ(
        WordlistSort::ParseLine("5f4dcc3b5aa765d61d8327deb882cf99:$HEX[70617373]"),
        "pass"
    );
    EXPECT_EQ(
        WordlistSort::ParseLine("5f4dcc3b5aa765d61d8327deb882cf99:password"),
        "password"
    );
    EXPECT_EQ(
        WordlistSort::ParseLine("$6$salt$abcdefghijklmn:password"),
        "password"
    );
    EXPECT_EQ(
        WordlistSort::ParseLine("user:password"),
        "user:password"
    );
    EXPECT_EQ(
        WordlistSort::ParseLine("deadbeef:password"),
        "deadbeef:password"
    );
    EXPECT_EQ(WordlistSort::ParseLine("password"), "password");
}

TEST(WordlistSort, SortsDecodedBytesAndRehexlifiesOutput)
{
    std::vector<std::string> Words = {
        "z",
        "a:",
        "a",
        std::string("a\0", 2)
    };
    std::ostringstream Output;

    WordlistSort::Sort(Words);
    WordlistSort::Write(Words, Output);

    EXPECT_EQ(
        Output.str(),
        "a\n$HEX[6100]\n$HEX[613a]\nz\n"
    );
}

TEST(WordlistSort, SortsByLengthThenContent)
{
    std::vector<std::string> Words = {
        "bbb", "ab", "z", "aa", "", "a", "ab", "aaa"
    };

    WordlistSort::Sort(Words, true);

    const std::vector<std::string> Expected = {
        "", "a", "z", "aa", "ab", "ab", "aaa", "bbb"
    };
    EXPECT_EQ(Words, Expected);
}

TEST(WordlistSort, LengthSortUsesDecodedBytesAndRehexlifiesOutput)
{
    std::vector<std::string> Words = {
        WordlistSort::ParseLine("$HEX[613a]"),
        WordlistSort::ParseLine("$HEX[ff]"),
        WordlistSort::ParseLine("$HEX[6100]"),
        WordlistSort::ParseLine("z"),
        WordlistSort::ParseLine("$HEX[00]"),
        WordlistSort::ParseLine("aaa")
    };
    std::ostringstream Output;

    WordlistSort::Sort(Words, true);
    WordlistSort::Write(Words, Output);

    EXPECT_EQ(
        Output.str(),
        "$HEX[00]\nz\n$HEX[ff]\n$HEX[6100]\n$HEX[613a]\naaa\n"
    );
}

TEST(WordlistSort, LengthSortHandlesEmptyAndSingleWordLists)
{
    std::vector<std::string> Words;
    WordlistSort::Sort(Words, true);
    EXPECT_TRUE(Words.empty());

    Words.push_back("word");
    WordlistSort::Sort(Words, true);
    ASSERT_EQ(Words.size(), 1u);
    EXPECT_EQ(Words.front(), "word");
}

TEST(WordlistSort, PlainViewsReferenceOriginalInput)
{
    const std::string Line = "5f4dcc3b5aa765d61d8327deb882cf99:password\r";
    std::deque<std::string> DecodedWords;
    const std::string_view Word = WordlistSort::ParseLineView(Line, DecodedWords);

    EXPECT_EQ(Word, "password");
    EXPECT_EQ(Word.data(), std::string_view(Line).substr(33).data());
    EXPECT_TRUE(DecodedWords.empty());
    EXPECT_EQ(WordlistSort::ParseLineView("user:password", DecodedWords), "user:password");
    EXPECT_EQ(WordlistSort::ParseLineView("$HEX[zz]", DecodedWords), "$HEX[zz]");
    EXPECT_EQ(WordlistSort::ParseLineView("\r", DecodedWords), "");
    EXPECT_TRUE(DecodedWords.empty());
}

TEST(WordlistSort, DecodedViewsRemainStableWhenStorageGrows)
{
    std::deque<std::string> DecodedWords;
    const std::string_view Short = WordlistSort::ParseLineView("$HEX[610062]", DecodedWords);
    const std::string HexLine = "$HEX[" + std::string(200, '6') + "]";
    const std::string_view Long = WordlistSort::ParseLineView(HexLine, DecodedWords);
    for (size_t i = 0; i < 10000; ++i)
    {
        EXPECT_EQ(WordlistSort::ParseLineView("$HEX[ff]", DecodedWords), std::string("\xff", 1));
    }
    EXPECT_EQ(Short, std::string("a\0b", 3));
    EXPECT_EQ(Long, std::string(100, 'f'));
}

TEST(WordlistSort, ViewSortingMatchesOwnedSorting)
{
    const std::vector<std::string> Lines = {
        "z", "$HEX[6100]", "$HEX[ff]", "a:", "", "aaa", "aa", "aa",
        "5f4dcc3b5aa765d61d8327deb882cf99:$HEX[00]", "password\r", "$HEX[zz]"
    };
    for (const bool LengthFirst : {false, true})
    {
        std::deque<std::string> DecodedWords;
        std::vector<std::string_view> Views;
        std::vector<std::string> Owned;
        for (const auto& Line : Lines)
        {
            Views.push_back(WordlistSort::ParseLineView(Line, DecodedWords));
            Owned.push_back(WordlistSort::ParseLine(Line));
        }
        WordlistSort::Sort(Views, LengthFirst);
        WordlistSort::Sort(Owned, LengthFirst);
        std::ostringstream ViewOutput;
        std::ostringstream OwnedOutput;
        WordlistSort::Write(Views, ViewOutput);
        WordlistSort::Write(Owned, OwnedOutput);
        EXPECT_EQ(ViewOutput.str(), OwnedOutput.str());
    }

    std::vector<std::string_view> Empty;
    WordlistSort::Sort(Empty, true);
    std::ostringstream Output;
    WordlistSort::Write(Empty, Output);
    EXPECT_TRUE(Output.str().empty());
}

TEST(WordlistSort, UniqueOutputComparesDecodedWordsInBothSortingModes)
{
    const std::vector<std::string> Lines = {
        "$HEX[41]", "B", "A", "$HEX[4100]", "$HEX[4100]", "a", "", "",
        "$HEX[42]", "5f4dcc3b5aa765d61d8327deb882cf99:A", "$HEX[41]\r",
        "$HEX[413a]", "A:"
    };
    for (const bool LengthFirst : {false, true})
    {
        std::deque<std::string> DecodedWords;
        std::vector<std::string_view> Views;
        std::vector<std::string> Owned;
        for (const auto& Line : Lines)
        {
            Views.push_back(WordlistSort::ParseLineView(Line, DecodedWords));
            Owned.push_back(WordlistSort::ParseLine(Line));
        }
        WordlistSort::Sort(Views, LengthFirst);
        WordlistSort::Sort(Owned, LengthFirst);
        std::ostringstream ViewOutput;
        std::ostringstream OwnedOutput;
        WordlistSort::Write(Views, ViewOutput, true);
        WordlistSort::Write(Owned, OwnedOutput, true);
        const std::string Expected = LengthFirst
            ? "\nA\nB\na\n$HEX[4100]\n$HEX[413a]\n"
            : "\nA\n$HEX[4100]\n$HEX[413a]\nB\na\n";
        EXPECT_EQ(ViewOutput.str(), Expected);
        EXPECT_EQ(OwnedOutput.str(), Expected);
        // Deduplication happens only while writing, not by modifying the input.
        EXPECT_EQ(Views.size(), Lines.size());
        EXPECT_EQ(Owned.size(), Lines.size());
    }
}

TEST(WordlistSort, OutputPreservesDuplicatesByDefault)
{
    std::vector<std::string> Owned = {"", "", "A", "A"};
    std::vector<std::string_view> Views = {"", "", "A", "A"};
    std::ostringstream OwnedOutput;
    std::ostringstream ViewOutput;
    WordlistSort::Write(Owned, OwnedOutput);
    WordlistSort::Write(Views, ViewOutput);
    EXPECT_EQ(OwnedOutput.str(), "\n\nA\nA\n");
    EXPECT_EQ(ViewOutput.str(), "\n\nA\nA\n");
}

TEST(WordlistSort, UniqueOutputHandlesEmptyAndRepeatedWordLists)
{
    std::vector<std::string> Owned;
    std::vector<std::string_view> Views;
    std::ostringstream OwnedOutput;
    std::ostringstream ViewOutput;
    WordlistSort::Write(Owned, OwnedOutput, true);
    WordlistSort::Write(Views, ViewOutput, true);
    EXPECT_TRUE(OwnedOutput.str().empty());
    EXPECT_TRUE(ViewOutput.str().empty());

    Owned = {"", "", ""};
    Views = {"", "", ""};
    WordlistSort::Write(Owned, OwnedOutput, true);
    WordlistSort::Write(Views, ViewOutput, true);
    EXPECT_EQ(OwnedOutput.str(), "\n");
    EXPECT_EQ(ViewOutput.str(), "\n");
}

TEST(WordlistSort, WriteProgressReportsProcessedAndWrittenCounts)
{
    const std::vector<std::string> Owned = {"", "", "A", "A", "B"};
    const std::vector<std::string_view> Views = {"", "", "A", "A", "B"};
    const auto Check = [](const auto& Words, const bool Unique)
    {
        size_t Processed = 0;
        size_t Written = 0;
        size_t Calls = 0;
        std::ostringstream Output;
        WordlistSort::Write(Words, Output, Unique,
            [&](const size_t InputCount, const size_t OutputCount)
            {
                Processed = InputCount;
                Written = OutputCount;
                ++Calls;
            });
        EXPECT_EQ(Calls, 1u);
        EXPECT_EQ(Processed, Words.size());
        EXPECT_EQ(Written, Unique ? 3u : 5u);
        EXPECT_EQ(Output.str(), Unique ? "\nA\nB\n" : "\n\nA\nA\nB\n");
    };
    for (const bool Unique : {false, true})
    {
        Check(Owned, Unique);
        Check(Views, Unique);
    }
}

TEST(WordlistSort, WriteProgressUpdatesWhileSkippingDuplicateWords)
{
    const std::vector<std::string_view> Words(32769, "A");
    size_t Calls = 0;
    size_t Previous = 0;
    std::ostringstream Output;
    WordlistSort::Write(Words, Output, true,
        [&](const size_t Processed, const size_t Written)
        {
            EXPECT_GT(Processed, Previous);
            EXPECT_LE(Processed, Words.size());
            EXPECT_EQ(Written, 1u);
            Previous = Processed;
            ++Calls;
        });
    EXPECT_EQ(Calls, 3u);
    EXPECT_EQ(Previous, Words.size());
    EXPECT_EQ(Output.str(), "A\n");
}

TEST(WordlistSort, WriteProgressReportsEmptyAndFailedOutput)
{
    std::vector<std::string> Words;
    size_t Calls = 0;
    const WordlistSort::WriteProgress Progress = [&](const size_t Processed, const size_t Written)
    {
        EXPECT_EQ(Processed, 0u);
        EXPECT_EQ(Written, 0u);
        ++Calls;
    };
    std::ostringstream Output;
    WordlistSort::Write(Words, Output, false, Progress);
    EXPECT_EQ(Calls, 1u);
    EXPECT_TRUE(Output.str().empty());

    Words = {"A", "B"};
    Output.setstate(std::ios::badbit);
    WordlistSort::Write(Words, Output, false, Progress);
    EXPECT_EQ(Calls, 2u);
    EXPECT_TRUE(Output.str().empty());
}