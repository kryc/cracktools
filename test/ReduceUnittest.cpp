#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "Reduce.hpp"

template<size_t N>
static void FillRandom(std::array<uint8_t, N>& buf, std::mt19937_64& rng)
{
    for (size_t i = 0; i < N; i++)
    {
        if (i % 8 == 0) { uint64_t r = rng(); buf[i] = static_cast<uint8_t>(r); }
        else { buf[i] = static_cast<uint8_t>(rng() >> ((i % 8) * 8)); }
    }
}

TEST(Reduction, GetBitsRequiredASCII) {
    uint8_t mask = 0;
    auto bitsRequired = CalculateBitsRequired(96, &mask);
    EXPECT_EQ(bitsRequired, 7);
    EXPECT_EQ(mask, 0x7F);
}

TEST(Reduction, GetBitsRequiredLower) {
    uint8_t mask = 0;
    auto bitsRequired = CalculateBitsRequired(26, &mask);
    EXPECT_EQ(bitsRequired, 5);
    EXPECT_EQ(mask, 0x1F);
}

TEST(Reduction, LoadBytesToIndex) {
    std::vector<uint8_t> hash = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    index_t value = LoadBytesToIndex(hash, 0, hash.size());
    EXPECT_EQ(value, 0x0102030405060708);

    value = LoadBytesToIndex(hash, 0, 4);
    EXPECT_EQ(value, 0x01020304);

    value = LoadBytesToIndex(hash, 2, 4);
    EXPECT_EQ(value, 0x03040506);

    value = LoadBytesToIndex(hash, 0, 3);
    EXPECT_EQ(value, 0x010203);
}

// ============================================================
// Reducer distribution tests
// ============================================================

// Feed random hashes into the reducer and measure how many
// unique words are produced across multiple iterations.
// This catches bias / dead spots in the reduction function.
TEST(Reduction, HybridReducerDistributionLower)
{
    constexpr size_t kMin = 1;
    constexpr size_t kMax = 4;
    const std::string charset = "abcdefghijklmnopqrstuvwxyz";
    constexpr size_t kHashSize = 20; // SHA-1 sized

    HybridReducer reducer(kMin, kMax, charset);

    // Total keyspace: 26 + 26^2 + 26^3 + 26^4 = 475254
    __uint128_t keyspace = WordGenerator::WordLengthIndex128(kMax + 1, charset)
                         - WordGenerator::WordLengthIndex128(kMin, charset);
    size_t N = static_cast<size_t>(keyspace);

    // Generate mt = 5N random reductions — should give ~99.3% coverage
    size_t numIterations = 100;
    size_t totalReductions = N * 5;
    size_t reductionsPerIteration = totalReductions / numIterations;

    std::unordered_set<std::string> uniqueWords;
    uniqueWords.reserve(N);

    std::mt19937_64 rng(42); // fixed seed for reproducibility
    std::array<char, 32> reduced;
    std::array<uint8_t, kHashSize> hashBuf;

    for (size_t iter = 0; iter < numIterations; iter++)
    {
        for (size_t i = 0; i < reductionsPerIteration; i++)
        {
            FillRandom(hashBuf, rng);

            size_t len = reducer.Reduce(reduced, hashBuf, iter);
            uniqueWords.emplace(reduced.data(), len);
        }
    }

    double coveragePercent = (100.0 * uniqueWords.size()) / N;

    // With 5N random draws into N bins, expect ~99.3% unique bins.
    // Allow some margin — fail if below 95%.
    std::cerr << "Lower charset distribution: " << uniqueWords.size()
              << "/" << N << " unique words ("
              << coveragePercent << "%)" << std::endl;

    EXPECT_GE(coveragePercent, 95.0)
        << "Reducer is producing too few unique words — possible bias";
}

TEST(Reduction, HybridReducerDistributionAscii)
{
    constexpr size_t kMin = 1;
    constexpr size_t kMax = 3;
    const std::string charset =
        " !\"#$%&'()*+,-./0123456789:;<=>?@"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        "abcdefghijklmnopqrstuvwxyz{|}~";
    constexpr size_t kHashSize = 20;

    HybridReducer reducer(kMin, kMax, charset);

    __uint128_t keyspace = WordGenerator::WordLengthIndex128(kMax + 1, charset)
                         - WordGenerator::WordLengthIndex128(kMin, charset);
    size_t N = static_cast<size_t>(keyspace);

    // 95 + 95^2 + 95^3 = 863070
    // Generate mt = 5N reductions
    size_t numIterations = 100;
    size_t totalReductions = N * 5;
    size_t reductionsPerIteration = totalReductions / numIterations;

    std::unordered_set<std::string> uniqueWords;
    uniqueWords.reserve(N);

    std::mt19937_64 rng(123);
    std::array<char, 32> reduced;
    std::array<uint8_t, kHashSize> hashBuf;

    for (size_t iter = 0; iter < numIterations; iter++)
    {
        for (size_t i = 0; i < reductionsPerIteration; i++)
        {
            FillRandom(hashBuf, rng);

            size_t len = reducer.Reduce(reduced, hashBuf, iter);
            uniqueWords.emplace(reduced.data(), len);
        }
    }

    double coveragePercent = (100.0 * uniqueWords.size()) / N;

    std::cerr << "ASCII charset distribution: " << uniqueWords.size()
              << "/" << N << " unique words ("
              << coveragePercent << "%)" << std::endl;

    EXPECT_GE(coveragePercent, 95.0)
        << "Reducer is producing too few unique words — possible bias";
}

// Check that different iterations produce different outputs
// for the same hash input (no iteration-0 degeneracy)
TEST(Reduction, HybridReducerIterationDiversity)
{
    constexpr size_t kMin = 1;
    constexpr size_t kMax = 4;
    const std::string charset = "abcdefghijklmnopqrstuvwxyz";
    constexpr size_t kHashSize = 20;

    HybridReducer reducer(kMin, kMax, charset);

    // Fixed hash input
    std::array<uint8_t, kHashSize> hashBuf;
    for (size_t b = 0; b < kHashSize; b++) hashBuf[b] = static_cast<uint8_t>(b * 17 + 3);

    std::unordered_set<std::string> results;
    std::array<char, 32> reduced;

    for (size_t iter = 0; iter < 1000; iter++)
    {
        size_t len = reducer.Reduce(reduced, hashBuf, iter);
        results.emplace(reduced.data(), len);
    }

    // 1000 iterations of the same hash should produce mostly unique words.
    // Anything below 900 unique / 1000 would indicate weak iteration mixing.
    std::cerr << "Iteration diversity: " << results.size()
              << "/1000 unique words" << std::endl;

    EXPECT_GE(results.size(), 900u)
        << "Reducer iterations are not producing diverse enough outputs";
}

// Check length distribution matches expected proportions
TEST(Reduction, HybridReducerLengthDistribution)
{
    constexpr size_t kMin = 1;
    constexpr size_t kMax = 4;
    const std::string charset = "abcdefghijklmnopqrstuvwxyz";
    constexpr size_t kHashSize = 20;
    constexpr size_t kSamples = 1000000;

    HybridReducer reducer(kMin, kMax, charset);

    std::array<size_t, 5> lengthCounts = {};
    std::mt19937_64 rng(77);
    std::array<char, 32> reduced;
    std::array<uint8_t, kHashSize> hashBuf;

    for (size_t i = 0; i < kSamples; i++)
    {
        FillRandom(hashBuf, rng);
        size_t len = reducer.Reduce(reduced, hashBuf, i % 100);
        lengthCounts[len]++;
    }

    // Expected proportions: 26^L / total
    // total = 26 + 676 + 17576 + 456976 = 475254
    // len 1: 26/475254 = 0.0055%
    // len 2: 676/475254 = 0.142%
    // len 3: 17576/475254 = 3.699%
    // len 4: 456976/475254 = 96.154%
    __uint128_t total = WordGenerator::WordLengthIndex128(kMax + 1, charset)
                      - WordGenerator::WordLengthIndex128(kMin, charset);

    std::cerr << "Length distribution (1M samples):" << std::endl;
    for (size_t l = kMin; l <= kMax; l++)
    {
        __uint128_t subKeyspace = WordGenerator::WordLengthIndex128(l + 1, charset)
                                - WordGenerator::WordLengthIndex128(l, charset);
        double expected = static_cast<double>(subKeyspace) / static_cast<double>(total);
        double actual = static_cast<double>(lengthCounts[l]) / kSamples;
        std::cerr << "  len " << l << ": expected " << (expected * 100)
                  << "%, actual " << (actual * 100) << "%" << std::endl;

        // Allow 50% relative tolerance for lengths with a reasonable sample count
        if (expected * kSamples > 100)
        {
            EXPECT_NEAR(actual, expected, expected * 0.5)
                << "Length " << l << " distribution is off";
        }
    }
}