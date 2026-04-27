#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "UnsafeBuffer.hpp"

TEST(UnsafeBuffer, StringViewAsBytes)
{
    std::string_view str = "Hello, World!";
    auto bytes = cracktools::AsBytes(str);
    EXPECT_EQ(bytes.size(), str.size());
    for (size_t i = 0; i < str.size(); i++) {
        EXPECT_EQ(bytes[i], static_cast<uint8_t>(str[i]));
    }
}

static const std::array<uint8_t, 20> TestData = {
    0x01, 0x02, 0x03, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14
};

TEST(UnsafeBuffer, LoadTypeUint64){
    uint64_t value = cracktools::LoadTypeLittleEndian<uint64_t>(TestData);
    EXPECT_EQ(value, 0x0807060504030201);
    value = cracktools::LoadTypeBigEndian<uint64_t>(TestData);
    EXPECT_EQ(value, 0x0102030405060708);
}

TEST(UnsafeBuffer, LoadTypeUint128){
    __uint128_t value = cracktools::LoadTypeLittleEndian<__uint128_t>(TestData);
    uint64_t low = static_cast<uint64_t>(value & 0xFFFFFFFFFFFFFFFF);
    uint64_t high = static_cast<uint64_t>(value >> 64);
    EXPECT_EQ(low, 0x0807060504030201);
    EXPECT_EQ(high, 0x100F0E0D0C0B0A09);
    value = cracktools::LoadTypeBigEndian<__uint128_t>(TestData);
    low = static_cast<uint64_t>(value & 0xFFFFFFFFFFFFFFFF);
    high = static_cast<uint64_t>(value >> 64);
    EXPECT_EQ(low, 0x090A0B0C0D0E0F10);
    EXPECT_EQ(high, 0x0102030405060708);
}

TEST(UnsafeBuffer, LoadBytesToTypeLittleEndian){
    // Test load the full 8 bytes
    uint64_t value = cracktools::LoadBytesToTypeLittleEndian<uint64_t>(TestData);
    EXPECT_EQ(value, 0x0807060504030201);
    // Test load only 5 bytes
    value = cracktools::LoadBytesToTypeLittleEndian<uint64_t>(TestData, 5);
    EXPECT_EQ(value, 0x0504030201);
    // Test load the full 16 bytes into a 128-bit type
    __uint128_t value128 = cracktools::LoadBytesToTypeLittleEndian<__uint128_t>(TestData);
    uint64_t low = static_cast<uint64_t>(value128 & 0xFFFFFFFFFFFFFFFF);
    uint64_t high = static_cast<uint64_t>(value128 >> 64);
    EXPECT_EQ(low, 0x0807060504030201);
    EXPECT_EQ(high, 0x100F0E0D0C0B0A09);
    // Test load only 10 bytes into a 128-bit type
    value128 = cracktools::LoadBytesToTypeLittleEndian<__uint128_t>(TestData, 10);
    low = static_cast<uint64_t>(value128 & 0xFFFFFFFFFFFFFFFF);
    high = static_cast<uint64_t>(value128 >> 64);
    EXPECT_EQ(low, 0x0807060504030201);
    EXPECT_EQ(high, 0x0000000000000A09);
    // Create a smaller subspan to force the slower path
    std::span<const uint8_t> span(TestData);
    auto subspan = span.subspan(0, 7);
    value = cracktools::LoadBytesToTypeLittleEndian<uint64_t>(subspan, 7);
    EXPECT_EQ(value, 0x07060504030201);
}

TEST(UnsafeBuffer, LoadBytesToTypeBigEndian){
    // Test load the full 8 bytes
    uint64_t value = cracktools::LoadBytesToTypeBigEndian<uint64_t>(TestData);
    EXPECT_EQ(value, 0x0102030405060708);
    // Test load only 5 bytes
    value = cracktools::LoadBytesToTypeBigEndian<uint64_t>(TestData, 5);
    EXPECT_EQ(value, 0x0102030405);
    // Test load the full 16 bytes into a 128-bit type
    __uint128_t value128 = cracktools::LoadBytesToTypeBigEndian<__uint128_t>(TestData);
    uint64_t low = static_cast<uint64_t>(value128 & 0xFFFFFFFFFFFFFFFF);
    uint64_t high = static_cast<uint64_t>(value128 >> 64);
    EXPECT_EQ(low, 0x090A0B0C0D0E0F10);
    EXPECT_EQ(high, 0x0102030405060708);
    // Test load only 10 bytes into a 128-bit type
    value128 = cracktools::LoadBytesToTypeBigEndian<__uint128_t>(TestData, 10);
    low = static_cast<uint64_t>(value128 & 0xFFFFFFFFFFFFFFFF);
    high = static_cast<uint64_t>(value128 >> 64);
    EXPECT_EQ(low, 0x030405060708090A);
    EXPECT_EQ(high, 0x0000000000000102);
    // Create a smaller subspan to force the slower path
    std::span<const uint8_t> span(TestData);
    auto subspan = span.subspan(0, 7);
    value = cracktools::LoadBytesToTypeBigEndian<uint64_t>(subspan, 7);
    EXPECT_EQ(value, 0x01020304050607);
}