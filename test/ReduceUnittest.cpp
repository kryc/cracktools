#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Reduce.hpp"

TEST(Reduction, GetBitsRequiredASCII) {
    uint8_t mask = 0;
    auto bitsRequired = calculate_bits_required(96, &mask);
    EXPECT_EQ(bitsRequired, 7);
    EXPECT_EQ(mask, 0x7F);
}

TEST(Reduction, GetBitsRequiredLower) {
    uint8_t mask = 0;
    auto bitsRequired = calculate_bits_required(26, &mask);
    EXPECT_EQ(bitsRequired, 5);
    EXPECT_EQ(mask, 0x1F);
}