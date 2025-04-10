#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "HashList.hpp"
#include "Util.hpp"

std::vector<uint8_t>
GenerateLinearHashes(
    const size_t count,
    const size_t length
)
{
    std::vector<uint8_t> hashes;
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < length; j++) {
            hashes.push_back(static_cast<uint8_t>(i));
        }
    }
    return hashes;
}

std::vector<uint8_t>
GenerateRandomHashes(
    const size_t count,
    const size_t length
)
{
    std::vector<uint8_t> hashes;
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < length; j++) {
            hashes.push_back(static_cast<uint8_t>(rand() % 256));
        }
    }
    return hashes;
}

TEST(HashList, BasicLookup) {
    std::vector<uint8_t> hashes = GenerateLinearHashes(100, 32);
    HashList hashlist;
    hashlist.Initialize(hashes, 32);
    EXPECT_EQ(hashlist.GetCount(), 100);
    for (size_t i = 0; i < 100; i++) {
        std::vector<uint8_t> hash(32, static_cast<uint8_t>(i));
        EXPECT_TRUE(hashlist.Lookup(hash));
        EXPECT_EQ(hashlist.Find(hash).value(), i);
    }
}

TEST(HashList, Sorting) {
    std::vector<uint8_t> hashes = GenerateLinearHashes(100, 32);
    std::reverse(hashes.begin(), hashes.end());
    HashList hashlist;
    hashlist.Initialize(hashes, 32, true);
    for (size_t i = 0; i < 100; i++) {
        std::vector<uint8_t> hash(32, static_cast<uint8_t>(i));
        EXPECT_TRUE(hashlist.Lookup(hash));
        EXPECT_EQ(hashlist.Find(hash).value(), i);
    }
}

TEST(HashList, BinaryLookup) {
    std::vector<uint8_t> hashes = GenerateLinearHashes(100, 32);
    HashList hashlist;
    hashlist.Initialize(hashes, 32);
    for (size_t i = 0; i < 100; i++) {
        std::vector<uint8_t> hash(32, static_cast<uint8_t>(i));
        EXPECT_TRUE(hashlist.LookupBinary(hash));
        EXPECT_EQ(hashlist.Find(hash).value(), i);
    }
}

TEST(HashList, LinearLookup) {
    std::vector<uint8_t> hashes = GenerateLinearHashes(100, 32);
    HashList hashlist;
    hashlist.Initialize(hashes, 32);
    for (size_t i = 0; i < 100; i++) {
        std::vector<uint8_t> hash(32, static_cast<uint8_t>(i));
        EXPECT_TRUE(hashlist.LookupLinear(hash));
        EXPECT_EQ(hashlist.Find(hash).value(), i);
    }
}

TEST(HashList, InvalidLookup) {
    std::vector<uint8_t> hashes = GenerateLinearHashes(100, 32);
    HashList hashlist;
    hashlist.Initialize(hashes, 32);
    std::vector<uint8_t> invalid_hash(32, 255);
    EXPECT_FALSE(hashlist.Lookup(invalid_hash));
    EXPECT_FALSE(hashlist.LookupBinary(invalid_hash));
    EXPECT_FALSE(hashlist.LookupLinear(invalid_hash));
}

TEST(HashList, DuplicateLookup) {
    std::vector<uint8_t> hashes = GenerateLinearHashes(100, 32);
    hashes.insert(hashes.end(), hashes.begin(), hashes.end()); // Duplicate the hashes
    HashList hashlist;
    hashlist.Initialize(hashes, 32, true);
    EXPECT_EQ(hashlist.GetCount(), 200); // Check that the count is correct
    std::vector<uint8_t> duplicate_hash(32, 0);
    EXPECT_TRUE(hashlist.Lookup(duplicate_hash));
    EXPECT_EQ(hashlist.Find(duplicate_hash).value(), 0);
}

TEST(HashList, BitMapSize) {
    std::vector<uint8_t> hashes = GenerateRandomHashes(1024, 32);
    HashList hashlist;
    EXPECT_TRUE(hashlist.SetBitmaskSize(24));
    EXPECT_EQ(hashlist.GetBitmaskSize(), 24);
    hashlist.Initialize(hashes, 32, true);
    EXPECT_FALSE(hashlist.SetBitmaskSize(10));
    EXPECT_EQ(hashlist.GetBitmaskSize(), 24);
    for (size_t i = 0; i < 100; i++) {
        size_t random_index = rand() % hashlist.GetCount();
        auto hash = hashlist.GetHash(random_index);
        EXPECT_TRUE(hashlist.Lookup(hash));
        EXPECT_EQ(hashlist.Find(hash).value(), random_index);
    }
}

TEST(HashList, HugeTest) {
    std::vector<uint8_t> hashes = GenerateRandomHashes(65536 << 5, 20);
    auto hashes_span = std::span<uint8_t>(hashes);
    HashList hashlist;
    hashlist.Initialize(hashes, 20, true);
    for (size_t i = 0; i < hashlist.GetCount(); i++) {
        std::span<const uint8_t> hash = hashes_span.subspan(i * 20, 20);
        EXPECT_TRUE(hashlist.Lookup(hash));
    }
}