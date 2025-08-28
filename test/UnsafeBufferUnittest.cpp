#include <gtest/gtest.h>

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