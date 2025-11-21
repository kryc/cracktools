#include <gtest/gtest.h>

#include <string>

#include "Util.hpp"

TEST(Util, NeedsHexlify)
{
    EXPECT_TRUE(Util::NeedsHexlify("$HEX[48656C6C6F2C20576F726C6421]"));
    EXPECT_TRUE(Util::NeedsHexlify("$HEX[48656c6c6f2c20576f726c6421]"));
    EXPECT_TRUE(Util::NeedsHexlify("$HEX[3030]"));
    EXPECT_TRUE(Util::NeedsHexlify("$HEX[303]"));
    EXPECT_FALSE(Util::NeedsHexlify("Hello, World!"));
    EXPECT_TRUE(Util::NeedsHexlify("$HEX[ZZZ]")); // Invalid hex should not be considered hexlified
    EXPECT_FALSE(Util::NeedsHexlify("")); // Empty string should not need hexlify
    EXPECT_FALSE(Util::NeedsHexlify("NormalStringWith$HEXInside")); // $HEX inside normal string
    EXPECT_TRUE(Util::NeedsHexlify("Space ")); // Ending in whitespace (space)
    EXPECT_TRUE(Util::NeedsHexlify("Tab\t")); // Ending in whitespace (tab)
    EXPECT_TRUE(Util::NeedsHexlify("Newline\n")); // Ending in whitespace (newline)
    EXPECT_TRUE(Util::NeedsHexlify("CarriageReturn\r")); // Ending in whitespace (carriage return)
    EXPECT_TRUE(Util::NeedsHexlify("NonPrintable\x01")); // Non-printable character
}

TEST(Util, Hexlify)
{
    EXPECT_EQ(Util::Hexlify("$HEX[48656c6c6f2c20576f726c6421]"), "$HEX[244845585b34383635366336633666326332303537366637323663363432315d]");
    EXPECT_EQ(Util::Hexlify("Hello, World!"), "Hello, World!");
    EXPECT_EQ(Util::Hexlify("Space "), "$HEX[537061636520]");
    EXPECT_EQ(Util::Hexlify("Tab\t"), "$HEX[54616209]");
    EXPECT_EQ(Util::Hexlify("Newline\n"), "$HEX[4e65776c696e650a]");
    EXPECT_EQ(Util::Hexlify("CarriageReturn\r"), "$HEX[436172726961676552657475726e0d]");
    EXPECT_EQ(Util::Hexlify("NonPrintable\x01"), "$HEX[4e6f6e5072696e7461626c6501]");
}

TEST(Util, IsHexlified)
{
    EXPECT_TRUE(Util::IsHexlified("$HEX[48656C6C6F2C20576F726C6421]"));
    EXPECT_TRUE(Util::IsHexlified("$HEX[48656c6c6f2c20576f726c6421]"));
    EXPECT_TRUE(Util::IsHexlified("$HEX[3030]"));
    EXPECT_FALSE(Util::IsHexlified("$HEX[303]"));
    EXPECT_TRUE(Util::IsHexlified("$HEX[]"));
    EXPECT_FALSE(Util::IsHexlified("$HEX["));
    EXPECT_FALSE(Util::IsHexlified("$HEX010203]"));
    EXPECT_FALSE(Util::IsHexlified("$HEX[ZZZZ]"));
    EXPECT_FALSE(Util::IsHexlified("$HEX[ZZZ]"));
    EXPECT_FALSE(Util::IsHexlified("$HEX[zzzz]"));
    EXPECT_FALSE(Util::IsHexlified("$HEX[zzz]"));
    EXPECT_FALSE(Util::IsHexlified("Hello, World!"));
}

TEST(Util, UnHexlify)
{
    EXPECT_EQ(Util::UnHexlify("$HEX[48656C6C6F2C20576F726C6421]"), "Hello, World!");
    EXPECT_EQ(Util::UnHexlify("Hello, World!"), "Hello, World!");
    std::string hexString = "$HEX[48656C6C6F2C20576F726C6421]";
    bool unhexlified = Util::MaybeUnHexlifyInPlace(hexString);
    EXPECT_TRUE(unhexlified);
    EXPECT_EQ(hexString, "Hello, World!");
    std::string normalString = "Normal String";
    unhexlified = Util::MaybeUnHexlifyInPlace(normalString);
    EXPECT_FALSE(unhexlified);
    EXPECT_EQ(normalString, "Normal String");
}