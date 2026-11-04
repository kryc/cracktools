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