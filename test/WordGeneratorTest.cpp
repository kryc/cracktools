#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include <gmpxx.h>

#include "WordGenerator.hpp"

// Notation for comments is as follows:
//  <LOWER>[<index>] = <word>
// For example:
//  LOWER[0] = a
//  LOWER[1] = b
//  abc[0] = a    

TEST(WordGenerator, GenerateWordLowerBound64) {
    uint64_t value = 0;
    auto result = WordGenerator::GenerateWord(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(0, 1));
}

TEST(WordGenerator, GenerateWordLowerBound) {
    mpz_class value = 0;
    auto result = WordGenerator::GenerateWord(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(0, 1));
}

TEST(WordGenerator, GenerateLowerBoundSpan64) {
    std::string result(21, ' ');
    auto length = WordGenerator::GenerateWord(std::span<char>(result), 0, LOWER);
    EXPECT_EQ(result.substr(0, length), LOWER.substr(0, 1));
}

TEST(WordGenerator, GenerateLowerBoundSpan) {
    std::string result(21, ' ');
    auto length = WordGenerator::GenerateWord(std::span<char>(result), mpz_class(0), LOWER);
    EXPECT_EQ(result.substr(0, length), LOWER.substr(0, 1));
}

TEST(WordGenerator, GenerateWordUpperBound64) {
    uint64_t value = LOWER.size() - 1;
    auto result = WordGenerator::GenerateWord(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(LOWER.size() - 1, 1));
}

TEST(WordGenerator, GenerateWordUpperBound) {
    mpz_class value = LOWER.size() - 1;
    auto result = WordGenerator::GenerateWord(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(LOWER.size() - 1, 1));
}

TEST(WordGenerator, GenerateUpperBoundSpan64) {
    std::string result(21, ' ');
    auto length = WordGenerator::GenerateWord(std::span<char>(result), LOWER.size() - 1, LOWER);
    EXPECT_EQ(result.substr(0, length), LOWER.substr(LOWER.size() - 1, 1));
}

TEST(WordGenerator, GenerateUpperBoundSpan) {
    std::string result(21, ' ');
    auto length = WordGenerator::GenerateWord(std::span<char>(result), mpz_class(LOWER.size() - 1), LOWER);
    EXPECT_EQ(result.substr(0, length), LOWER.substr(LOWER.size() - 1, 1));
}

// Test for wrapping to the next string
// LOWER[25] = z
// LOWER[26] = aa
TEST(WordGenerator, WordLengthTick64) {
    uint64_t value = LOWER.size();
    auto result = WordGenerator::GenerateWord(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(0, 1) + LOWER.substr(0, 1));
    value++;
    result = WordGenerator::GenerateWord(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(1, 1) + LOWER.substr(0, 1));
}

TEST(WordGenerator, WordLengthTick) {
    mpz_class value = LOWER.size();
    auto result = WordGenerator::GenerateWord(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(0, 1) + LOWER.substr(0, 1));
    value++;
    result = WordGenerator::GenerateWord(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(1, 1) + LOWER.substr(0, 1));
}

TEST(WordGenerator, WordLengthTickSpan64) {
    std::string result(21, ' ');
    auto length = WordGenerator::GenerateWord(std::span<char>(result), LOWER.size(), LOWER);
    EXPECT_EQ(result.substr(0, length), LOWER.substr(0, 1) + LOWER.substr(0, 1));
    length = WordGenerator::GenerateWord(std::span<char>(result), mpz_class(LOWER.size() + 1), LOWER);
    EXPECT_EQ(result.substr(0, length), LOWER.substr(1, 1) + LOWER.substr(0, 1));
}

TEST(WordGenerator, WordLengthTickSpan) {
    std::string result(21, ' ');
    auto length = WordGenerator::GenerateWord(std::span<char>(result), mpz_class(LOWER.size()), LOWER);
    EXPECT_EQ(result.substr(0, length), LOWER.substr(0, 1) + LOWER.substr(0, 1));
    length = WordGenerator::GenerateWord(std::span<char>(result), mpz_class(LOWER.size() + 1), LOWER);
    EXPECT_EQ(result.substr(0, length), LOWER.substr(1, 1) + LOWER.substr(0, 1));
}

TEST(WordGenerator, WordLengthTickReversed64) {
    uint64_t value = LOWER.size();
    auto result = WordGenerator::GenerateWordReversed(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(0, 1) + LOWER.substr(0, 1));
    value++;
    result = WordGenerator::GenerateWordReversed(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(0, 1) + LOWER.substr(1, 1));
}

TEST(WordGenerator, WordLengthTickReversed) {
    mpz_class value = LOWER.size();
    auto result = WordGenerator::GenerateWordReversed(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(0, 1) + LOWER.substr(0, 1));
    value++;
    result = WordGenerator::GenerateWordReversed(value, LOWER);
    EXPECT_EQ(result, LOWER.substr(0, 1) + LOWER.substr(1, 1));
}

// Test for getting the word length index
// LOWER with length 1 = 0
// LOWER with length 2 = 26
TEST(WordGenerator, WordLengthIndex64) {
    size_t wordLength = 1;
    auto result = WordGenerator::WordLengthIndex64(wordLength, LOWER);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(result, WordGenerator::Parse64("a", LOWER));
    wordLength++;
    result = WordGenerator::WordLengthIndex64(wordLength, LOWER);
    EXPECT_EQ(result, WordGenerator::Parse64("aa", LOWER));
    wordLength++;
    result = WordGenerator::WordLengthIndex64(wordLength, LOWER);
    EXPECT_EQ(result, WordGenerator::Parse64("aaa", LOWER));
}

TEST(WordGenerator, WordLengthIndex) {
    size_t wordLength = 1;
    auto result = WordGenerator::WordLengthIndex(wordLength, LOWER);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(result, WordGenerator::Parse("a", LOWER));
    wordLength++;
    result = WordGenerator::WordLengthIndex(wordLength, LOWER);
    EXPECT_EQ(result, WordGenerator::Parse("aa", LOWER));
    wordLength++;
    result = WordGenerator::WordLengthIndex(wordLength, LOWER);
    EXPECT_EQ(result, WordGenerator::Parse("aaa", LOWER));
}

TEST(WordGenerator, Parse64) {
    auto result = WordGenerator::Parse64("a", LOWER);
    EXPECT_EQ(result, 0);
    result = WordGenerator::Parse64("b", LOWER);
    EXPECT_EQ(result, 1);
}

TEST(WordGenerator, Parse) {
    auto result = WordGenerator::Parse("a", LOWER);
    EXPECT_EQ(result, 0);
    result = WordGenerator::Parse("b", LOWER);
    EXPECT_EQ(result, 1);
}

TEST(WordGenerator, Equality64) {
    auto lut = WordGenerator::GenerateParsingLookupTable(LOWER);
    for (size_t i = 0; i < 100; i++) {
        const uint64_t value = i;
        auto word = WordGenerator::GenerateWord(value, LOWER);
        EXPECT_EQ(value, WordGenerator::Parse64(word, LOWER));
        EXPECT_EQ(value, WordGenerator::Parse64(word, lut));
    }
    for (size_t i = 0; i < 5; i++) {
        const uint64_t value = i * 1000;
        auto word = WordGenerator::GenerateWord(value, LOWER);
        EXPECT_EQ(value, WordGenerator::Parse64(word, LOWER));
        EXPECT_EQ(value, WordGenerator::Parse64(word, lut));
    }
}

TEST(WordGenerator, Equality) {
    auto lut = WordGenerator::GenerateParsingLookupTable(LOWER);
    for (size_t i = 0; i < 100; i++) {
        const mpz_class value = i;
        auto word = WordGenerator::GenerateWord(value, LOWER);
        EXPECT_EQ(value, WordGenerator::Parse(word, LOWER));
        EXPECT_EQ(value, WordGenerator::Parse(word, lut));
    }
    for (size_t i = 0; i < 5; i++) {
        const mpz_class value = i * 1000;
        auto word = WordGenerator::GenerateWord(value, LOWER);
        EXPECT_EQ(value, WordGenerator::Parse(word, LOWER));
        EXPECT_EQ(value, WordGenerator::Parse(word, lut));
    }
}

TEST(WordGenerator, EqualityReversed64) {
    auto lut = WordGenerator::GenerateParsingLookupTable(LOWER);
    for (size_t i = 0; i < 100; i++) {
        const uint64_t value = i;
        auto word = WordGenerator::GenerateWordReversed(value, LOWER);
        EXPECT_EQ(value, WordGenerator::ParseReversed(word, LOWER));
        EXPECT_EQ(value, WordGenerator::ParseReversed(word, lut));
    }
    for (size_t i = 0; i < 5; i++) {
        const uint64_t value = i * 1000;
        auto word = WordGenerator::GenerateWordReversed(value, LOWER);
        EXPECT_EQ(value, WordGenerator::ParseReversed(word, LOWER));
        EXPECT_EQ(value, WordGenerator::ParseReversed(word, lut));
    }
}

TEST(WordGenerator, EqualityReversed) {
    auto lut = WordGenerator::GenerateParsingLookupTable(LOWER);
    for (size_t i = 0; i < 100; i++) {
        const mpz_class value = i;
        auto word = WordGenerator::GenerateWordReversed(value, LOWER);
        EXPECT_EQ(value, WordGenerator::ParseReversed(word, LOWER));
        EXPECT_EQ(value, WordGenerator::ParseReversed(word, lut));
    }
    for (size_t i = 0; i < 5; i++) {
        const mpz_class value = i * 1000;
        auto word = WordGenerator::GenerateWordReversed(value, LOWER);
        EXPECT_EQ(value, WordGenerator::ParseReversed(word, LOWER));
        EXPECT_EQ(value, WordGenerator::ParseReversed(word, lut));
    }
}