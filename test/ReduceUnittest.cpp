#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Reduce.hpp"

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