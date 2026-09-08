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