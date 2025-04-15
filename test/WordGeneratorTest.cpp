#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <gmpxx.h>

#include "WordGenerator.hpp"

// Notation for comments is as follows:
//  <charset>[<index>] = <word>
// For example:
//  LOWER[0] = a
//  LOWER[1] = b
//  abc[0] = a    

TEST(WordGenerator, GenerateWordLowerBound64) {
    std::string charset = LOWER;
    uint64_t value = 0;
    auto result = WordGenerator::GenerateWord(value, charset);
    EXPECT_EQ(result, LOWER.substr(0, 1));
}

TEST(WordGenerator, GenerateWordLowerBound) {
    std::string charset = LOWER;
    mpz_class value = 0;
    auto result = WordGenerator::GenerateWord(value, charset);
    EXPECT_EQ(result, LOWER.substr(0, 1));
}

TEST(WordGenerator, GenerateWordUpperBound64) {
    std::string charset = LOWER;
    uint64_t value = LOWER.size() - 1;
    auto result = WordGenerator::GenerateWord(value, charset);
    EXPECT_EQ(result, LOWER.substr(LOWER.size() - 1, 1));
}

TEST(WordGenerator, GenerateWordUpperBound) {
    std::string charset = LOWER;
    mpz_class value = LOWER.size() - 1;
    auto result = WordGenerator::GenerateWord(value, charset);
    EXPECT_EQ(result, LOWER.substr(LOWER.size() - 1, 1));
}

// Test for wrapping to the next string
// LOWER[25] = z
// LOWER[26] = aa
TEST(WordGenerator, WordLengthTick64) {
    std::string charset = LOWER;
    uint64_t value = LOWER.size();
    auto result = WordGenerator::GenerateWord(value, charset);
    EXPECT_EQ(result, LOWER.substr(0, 1) + LOWER.substr(0, 1));
    value++;
    result = WordGenerator::GenerateWord(value, charset);
    EXPECT_EQ(result, LOWER.substr(1, 1) + LOWER.substr(0, 1));
}

TEST(WordGenerator, WordLengthTick) {
    std::string charset = LOWER;
    mpz_class value = LOWER.size();
    auto result = WordGenerator::GenerateWord(value, charset);
    EXPECT_EQ(result, LOWER.substr(0, 1) + LOWER.substr(0, 1));
    value++;
    result = WordGenerator::GenerateWord(value, charset);
    EXPECT_EQ(result, LOWER.substr(1, 1) + LOWER.substr(0, 1));
}

// Test for getting the word length index
// LOWER with length 1 = 0
// LOWER with length 2 = 26
TEST(WordGenerator, WordLengthIndex64) {
    std::string charset = LOWER;
    size_t wordLength = 1;
    auto result = WordGenerator::WordLengthIndex64(wordLength, charset);
    EXPECT_EQ(result, 0);
    wordLength++;
    result = WordGenerator::WordLengthIndex64(wordLength, charset);
    EXPECT_EQ(result, 26);
    wordLength++;
    result = WordGenerator::WordLengthIndex64(wordLength, charset);
    EXPECT_EQ(result, 702);
}

TEST(WordGenerator, WordLengthIndex) {
    std::string charset = LOWER;
    size_t wordLength = 1;
    auto result = WordGenerator::WordLengthIndex(wordLength, charset);
    EXPECT_EQ(result, 0);
    wordLength++;
    result = WordGenerator::WordLengthIndex(wordLength, charset);
    EXPECT_EQ(result, 26);
    wordLength++;
    result = WordGenerator::WordLengthIndex(wordLength, charset);
    EXPECT_EQ(result, 702);
}