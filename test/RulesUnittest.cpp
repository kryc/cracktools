#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "Rules.hpp"

namespace
{

void
ExpectApplied(
    const std::string_view input,
    const std::string_view rule,
    const std::string_view expected
)
{
    const Rules::Result result = Rules::Apply(input, rule);
    ASSERT_EQ(result.status, Rules::Status::Applied) << "rule: " << rule;
    EXPECT_EQ(result.word, expected) << "rule: " << rule;
    EXPECT_EQ(result.errorOffset, std::string_view::npos);
}

void
ExpectRejected(const std::string_view input, const std::string_view rule)
{
    const Rules::Result result = Rules::Apply(input, rule);
    EXPECT_EQ(result.status, Rules::Status::Rejected) << "rule: " << rule;
}

void
ExpectSyntaxError(const std::string_view input, const std::string_view rule)
{
    const Rules::Result result = Rules::Apply(input, rule);
    EXPECT_EQ(result.status, Rules::Status::SyntaxError) << "rule: " << rule;
}

}

TEST(Rules, CompatibleCaseRules)
{
    ExpectApplied("p@ssW0rd", ":", "p@ssW0rd");
    ExpectApplied("p@ssW0rd", "l", "p@ssw0rd");
    ExpectApplied("p@ssW0rd", "u", "P@SSW0RD");
    ExpectApplied("p@ssW0rd", "c", "P@ssw0rd");
    ExpectApplied("p@ssW0rd", "C", "p@SSW0RD");
    ExpectApplied("p@ssW0rd", "t", "P@SSw0RD");
    ExpectApplied("p@ssW0rd", "T3", "p@sSW0rd");
    ExpectApplied("", "cCtT0", "");
}

TEST(Rules, CompatibleWholeWordRules)
{
    ExpectApplied("p@ssW0rd", "r", "dr0Wss@p");
    ExpectApplied("p@ssW0rd", "d", "p@ssW0rdp@ssW0rd");
    ExpectApplied("p@ssW0rd", "p2", "p@ssW0rdp@ssW0rdp@ssW0rd");
    ExpectApplied("p@ssW0rd", "f", "p@ssW0rddr0Wss@p");
    ExpectApplied("p@ssW0rd", "{", "@ssW0rdp");
    ExpectApplied("p@ssW0rd", "}", "dp@ssW0r");
    ExpectApplied("", "{}rdfq", "");
}

TEST(Rules, CompatibleCharacterAndTruncationRules)
{
    ExpectApplied("p@ssW0rd", "$1$2", "p@ssW0rd12");
    ExpectApplied("p@ssW0rd", "^2^1", "12p@ssW0rd");
    ExpectApplied("p@ssW0rd", "[", "@ssW0rd");
    ExpectApplied("p@ssW0rd", "]", "p@ssW0r");
    ExpectApplied("p@ssW0rd", "D3", "p@sW0rd");
    ExpectApplied("p@ssW0rd", "x04", "p@ss");
    ExpectApplied("p@ssW0rd", "O12", "psW0rd");
    ExpectApplied("p@ssW0rd", "i4!", "p@ss!W0rd");
    ExpectApplied("p@ssW0rd", "o3$", "p@s$W0rd");
    ExpectApplied("p@ssW0rd", "'6", "p@ssW0");
    ExpectApplied("abc", "D9x92O92i9!o9!'9", "abc");
}

TEST(Rules, CompatibleReplacementAndDuplicationRules)
{
    ExpectApplied("p@ssW0rd", "ss$", "p@$$W0rd");
    ExpectApplied("p@ssW0rd", "@s", "p@W0rd");
    ExpectApplied("p@ssW0rd", "z2", "ppp@ssW0rd");
    ExpectApplied("p@ssW0rd", "Z2", "p@ssW0rddd");
    ExpectApplied("p@ssW0rd", "q", "pp@@ssssWW00rrdd");
    ExpectApplied("", "z2Z2q", "");
}

TEST(Rules, MemoryRules)
{
    ExpectApplied("p@ssW0rd", "lMX428", "p@ssw0rdw0");
    ExpectApplied("p@ssW0rd", "uMl4", "p@ssw0rdP@SSW0RD");
    ExpectApplied("p@ssW0rd", "rMr6", "dr0Wss@pp@ssW0rd");
    ExpectRejected("p@ssW0rd", "MQ");
    ExpectApplied("p@ssW0rd", "MlQ", "p@ssw0rd");
    ExpectRejected("word", "4");
    ExpectRejected("word", "6");
    ExpectRejected("word", "X011");
    ExpectSyntaxError("word", "MX001");
}

TEST(Rules, LengthRejectRules)
{
    ExpectApplied("password", "<8", "password");
    ExpectRejected("password", "<7");
    ExpectApplied("password", ">8", "password");
    ExpectRejected("pass", ">8");
    ExpectApplied("password", "_8", "password");
    ExpectRejected("pass", "_8");
}

TEST(Rules, CharacterRejectRules)
{
    ExpectApplied("password", "!z", "password");
    ExpectRejected("password", "!s");
    ExpectApplied("password", "/s", "password");
    ExpectRejected("password", "/z");
    ExpectApplied("password", "(p", "password");
    ExpectRejected("password", "(a");
    ExpectApplied("password", ")d", "password");
    ExpectRejected("password", ")p");
    ExpectApplied("password", "=1a", "password");
    ExpectRejected("password", "=1z");
    ExpectApplied("password", "%2s", "password");
    ExpectRejected("password", "%3s");
    ExpectApplied("aaaa", "%4a", "aaaa");
    ExpectRejected("", "(a");
    ExpectRejected("", ")a");
}

TEST(Rules, SavedRejectPosition)
{
    ExpectApplied("Odessa77", "%2s Dp ip$", "Odes$a77");
    ExpectApplied("Odessa77", "/s Dp ip$", "Ode$sa77");
    ExpectApplied("p@s.sW0rd", "%2s Tp", "p@s.SW0rd");
    ExpectApplied("p@s.sW0rd", "%2s xp4", "sW0r");
    ExpectApplied("p@s.sW0rd", "%2s Op2", "p@s.0rd");
    ExpectApplied("p@s.sW0rd", "%2s op$", "p@s.$W0rd");
    ExpectApplied("p@s.sW0rd", "%2s 'p", "p@s.");
    ExpectSyntaxError("word", "Dp");
}

TEST(Rules, HashcatSpecificSwapAndByteRules)
{
    ExpectApplied("p@ssW0rd", "k", "@pssW0rd");
    ExpectApplied("p@ssW0rd", "K", "p@ssW0dr");
    ExpectApplied("p@ssW0rd", "*34", "p@sWs0rd");

    std::string shiftedLeft = "p@ssW0rd";
    shiftedLeft[2] = static_cast<char>(0xe6);
    ExpectApplied("p@ssW0rd", "L2", shiftedLeft);
    ExpectApplied("p@ssW0rd", "R2", "p@9sW0rd");
    ExpectApplied("p@ssW0rd", "+2", "p@tsW0rd");
    ExpectApplied("p@ssW0rd", "-1", "p?ssW0rd");
    ExpectApplied("p@ssW0rd", ".1", "psssW0rd");
    ExpectApplied("p@ssW0rd", ",1", "ppssW0rd");
}

TEST(Rules, HashcatSpecificBlockAndTitleRules)
{
    ExpectApplied("p@ssW0rd", "y2", "p@p@ssW0rd");
    ExpectApplied("p@ssW0rd", "Y2", "p@ssW0rdrd");
    ExpectApplied("p@ssW0rd w0rld", "E", "P@ssw0rd W0rld");
    ExpectApplied("p@ssW0rd-w0rld", "e-", "P@ssw0rd-W0rld");
    ExpectApplied("pass-word", "30-", "pass-Word");
    ExpectApplied("one-two-three", "31-", "one-two-Three");
}

TEST(Rules, CharacterClassMangleRules)
{
    const std::string input = "aB3fF! z";
    ExpectApplied(input, "~s?l_", "_B3_F! _");
    ExpectApplied(input, "~s?u_", "a_3f_! z");
    ExpectApplied(input, "~s?d_", "aB_fF! z");
    ExpectApplied(input, "~s?h_", "_B__F! z");
    ExpectApplied(input, "~s?H_", "a__f_! z");
    ExpectApplied(input, "~s?s_", "aB3fF__z");
    ExpectApplied("a?b", "~s?" "?!", "a!b");

    ExpectApplied(input, "~@?d", "aBfF! z");
    ExpectApplied("hELLO-wORLD", "~e?s", "Hello-World");
}

TEST(Rules, CharacterClassRejectRules)
{
    ExpectRejected("ab3", "~!?d");
    ExpectApplied("abc", "~!?d", "abc");
    ExpectApplied("ab3", "~/?d", "ab3");
    ExpectRejected("abc", "~/?d");
    ExpectApplied("Abc", "~(?u", "Abc");
    ExpectRejected("abc", "~(?u");
    ExpectApplied("ab3", "~)?d", "ab3");
    ExpectRejected("abc", "~)?d");
    ExpectApplied("aBc", "~=1?u", "aBc");
    ExpectRejected("abc", "~=1?u");
    ExpectApplied("a1b2", "~%2?d", "a1b2");
    ExpectRejected("a1bc", "~%2?d");
    ExpectApplied("1234", "~%4?d", "1234");
    ExpectApplied("a1b2", "~%2?d Dp", "a1b");
}

TEST(Rules, HexBytesWhitespaceAndComments)
{
    ExpectApplied("Penguin", "$\\x64", "Penguind");
    ExpectApplied("p@ss", "s\\x40\\x21", "p!ss");
    ExpectApplied("word", "$ ", "word ");
    ExpectApplied("Word", " l  $1 ", "word1");
    ExpectApplied("word", "# comment", "word");
    ExpectApplied("word", "   # comment", "word");
}

TEST(Rules, CompiledRules)
{
    const Rules::CompiledRule append = Rules::Compile("$\\x31");
    const Rules::Result appended = Rules::Apply("word", append);
    ASSERT_EQ(appended.status, Rules::Status::Applied);
    EXPECT_EQ(appended.word, "word1");

    const Rules::CompiledRule comment = Rules::Compile("  # comment");
    const Rules::Result unchanged = Rules::Apply("word", comment);
    ASSERT_EQ(unchanged.status, Rules::Status::Applied);
    EXPECT_EQ(unchanged.word, "word");
}

TEST(Rules, InvalidRulesAndLimits)
{
    ExpectSyntaxError("word", "?");
    ExpectSyntaxError("word", "T");
    ExpectSyntaxError("word", "Tz");
    ExpectSyntaxError("word", "~s?x_");
    ExpectSyntaxError("word", "~x?d");
    ExpectSyntaxError("word", std::string(Rules::MAX_OPERATIONS + 1, ':'));
    ExpectSyntaxError(std::string(Rules::MAX_PASSWORD_SIZE + 1, 'a'), ":");
}

TEST(Rules, SizeAndRangeBoundaries)
{
    const std::string maximumGrowingWord(Rules::MAX_PASSWORD_SIZE - 1, 'a');
    ExpectApplied(maximumGrowingWord, "$b", maximumGrowingWord);
    ExpectApplied("abc", "x92O92y9Y9*09L9R9+9-9.9,9", "abc");
    ExpectApplied("abc", "i3!", "abc!");
    ExpectApplied("abc", "'0", "");
}
