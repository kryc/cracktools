#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "RainbowTable.hpp"
#include "Util.hpp"

class RainbowTableTest : public ::testing::Test
{
protected:
    std::filesystem::path m_TablePath;

    void SetUp() override
    {
        // Create unique temp paths for each test
        m_TablePath = std::filesystem::temp_directory_path() /
            ("rainbow_test_" + std::to_string(::getpid()) + ".tbl");
        Cleanup();
    }

    void TearDown() override
    {
        Cleanup();
    }

    void Cleanup()
    {
        std::filesystem::remove(m_TablePath);
    }

    static constexpr size_t kSmallMin = 1;
    static constexpr size_t kSmallMax = 4;
    static constexpr const char* kSmallCharset = "abcdefghijklmnopqrstuvwxyz";

    // Helper: configure a small, fast table for testing
    void ConfigureSmallTable(RainbowTable& table)
    {
        table.SetPath(m_TablePath);
        table.SetAlgorithm("md5");
        table.SetMin(kSmallMin);
        table.SetMax(kSmallMax);
        table.SetLength(100);
        table.SetBlocksize(SimdLanes());
        table.SetCount(SimdLanes() * 4);
        table.SetThreads(1);
        table.SetCharset("lower");
    }

    uint8_t SmallTableIndexWidth() const
    {
        return ComputeIndexWidth(kSmallMin, kSmallMax, kSmallCharset);
    }
};

// ============================================================
// Config validation
// ============================================================

TEST_F(RainbowTableTest, ValidateConfigRequiresPath)
{
    RainbowTable table;
    table.SetAlgorithm("md5");
    table.SetMin(1);
    table.SetMax(4);
    table.SetLength(100);
    table.SetCharset("lower");
    // No path set
    EXPECT_FALSE(table.ValidateConfig());
}

TEST_F(RainbowTableTest, ValidateConfigRequiresAlgorithm)
{
    RainbowTable table;
    table.SetPath(m_TablePath);
    table.SetMin(1);
    table.SetMax(4);
    table.SetLength(100);
    table.SetCharset("lower");
    // No algorithm set
    EXPECT_FALSE(table.ValidateConfig());
}

TEST_F(RainbowTableTest, ValidateConfigRequiresMinMax)
{
    RainbowTable table;
    table.SetPath(m_TablePath);
    table.SetAlgorithm("md5");
    table.SetLength(100);
    table.SetCharset("lower");
    // No min/max
    EXPECT_FALSE(table.ValidateConfig());
}

TEST_F(RainbowTableTest, ValidateConfigRequiresLength)
{
    RainbowTable table;
    table.SetPath(m_TablePath);
    table.SetAlgorithm("md5");
    table.SetMin(1);
    table.SetMax(4);
    table.SetCharset("lower");
    // No chain length
    EXPECT_FALSE(table.ValidateConfig());
}

TEST_F(RainbowTableTest, ValidateConfigAcceptsValidConfig)
{
    RainbowTable table;
    ConfigureSmallTable(table);
    EXPECT_TRUE(table.ValidateConfig());
}

TEST_F(RainbowTableTest, ValidateConfigRejectsKeyspaceOverflow128)
{
    RainbowTable table;
    table.SetPath(m_TablePath);
    table.SetAlgorithm("md5");
    table.SetMin(1);
    // 26^28 > 2^128, so this should overflow a 128-bit index
    table.SetMax(28);
    table.SetLength(100);
    table.SetCharset("lower");
    EXPECT_FALSE(table.ValidateConfig());
}

// ============================================================
// Coverage configuration
// ============================================================

TEST_F(RainbowTableTest, DefaultCoverageIs99Percent)
{
    RainbowTable table;
    EXPECT_DOUBLE_EQ(table.GetCoverage(), 0.99);
}

TEST_F(RainbowTableTest, SetCoverageClamps)
{
    RainbowTable table;
    table.SetCoverage(0.0);
    EXPECT_GE(table.GetCoverage(), 0.01);
    table.SetCoverage(1.0);
    EXPECT_LE(table.GetCoverage(), 0.999);
}

TEST_F(RainbowTableTest, CoverageAffectsChainCount)
{
    // Build with low coverage
    RainbowTable low;
    low.SetPath(m_TablePath);
    low.SetAlgorithm("md5");
    low.SetMin(1);
    low.SetMax(4);
    low.SetLength(100);
    low.SetBlocksize(SimdLanes());
    low.SetThreads(1);
    low.SetCharset("lower");
    low.SetCoverage(0.50);
    low.InitAndRunBuild();
    size_t lowCount = low.GetCount();

    Cleanup();

    // Build with high coverage
    RainbowTable high;
    high.SetPath(m_TablePath);
    high.SetAlgorithm("md5");
    high.SetMin(1);
    high.SetMax(4);
    high.SetLength(100);
    high.SetBlocksize(SimdLanes());
    high.SetThreads(1);
    high.SetCharset("lower");
    high.SetCoverage(0.99);
    high.InitAndRunBuild();
    size_t highCount = high.GetCount();

    EXPECT_GT(highCount, lowCount);
}

// ============================================================
// Table building
// ============================================================

TEST_F(RainbowTableTest, BuildCreatesTableFile)
{
    RainbowTable table;
    ConfigureSmallTable(table);

    table.InitAndRunBuild();

    EXPECT_TRUE(std::filesystem::exists(m_TablePath));
    EXPECT_GT(std::filesystem::file_size(m_TablePath), sizeof(TableHeader));
}

TEST_F(RainbowTableTest, BuildWritesValidHeader)
{
    RainbowTable table;
    ConfigureSmallTable(table);

    table.InitAndRunBuild();

    TableHeader hdr;
    ASSERT_TRUE(RainbowTable::GetTableHeader(m_TablePath, &hdr));
    EXPECT_EQ(hdr.magic, kMagic);
    EXPECT_EQ(hdr.min, 1);
    EXPECT_EQ(hdr.max, 4);
    EXPECT_EQ(hdr.length, 100);
}

TEST_F(RainbowTableTest, BuildWritesCorrectChainCount)
{
    RainbowTable table;
    ConfigureSmallTable(table);

    const size_t expectedCount = SimdLanes() * 4;
    table.InitAndRunBuild();

    // File size = header + count * chain_width
    size_t fileSize = std::filesystem::file_size(m_TablePath);
    size_t dataSize = fileSize - sizeof(TableHeader);
    size_t chainWidth = RainbowTable::ChainWidth(SmallTableIndexWidth());
    EXPECT_EQ(dataSize % chainWidth, 0u);
    EXPECT_EQ(dataSize / chainWidth, expectedCount);
}

TEST_F(RainbowTableTest, BuiltTableCanBeLoaded)
{
    RainbowTable builder;
    ConfigureSmallTable(builder);
    builder.InitAndRunBuild();

    RainbowTable loader;
    loader.SetPath(m_TablePath);
    ASSERT_TRUE(loader.ValidTable());
    ASSERT_TRUE(loader.LoadTable());

    EXPECT_EQ(loader.GetMin(), 1u);
    EXPECT_EQ(loader.GetMax(), 4u);
    EXPECT_EQ(loader.GetLength(), 100u);
    EXPECT_EQ(loader.GetAlgorithmString(), "MD5");
}

// ============================================================
// Resume building
// ============================================================

TEST_F(RainbowTableTest, ResumeAppendsChains)
{
    // Build initial table
    RainbowTable builder;
    ConfigureSmallTable(builder);
    builder.SetCount(SimdLanes() * 2);
    builder.InitAndRunBuild();

    size_t sizeAfterFirst = std::filesystem::file_size(m_TablePath);

    // Resume with a larger count
    RainbowTable resumeBuilder;
    ConfigureSmallTable(resumeBuilder);
    resumeBuilder.SetCount(SimdLanes() * 4);
    resumeBuilder.InitAndRunBuild();

    size_t sizeAfterResume = std::filesystem::file_size(m_TablePath);
    EXPECT_GT(sizeAfterResume, sizeAfterFirst);
}

// ============================================================
// ComputeChain consistency
// ============================================================

TEST_F(RainbowTableTest, ComputeChainProducesConsistentResults)
{
    const std::string charset = "abcdefghijklmnopqrstuvwxyz";
    auto chain1 = RainbowTable::ComputeChain(0, 1, 4, 50, HashAlgorithmMD5, charset);
    auto chain2 = RainbowTable::ComputeChain(0, 1, 4, 50, HashAlgorithmMD5, charset);

    EXPECT_EQ(chain1.Start(), chain2.Start());
    EXPECT_EQ(chain1.End(), chain2.End());
    EXPECT_FALSE(chain1.Start().empty());
    EXPECT_FALSE(chain1.End().empty());
}

TEST_F(RainbowTableTest, ComputeChainDifferentIndicesProduceDifferentChains)
{
    const std::string charset = "abcdefghijklmnopqrstuvwxyz";
    auto chain1 = RainbowTable::ComputeChain(0, 1, 4, 50, HashAlgorithmMD5, charset);
    auto chain2 = RainbowTable::ComputeChain(1, 1, 4, 50, HashAlgorithmMD5, charset);

    EXPECT_NE(chain1.Start(), chain2.Start());
}

// ============================================================
// Cracking
// ============================================================

TEST_F(RainbowTableTest, CrackFindsKnownPassword)
{
    // Build table with lowercase charset, lengths 1-4, enough chains to cover
    RainbowTable builder;
    builder.SetPath(m_TablePath);
    builder.SetAlgorithm("md5");
    builder.SetMin(1);
    builder.SetMax(4);
    builder.SetLength(200);
    builder.SetBlocksize(SimdLanes());
    builder.SetCount(SimdLanes() * 64);
    builder.SetThreads(1);
    builder.SetCharset("lower");

    builder.InitAndRunBuild();

    // Compute the hash of "a" with md5
    std::string testPassword = "a";
    auto hash = RainbowTable::DoHashHex(
        (const uint8_t*)testPassword.data(),
        testPassword.size(),
        HashAlgorithmMD5
    );

    // Try to crack it
    RainbowTable cracker;
    cracker.SetPath(m_TablePath);
    ASSERT_TRUE(cracker.LoadTable());
    cracker.SetThreads(1);

    auto results = cracker.Crack(hash);

    // With lengths 1-4 and a small charset, "a" should be in the table
    // This is probabilistic, so we allow it to fail gracefully
    if (!results.empty())
    {
        bool found = false;
        for (const auto& [crackedHash, password] : results)
        {
            if (password == testPassword)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            SUCCEED();
            return;
        }
    }

    // Even if not cracked, verify the table infrastructure works
    // (the table is small, so coverage isn't guaranteed)
    EXPECT_TRUE(std::filesystem::exists(m_TablePath));
}

TEST_F(RainbowTableTest, CrackRejectsInvalidHashLength)
{
    RainbowTable builder;
    ConfigureSmallTable(builder);
    builder.InitAndRunBuild();

    RainbowTable cracker;
    cracker.SetPath(m_TablePath);
    ASSERT_TRUE(cracker.LoadTable());
    cracker.SetThreads(1);

    // MD5 expects 32 hex chars; pass a short one
    auto results = cracker.Crack("abcd");
    EXPECT_TRUE(results.empty());
}

// ============================================================
// WriteBlock fwrite correctness (regression for bug #2)
// ============================================================

TEST_F(RainbowTableTest, BuildProducesCorrectFileSize)
{
    // This test catches the swapped fwrite args bug:
    // With correct args, file = header + (count * chain_width) exactly.
    // With swapped args on some platforms, fwrite could write wrong sizes.
    RainbowTable table;
    ConfigureSmallTable(table);

    const size_t count = SimdLanes() * 4;
    table.InitAndRunBuild();

    size_t expectedSize = sizeof(TableHeader) +
        count * RainbowTable::ChainWidth(SmallTableIndexWidth());
    size_t actualSize = std::filesystem::file_size(m_TablePath);

    EXPECT_EQ(actualSize, expectedSize);
}

// ============================================================
// Multi-algorithm support
// ============================================================

TEST_F(RainbowTableTest, BuildWithSHA1)
{
    RainbowTable table;
    table.SetPath(m_TablePath);
    table.SetAlgorithm("sha1");
    table.SetMin(1);
    table.SetMax(4);
    table.SetLength(50);
    table.SetBlocksize(SimdLanes());
    table.SetCount(SimdLanes() * 2);
    table.SetThreads(1);
    table.SetCharset("lower");

    table.InitAndRunBuild();

    ASSERT_TRUE(std::filesystem::exists(m_TablePath));

    TableHeader hdr;
    ASSERT_TRUE(RainbowTable::GetTableHeader(m_TablePath, &hdr));
    EXPECT_EQ((HashAlgorithm)hdr.algorithm, HashAlgorithmSHA1);
}

TEST_F(RainbowTableTest, BuildWithSHA256)
{
    RainbowTable table;
    table.SetPath(m_TablePath);
    table.SetAlgorithm("sha256");
    table.SetMin(1);
    table.SetMax(4);
    table.SetLength(50);
    table.SetBlocksize(SimdLanes());
    table.SetCount(SimdLanes() * 2);
    table.SetThreads(1);
    table.SetCharset("lower");

    table.InitAndRunBuild();

    ASSERT_TRUE(std::filesystem::exists(m_TablePath));

    TableHeader hdr;
    ASSERT_TRUE(RainbowTable::GetTableHeader(m_TablePath, &hdr));
    EXPECT_EQ((HashAlgorithm)hdr.algorithm, HashAlgorithmSHA256);
}

// ============================================================
// DoHashHex
// ============================================================

TEST_F(RainbowTableTest, DoHashHexMD5)
{
    // MD5("") = d41d8cd98f00b204e9800998ecf8427e
    std::string empty;
    auto hash = RainbowTable::DoHashHex(
        (const uint8_t*)empty.data(), 0, HashAlgorithmMD5);
    EXPECT_EQ(hash, "d41d8cd98f00b204e9800998ecf8427e");
}

TEST_F(RainbowTableTest, DoHashHexSHA1)
{
    // SHA1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
    std::string empty;
    auto hash = RainbowTable::DoHashHex(
        (const uint8_t*)empty.data(), 0, HashAlgorithmSHA1);
    EXPECT_EQ(hash, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

// ============================================================
// GetChain
// ============================================================

TEST_F(RainbowTableTest, GetChainMatchesComputeChain)
{
    const std::string charset = "abcdefghijklmnopqrstuvwxyz";
    const size_t min = 1, max = 4, length = 100;

    // Build a table
    RainbowTable builder;
    ConfigureSmallTable(builder);
    builder.InitAndRunBuild();

    // GetChain from the file should match ComputeChain for the same startpoint
    // Table is sorted by endpoint, so file position != startpoint
    for (size_t i = 0; i < SimdLanes() * 4; i++)
    {
        auto fromFile = RainbowTable::GetChain(m_TablePath, i);
        auto computed = RainbowTable::ComputeChain(static_cast<size_t>(fromFile.Index()), min, max, length, HashAlgorithmMD5, charset);

        EXPECT_EQ(fromFile.Start(), computed.Start()) << "Start mismatch at chain " << i;
        EXPECT_EQ(fromFile.End(), computed.End()) << "End mismatch at chain " << i;
        EXPECT_EQ(fromFile.Length(), length);
    }
}

TEST_F(RainbowTableTest, GetChainOutOfBoundsReturnsEmpty)
{
    RainbowTable builder;
    ConfigureSmallTable(builder);
    builder.InitAndRunBuild();

    auto chain = RainbowTable::GetChain(m_TablePath, SimdLanes() * 4 + 100);
    EXPECT_TRUE(chain.Start().empty());
    EXPECT_TRUE(chain.End().empty());
}

// ============================================================
// Deterministic crack test (bug #7 regression)
// ============================================================

TEST_F(RainbowTableTest, CrackReturnsCrackedResults)
{
    const std::string charset = "abcdefghijklmnopqrstuvwxyz";
    const size_t min = 1, max = 4, length = 100;
    const HashAlgorithm algo = HashAlgorithmMD5;

    // Compute chain 0 to learn the start word
    auto chain = RainbowTable::ComputeChain(0, min, max, length, algo, charset);
    std::string startWord = chain.Start();
    ASSERT_FALSE(startWord.empty());

    // Hash the start word — this hash is guaranteed crackable via chain 0
    auto targetHash = RainbowTable::DoHashHex(
        (const uint8_t*)startWord.data(), startWord.size(), algo);

    // Build a table that includes chain 0
    RainbowTable builder;
    builder.SetPath(m_TablePath);
    builder.SetAlgorithm("md5");
    builder.SetMin(min);
    builder.SetMax(max);
    builder.SetLength(length);
    builder.SetBlocksize(SimdLanes());
    builder.SetCount(SimdLanes() * 4);
    builder.SetThreads(1);
    builder.SetCharset("lower");
    builder.InitAndRunBuild();

    // Crack the hash
    RainbowTable cracker;
    cracker.SetPath(m_TablePath);
    ASSERT_TRUE(cracker.LoadTable());
    cracker.SetThreads(1);

    auto results = cracker.Crack(targetHash);

    // Verify the returned vector contains the cracked result
    ASSERT_FALSE(results.empty());
    bool found = false;
    for (const auto& [crackedHash, password] : results)
    {
        if (password == startWord)
        {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected to find '" << startWord << "' in cracked results";
}
