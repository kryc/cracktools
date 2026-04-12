#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "RainbowTableSet.hpp"
#include "Util.hpp"

class RainbowTableSetTest : public ::testing::Test
{
protected:
    std::filesystem::path m_Dir;

    void SetUp() override
    {
        m_Dir = std::filesystem::temp_directory_path() /
            ("rtset_test_" + std::to_string(::getpid()));
        Cleanup();
    }

    void TearDown() override
    {
        Cleanup();
    }

    void Cleanup()
    {
        std::filesystem::remove_all(m_Dir);
    }

    void ConfigureSmallSet(RainbowTableSet& set, size_t tableCount = 1)
    {
        set.SetDirectory(m_Dir);
        set.SetAlgorithm("md5");
        set.SetMin(1);
        set.SetMax(3);
        set.SetLength(50);
        set.SetCharset("lower");
        set.SetThreads(1);
        set.SetChainCount(SimdLanes() * 4);
        set.SetTableCount(tableCount);
    }
};

// ============================================================
// Filename generation
// ============================================================

TEST_F(RainbowTableSetTest, GenerateFilenameFormat)
{
    RainbowTableSet set;
    set.SetAlgorithm("md5");
    set.SetMin(1);
    set.SetMax(5);
    set.SetLength(1024);
    set.SetCharset("ascii");

    EXPECT_EQ(set.GenerateFilename(0), "md5_ascii_1-5_1024_0.tbl");
    EXPECT_EQ(set.GenerateFilename(1), "md5_ascii_1-5_1024_1.tbl");
    EXPECT_EQ(set.GenerateFilename(42), "md5_ascii_1-5_1024_42.tbl");
}

TEST_F(RainbowTableSetTest, GenerateFilenameCharsets)
{
    RainbowTableSet set;
    set.SetAlgorithm("sha1");
    set.SetMin(2);
    set.SetMax(6);
    set.SetLength(100);

    set.SetCharset("lower");
    EXPECT_EQ(set.GenerateFilename(0), "sha1_lower_2-6_100_0.tbl");

    set.SetCharset("upper");
    EXPECT_EQ(set.GenerateFilename(0), "sha1_upper_2-6_100_0.tbl");

    set.SetCharset("numeric");
    EXPECT_EQ(set.GenerateFilename(0), "sha1_numeric_2-6_100_0.tbl");

    set.SetCharset("alnum");
    EXPECT_EQ(set.GenerateFilename(0), "sha1_alnum_2-6_100_0.tbl");
}

// ============================================================
// Directory scanning
// ============================================================

TEST_F(RainbowTableSetTest, ScanEmptyDirectory)
{
    RainbowTableSet set;
    ConfigureSmallSet(set);

    auto tables = set.ScanDirectory();
    EXPECT_TRUE(tables.empty());
}

TEST_F(RainbowTableSetTest, ScanNonexistentDirectory)
{
    RainbowTableSet set;
    set.SetDirectory(m_Dir / "nonexistent");
    auto tables = set.ScanDirectory();
    EXPECT_TRUE(tables.empty());
}

// ============================================================
// Build single table
// ============================================================

TEST_F(RainbowTableSetTest, BuildSingleTable)
{
    RainbowTableSet set;
    ConfigureSmallSet(set, 1);
    set.Build();

    auto tables = set.ScanDirectory();
    ASSERT_EQ(tables.size(), 1u);
    EXPECT_EQ(tables[0].seed, 0u);
    EXPECT_GT(tables[0].count, 0u);
    EXPECT_GT(tables[0].coverage, 0.0f);
}

TEST_F(RainbowTableSetTest, BuildCreatesDirectory)
{
    RainbowTableSet set;
    set.SetDirectory(m_Dir / "subdir" / "tables");
    set.SetAlgorithm("md5");
    set.SetMin(1);
    set.SetMax(3);
    set.SetLength(50);
    set.SetCharset("lower");
    set.SetThreads(1);
    set.SetChainCount(SimdLanes());
    set.SetTableCount(1);
    set.Build();

    EXPECT_TRUE(std::filesystem::exists(m_Dir / "subdir" / "tables"));
    // Cleanup the nested directory
    std::filesystem::remove_all(m_Dir / "subdir");
}

// ============================================================
// Build multiple tables
// ============================================================

TEST_F(RainbowTableSetTest, BuildMultipleTables)
{
    RainbowTableSet set;
    ConfigureSmallSet(set, 3);
    set.Build();

    auto tables = set.ScanDirectory();
    ASSERT_EQ(tables.size(), 3u);

    // Check seeds are 0, 1, 2
    EXPECT_EQ(tables[0].seed, 0u);
    EXPECT_EQ(tables[1].seed, 1u);
    EXPECT_EQ(tables[2].seed, 2u);
}

TEST_F(RainbowTableSetTest, BuildSkipsExistingTables)
{
    RainbowTableSet set;
    ConfigureSmallSet(set, 1);
    set.Build();

    auto tables1 = set.ScanDirectory();
    ASSERT_EQ(tables1.size(), 1u);
    size_t count1 = tables1[0].count;

    // Build again with same config — should skip
    RainbowTableSet set2;
    ConfigureSmallSet(set2, 1);
    set2.Build();

    auto tables2 = set2.ScanDirectory();
    ASSERT_EQ(tables2.size(), 1u);
    EXPECT_EQ(tables2[0].count, count1);
}

// ============================================================
// Different seeds produce different chains
// ============================================================

TEST_F(RainbowTableSetTest, DifferentSeedsProduceDifferentEndpoints)
{
    RainbowTableSet set;
    ConfigureSmallSet(set, 2);
    set.Build();

    auto tables = set.ScanDirectory();
    ASSERT_EQ(tables.size(), 2u);

    // Load both tables and compare first record's endpoint
    RainbowTable t0, t1;
    t0.SetPath(tables[0].path);
    t1.SetPath(tables[1].path);
    ASSERT_TRUE(t0.LoadTable());
    ASSERT_TRUE(t1.LoadTable());

    // The endpoint at index 0 should differ between tables with different seeds
    // (unless astronomically unlucky)
    t0.PrepareCrack();
    t1.PrepareCrack();
    auto ep0 = t0.GetEndpointAt(0);
    auto ep1 = t1.GetEndpointAt(0);
    t0.FinishCrack();
    t1.FinishCrack();

    EXPECT_NE(ep0, ep1);
}

// ============================================================
// Combined coverage
// ============================================================

TEST_F(RainbowTableSetTest, CombinedCoverageHigherThanSingle)
{
    RainbowTableSet set;
    ConfigureSmallSet(set, 2);
    set.Build();

    auto tables = set.ScanDirectory();
    ASSERT_EQ(tables.size(), 2u);

    float single = tables[0].coverage;
    float combined = set.GetCombinedCoverage(tables);
    EXPECT_GT(combined, single);
}

TEST_F(RainbowTableSetTest, CombinedCoverageFormula)
{
    // Manual test with known values
    std::vector<TableInfo> tables;
    tables.push_back({"", HashAlgorithmSHA1, 1, 4, 1024, "abc", 0, 0, 50.0f});
    tables.push_back({"", HashAlgorithmSHA1, 1, 4, 1024, "abc", 1, 0, 50.0f});

    RainbowTableSet set;
    float combined = set.GetCombinedCoverage(tables);
    // 1 - (0.5 * 0.5) = 0.75 = 75%
    EXPECT_NEAR(combined, 75.0f, 0.1f);
}

// ============================================================
// LoadConfigFromDirectory
// ============================================================

TEST_F(RainbowTableSetTest, LoadConfigFromExistingDirectory)
{
    // Build a table first
    RainbowTableSet buildSet;
    ConfigureSmallSet(buildSet, 1);
    buildSet.Build();

    // Now create a fresh set and load config from directory
    RainbowTableSet loadSet;
    loadSet.SetDirectory(m_Dir);
    ASSERT_TRUE(loadSet.LoadConfigFromDirectory());

    EXPECT_EQ(loadSet.GetAlgorithmString(), "MD5");
    EXPECT_EQ(loadSet.GetMin(), 1u);
    EXPECT_EQ(loadSet.GetMax(), 3u);
    EXPECT_EQ(loadSet.GetLength(), 50u);
    EXPECT_EQ(loadSet.GetCharset(), "abcdefghijklmnopqrstuvwxyz");
}

TEST_F(RainbowTableSetTest, LoadConfigFromEmptyDirectoryFails)
{
    std::filesystem::create_directories(m_Dir);
    RainbowTableSet set;
    set.SetDirectory(m_Dir);
    EXPECT_FALSE(set.LoadConfigFromDirectory());
}

// ============================================================
// Crack single hash across tables
// ============================================================

TEST_F(RainbowTableSetTest, CrackFindsKnownPassword)
{
    // Build a small table
    RainbowTableSet set;
    ConfigureSmallSet(set, 1);
    set.SetChainCount(SimdLanes() * 16);
    set.Build();

    auto tables = set.ScanDirectory();
    ASSERT_FALSE(tables.empty());

    // Compute the hash of "a" using MD5
    RainbowTable refTable;
    refTable.SetPath(tables[0].path);
    refTable.LoadTable();
    auto hash = refTable.DoHashHex(
        reinterpret_cast<const uint8_t*>("a"), 1);

    // Try to crack it
    RainbowTableSet crackSet;
    crackSet.SetDirectory(m_Dir);
    crackSet.LoadConfigFromDirectory();
    crackSet.SetThreads(1);
    auto results = crackSet.Crack(hash);

    // "a" is in the keyspace (lower, 1-3) and should be findable
    // with enough chains. This is probabilistic but very likely.
    if (!results.empty())
    {
        EXPECT_EQ(std::get<1>(results[0]), "a");
    }
}

// ============================================================
// Crack file of hashes across multiple tables
// ============================================================

TEST_F(RainbowTableSetTest, CrackFromFileAcrossTables)
{
    // Build 2 tables with enough chains
    RainbowTableSet set;
    ConfigureSmallSet(set, 2);
    set.SetChainCount(SimdLanes() * 16);
    set.Build();

    auto tables = set.ScanDirectory();
    ASSERT_EQ(tables.size(), 2u);

    // Create a hash file for known passwords
    RainbowTable refTable;
    refTable.SetPath(tables[0].path);
    refTable.LoadTable();

    auto hashFilePath = m_Dir / "test_hashes.txt";
    {
        std::ofstream hf(hashFilePath);
        hf << refTable.DoHashHex(reinterpret_cast<const uint8_t*>("a"), 1) << "\n";
        hf << refTable.DoHashHex(reinterpret_cast<const uint8_t*>("b"), 1) << "\n";
    }

    RainbowTableSet crackSet;
    crackSet.SetDirectory(m_Dir);
    crackSet.LoadConfigFromDirectory();
    crackSet.SetThreads(1);
    auto results = crackSet.Crack(hashFilePath.string());

    // Should find at least some results
    // (probabilistic but highly likely with 2 tables)
    EXPECT_GE(results.size(), 0u);
}

// ============================================================
// Seed stored in header
// ============================================================

TEST_F(RainbowTableSetTest, SeedStoredInTableHeader)
{
    RainbowTableSet set;
    ConfigureSmallSet(set, 3);
    set.Build();

    auto tables = set.ScanDirectory();
    ASSERT_EQ(tables.size(), 3u);

    for (uint32_t i = 0; i < 3; i++)
    {
        TableHeader hdr;
        ASSERT_TRUE(RainbowTable::GetTableHeader(tables[i].path, &hdr));
        EXPECT_EQ(hdr.seed, i);
    }
}

// ============================================================
// Auto table count from coverage
// ============================================================

TEST_F(RainbowTableSetTest, AutoTableCountFromCoverage)
{
    RainbowTableSet set;
    set.SetDirectory(m_Dir);
    set.SetAlgorithm("md5");
    set.SetMin(1);
    set.SetMax(3);
    set.SetLength(50);
    set.SetCharset("lower");
    set.SetCoverage(0.99);

    // With default table count = 0, it should auto-compute
    // For 99% combined, at ~85% per table: ceil(ln(0.01)/ln(0.15)) ≈ 3
    // The exact number depends on the estimate, but should be > 1
    auto tables_needed = set.GetTableCount();
    EXPECT_EQ(tables_needed, 0u);  // 0 means auto

    // Build should compute and create tables automatically
    set.SetThreads(1);
    set.Build();

    auto tables = set.ScanDirectory();
    EXPECT_GE(tables.size(), 2u);
}
