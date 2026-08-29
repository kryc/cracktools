#include <gtest/gtest.h>

#include <string>

#include "UnsafeBuffer.hpp"
#include "Util.hpp"

TEST(Util, IsNumeric)
{
    EXPECT_TRUE(Util::IsNumeric("1234567890"));
    EXPECT_FALSE(Util::IsNumeric("12345a67890"));
    EXPECT_FALSE(Util::IsNumeric("12 34567890"));
    EXPECT_FALSE(Util::IsNumeric(""));
}

TEST(Util, AsciiCase)
{
    EXPECT_TRUE(Util::IsLower('a'));
    EXPECT_FALSE(Util::IsLower('A'));
    EXPECT_TRUE(Util::IsUpper('Z'));
    EXPECT_FALSE(Util::IsUpper('z'));
    EXPECT_TRUE(Util::IsNumeric('5'));
    EXPECT_FALSE(Util::IsNumeric('x'));
    EXPECT_EQ(Util::ToLower('A'), 'a');
    EXPECT_EQ(Util::ToUpper('z'), 'Z');
    EXPECT_EQ(Util::ToggleCase('a'), 'A');
    EXPECT_EQ(Util::ToggleCase('A'), 'a');
    EXPECT_EQ(Util::ToggleCase('1'), '1');
    EXPECT_EQ(Util::ToUpper("Hello, World!"), "HELLO, WORLD!");
}

TEST(Util, IsHex)
{
    EXPECT_TRUE(Util::IsHex("48656C6C6F2C20576F726C6421"));
    EXPECT_TRUE(Util::IsHex("48656c6c6f2c20576f726c6421"));
    EXPECT_FALSE(Util::IsHex("48656C6C6F2C20576F726C642Z"));
    EXPECT_FALSE(Util::IsHex("48656C6C6F2C20576F726C642"));
    EXPECT_FALSE(Util::IsHex(""));
}

TEST(Util, IsHexCase)
{
    EXPECT_TRUE(Util::IsLowerHex('0'));
    EXPECT_TRUE(Util::IsLowerHex('a'));
    EXPECT_TRUE(Util::IsLowerHex('f'));
    EXPECT_FALSE(Util::IsLowerHex('A'));
    EXPECT_FALSE(Util::IsLowerHex('g'));

    EXPECT_TRUE(Util::IsUpperHex('0'));
    EXPECT_TRUE(Util::IsUpperHex('A'));
    EXPECT_TRUE(Util::IsUpperHex('F'));
    EXPECT_FALSE(Util::IsUpperHex('a'));
    EXPECT_FALSE(Util::IsUpperHex('G'));
}

TEST(Util, IsBase64)
{
    EXPECT_TRUE(Util::IsBase64("SGVsbG8sIFdvcmxkIQ=="));
    EXPECT_TRUE(Util::IsBase64("U29tZSB0ZXh0IHdpdGggc3BlY2lhbCBjaGFycy4="));
    EXPECT_FALSE(Util::IsBase64("SGVsbG8sIFdvcmxkIQ==!"));
    EXPECT_FALSE(Util::IsBase64("NotBase64String[]"));
    EXPECT_FALSE(Util::IsBase64(""));
}

TEST(Util, IsRadix64)
{
    EXPECT_TRUE(Util::IsRadix64("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./"));
    EXPECT_FALSE(Util::IsRadix64("Radix64.String/With+Chars="));
    EXPECT_FALSE(Util::IsRadix64("Invalid*Char&Here"));
    EXPECT_FALSE(Util::IsRadix64(""));
}

TEST(Util, IsPrintableASCII)
{
    const std::string_view helloWorld = "Hello, World!";
    const std::string_view helloWorldWithNewline = "Hello,\nWorld!";
    const std::string_view helloWorldWithNonPrintable = "Hello,\x01World!";

    EXPECT_TRUE(Util::IsPrintableASCII(helloWorld));
    EXPECT_FALSE(Util::IsPrintableASCII(helloWorldWithNewline));
    EXPECT_FALSE(Util::IsPrintableASCII(helloWorldWithNonPrintable));
    auto helloWorldSpan = cracktools::UnsafeSpan<const uint8_t>(helloWorld);
    auto helloWorldWithNewlineSpan = cracktools::UnsafeSpan<const uint8_t>(helloWorldWithNewline);
    EXPECT_TRUE(Util::IsPrintableASCII(helloWorldSpan));
    EXPECT_FALSE(Util::IsPrintableASCII(helloWorldWithNewlineSpan));
}

TEST(Util, IsPrintableASCIIHexlified)
{
    EXPECT_TRUE(Util::IsPrintableASCIIHexlified("$HEX[48656C6C6F2C20576F726C6421]")); // "Hello, World!"
    EXPECT_TRUE(Util::IsPrintableASCIIHexlified("$HEX[48656C6C6F2C20576F726C6420]")); // "Hello, World " (space at end)
    EXPECT_TRUE(Util::IsPrintableASCIIHexlified("NotAHexlifiedString")); // Fallthrough to IsPrintableASCII case
    EXPECT_FALSE(Util::IsPrintableASCIIHexlified("$HEX[48656C6C6F2C20576F726C6421FF]")); // "Hello, World!" + 0xFF
    EXPECT_FALSE(Util::IsPrintableASCIIHexlified("$HEX[48656C6C6F2C20576F726C64217F]")); // "Hello, World!" + 0x7F
}

TEST(Util, IsPrintableUTF8)
{
    EXPECT_TRUE(Util::IsPrintableUTF8("Hello, World!"));
    EXPECT_TRUE(Util::IsPrintableUTF8("こんにちは")); // "Hello" in Japanese
    EXPECT_FALSE(Util::IsPrintableUTF8("Hello,\nWorld!"));
    EXPECT_FALSE(Util::IsPrintableUTF8("Hello,\x01World!"));
}

TEST(Util, IsPrintableUTF8Hexlified)
{
    EXPECT_TRUE(Util::IsPrintableUTF8Hexlified("$HEX[E38193E38293E381ABE381A1E381AF]")); // "こんにちは"
    EXPECT_TRUE(Util::IsPrintableUTF8Hexlified("$HEX[48656C6C6F2C20576F726C6421]")); // "Hello, World!"
    EXPECT_TRUE(Util::IsPrintableUTF8Hexlified("NotAHexlifiedString")); // Fallthrough to IsPrintableUTF8 case
    EXPECT_FALSE(Util::IsPrintableUTF8Hexlified("$HEX[E38193E38293E381ABE381A1E381AF80]")); // "こんにちは" + invalid byte 0x80
    EXPECT_FALSE(Util::IsPrintableUTF8Hexlified("$HEX[48656C6C6F2C20576F726C6421FF]")); // "Hello, World!" + invalid byte 0xFF
}

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
    EXPECT_FALSE(Util::IsHexlified("$HEX[]"));
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

TEST(Util, ParseHex)
{
    std::string hexStr = "48656C6C6F2C20576F726C6421";
    auto result = Util::ParseHex(hexStr);
    std::string expected = "Hello, World!";
    EXPECT_EQ(cracktools::AsStringView(result), expected);

    std::string hexStr2 = "4A61766120576F726C6421";
    auto result2 = Util::ParseHex(hexStr2);
    expected = "Java World!";
    EXPECT_EQ(cracktools::AsStringView(result2), expected);

    // Lower case hex
    std::string hexStrLower = "68656c6c6f2c20776f726c6421";
    auto resultLower = Util::ParseHex(hexStrLower);
    expected = "hello, world!";
    EXPECT_EQ(cracktools::AsStringView(resultLower), expected);

    // With non-ascii characters
    std::string hexStrNonAscii = "E4BDA0E5A5BD"; // "你好" in UTF-8 hex
    auto resultNonAscii = Util::ParseHex(hexStrNonAscii);
    expected = "你好";
    EXPECT_EQ(cracktools::AsStringView(resultNonAscii), expected);

    // With invalid hex characters
    std::string hexStrInvalid = "5G656C6C6F2C20576F726C6421"; // '5G' is invalid
    auto resultInvalid = Util::ParseHex(hexStrInvalid);
    expected = "ello, World!"; // Should ignore the invalid character
    EXPECT_EQ(cracktools::AsStringView(resultInvalid), expected);
}

TEST(Util, ParseHexInplace)
{
    std::string str = "48656C6C6F2C20576F726C6421";
    size_t bytesParsed = Util::ParseHexInplace(str);
    EXPECT_EQ(bytesParsed, 13);
    std::string expected = "Hello, World!";
    EXPECT_EQ(std::string(str.data(), bytesParsed), expected);

    std::string str2 = "4A61766120576F726C6421";
    size_t bytesParsed2 = Util::ParseHexInplace(str2);
    EXPECT_EQ(bytesParsed2, 11);
    expected = "Java World!";
    EXPECT_EQ(std::string(str2.data(), bytesParsed2), expected);

    // Lower case hex
    std::string strLower = "68656c6c6f2c20776f726c6421";
    size_t bytesParsedLower = Util::ParseHexInplace(strLower);
    EXPECT_EQ(bytesParsedLower, 13);
    expected = "hello, world!";
    EXPECT_EQ(std::string(strLower.data(), bytesParsedLower), expected);

    // With non-ascii characters
    std::string strNonAscii = "E4BDA0E5A5BD"; // "你好" in UTF-8 hex
    size_t bytesParsedNonAscii = Util::ParseHexInplace(strNonAscii);
    EXPECT_EQ(bytesParsedNonAscii, 6);
    expected = "你好";
    EXPECT_EQ(std::string(strNonAscii.data(), bytesParsedNonAscii), expected);

    // With MaxBytes
    std::string str3 = "48656C6C6F2C20576F726C6421";
    size_t bytesParsed3 = Util::ParseHexInplace(str3, 5);
    EXPECT_EQ(bytesParsed3, 5);
    expected = "Hello";
    EXPECT_EQ(std::string(str3.data(), bytesParsed3), expected);

    // With invalid hex characters
    std::string strInvalid = "5G656C6C6F2C20576F726C6421"; // '5G' is invalid
    size_t bytesParsedInvalid = Util::ParseHexInplace(strInvalid);
    EXPECT_EQ(bytesParsedInvalid, 12);
    expected = "ello, World!"; // Should ignore the invalid character
    EXPECT_EQ(std::string(strInvalid.data(), bytesParsedInvalid), expected);
}

TEST(Util, CalculateKeyspaceForMask)
{
    std::string mask = "l";
    size_t keyspace = Util::CalculateKeyspaceForMask(mask);
    EXPECT_EQ(keyspace, 26);

    mask = "u";
    keyspace = Util::CalculateKeyspaceForMask(mask);
    EXPECT_EQ(keyspace, 26);

    mask = "d";
    keyspace = Util::CalculateKeyspaceForMask(mask);
    EXPECT_EQ(keyspace, 10);

    mask = "s";
    keyspace = Util::CalculateKeyspaceForMask(mask);
    EXPECT_EQ(keyspace, 32);

    mask = "lud";
    keyspace = Util::CalculateKeyspaceForMask(mask);
    EXPECT_EQ(keyspace, 26*26*10);
}

TEST(Util, GetMask)
{
    std::string input = "Hello, World!";
    auto mask = Util::GetMask(input);
    EXPECT_TRUE(mask.has_value());
    EXPECT_EQ(mask.value(), "?u?l?l?l?l?s?s?u?l?l?l?l?s");

    input = "1234567890";
    mask = Util::GetMask(input);
    EXPECT_TRUE(mask.has_value());
    EXPECT_EQ(mask.value(), "?d?d?d?d?d?d?d?d?d?d");

    input = "InvalidASCII\x01";
    mask = Util::GetMask(input);
    EXPECT_FALSE(mask.has_value());
}

TEST(Util, IsMask)
{
    EXPECT_TRUE(Util::IsMask("?u?l?l?l?l?s?s?u?l?l?l?l?s"));
    EXPECT_TRUE(Util::IsMask("?d?d?d?d?d?d?d?d?d?d"));
    EXPECT_FALSE(Util::IsMask("?x?y?z"));
    EXPECT_FALSE(Util::IsMask("invalidmask"));
    EXPECT_FALSE(Util::IsMask(""));
}

TEST(Util, IsValidUsername)
{
    EXPECT_TRUE(Util::IsValidUsername("user_name123"));
    EXPECT_FALSE(Util::IsValidUsername("user name"));
    EXPECT_FALSE(Util::IsValidUsername("user@name"));
    EXPECT_FALSE(Util::IsValidUsername(""));
}

TEST(Util, IsValidEmail)
{
    EXPECT_TRUE(Util::IsValidEmail("user@example.com"));
    EXPECT_TRUE(Util::IsValidEmail("user@example.co.uk"));
    EXPECT_TRUE(Util::IsValidEmail("user@qq.com"));
    EXPECT_TRUE(Util::IsValidEmail("user@360.com"));
    EXPECT_FALSE(Util::IsValidEmail(".user@example.com"));
    EXPECT_FALSE(Util::IsValidEmail("user.@example.com"));
    EXPECT_FALSE(Util::IsValidEmail("user..a@example.com"));
    EXPECT_FALSE(Util::IsValidEmail("user@.example.com"));
    EXPECT_FALSE(Util::IsValidEmail("user@example.com."));
    EXPECT_FALSE(Util::IsValidEmail("user@-example.com"));
    EXPECT_FALSE(Util::IsValidEmail("user@example.com-"));
    EXPECT_FALSE(Util::IsValidEmail("user@.com"));
    EXPECT_FALSE(Util::IsValidEmail("user@com"));
    EXPECT_FALSE(Util::IsValidEmail("user@exam ple.com"));
    EXPECT_FALSE(Util::IsValidEmail("userexample.com"));
    EXPECT_FALSE(Util::IsValidEmail("user@"));
    EXPECT_FALSE(Util::IsValidEmail("@example.com"));
    EXPECT_FALSE(Util::IsValidEmail(""));
}

TEST(Util, IsLikelyValidEmail)
{
    EXPECT_TRUE(Util::IsLikelyValidEmail("user@example.com"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("userexample.com"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("user@"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("@example.com"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("not an email"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("P@ssword.123"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("mk@cruiser.1991"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("Tou@reg3.6"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("PsychW@rd.841412"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("Distr@ction.1"));
    EXPECT_FALSE(Util::IsLikelyValidEmail("$t@r.wars"));
}

TEST(Util, IsValidIPv4)
{
    EXPECT_TRUE(Util::IsValidIPv4("1.1.1.1"));
    EXPECT_TRUE(Util::IsValidIPv4("192.168.1.1"));
    EXPECT_TRUE(Util::IsValidIPv4("255.255.255.255"));
    EXPECT_FALSE(Util::IsValidIPv4("256.1.1.1"));
    EXPECT_FALSE(Util::IsValidIPv4("192.168.1"));
    EXPECT_FALSE(Util::IsValidIPv4("192.168.1.256"));
}

TEST(Util, IsAlphanumeric)
{
    EXPECT_TRUE(Util::IsAlphanumeric("Username123", Util::Case::Both));
    EXPECT_TRUE(Util::IsAlphanumeric("username", Util::Case::Lower));
    EXPECT_TRUE(Util::IsAlphanumeric("USERNAME", Util::Case::Upper));
    EXPECT_FALSE(Util::IsAlphanumeric("User_name123", Util::Case::Both));
    EXPECT_FALSE(Util::IsAlphanumeric("User-name123", Util::Case::Both));
    EXPECT_FALSE(Util::IsAlphanumeric("User name123", Util::Case::Both));
    EXPECT_FALSE(Util::IsAlphanumeric("Username123!", Util::Case::Both));
    EXPECT_FALSE(Util::IsAlphanumeric("", Util::Case::Both));
}

TEST(Util, IsNumericString)
{
    EXPECT_TRUE(Util::IsNumericString("1234567890"));
    EXPECT_FALSE(Util::IsNumericString("12345a67890"));
    EXPECT_FALSE(Util::IsNumericString("12 34567890"));
    EXPECT_FALSE(Util::IsNumericString(""));
}

TEST(Util, IsLikelyDateString)
{
    EXPECT_TRUE(Util::IsLikelyDateString("2023-10-05"));
    EXPECT_TRUE(Util::IsLikelyDateString("05/10/2023"));
    EXPECT_TRUE(Util::IsLikelyDateString("10/05/23"));
    EXPECT_FALSE(Util::IsLikelyDateString("20231005"));
    EXPECT_FALSE(Util::IsLikelyDateString("NotADate"));
    EXPECT_FALSE(Util::IsLikelyDateString(""));
}

TEST(Util, CouldBeHashHex)
{
    EXPECT_TRUE(Util::CouldBeHashHex("5d41402abc4b2a76b9719d911017c592")); // MD5
    EXPECT_TRUE(Util::CouldBeHashHex("2aae6c35c94fcfb415dbe95f408b9ce91ee846ed")); // SHA1
    EXPECT_TRUE(Util::CouldBeHashHex("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e")); // SHA512
    EXPECT_FALSE(Util::CouldBeHashHex("NotAHash"));
    EXPECT_FALSE(Util::CouldBeHashHex("5d41402abc4b2a76b9719d911017c59")); // Invalid length
}

TEST(Util, CouldBeCryptHash)
{
    EXPECT_TRUE(Util::CouldBeCryptHash("$1$etNnh7FA$OlM7eljE/B7F1J4XYNnk81")); // MD5
    EXPECT_TRUE(Util::CouldBeCryptHash("$2a$10$VIhIOofSMqgdGlL4wzE//e.77dAQGqntF/1dT7bqCrVtquInWy2qi")); // Blowfish
    EXPECT_TRUE(Util::CouldBeCryptHash("$3$$abcdefghijklmnopqrstuvwxzy0123456789ABCD")); // NTHASH with empty salt
    EXPECT_TRUE(Util::CouldBeCryptHash("$5$9ks3nNEqv31FX.F$gdEoLFsCRsn/WRN3wxUnzfeZLoooVlzeF4WjLomTRFD")); // SHA256
    EXPECT_TRUE(Util::CouldBeCryptHash("$6$qoE2letU$wWPRl.PVczjzeMVgjiA8LLy2nOyZbf7Amj3qLIL978o18gbMySdKZ7uepq9tmMQXxyTIrS12Pln.2Q/6Xscao0")); // SHA512
    EXPECT_TRUE(Util::CouldBeCryptHash("$7$DU..../....2Q9obwLhin8qvQl6sisAO/$sHayJj/JBdcuD4lJ1AxiwCo9e5XSi8TcINcmyID12i8")); // scrypt
    EXPECT_TRUE(Util::CouldBeCryptHash("$8$mTj4RZG8N9ZDOk$elY/asfm8kD3iDmkBe3hD2r4xcA/0oWS5V3os.O91u.")); // PBKDF2-SHA256
    // EXPECT_TRUE(Util::CouldBeCryptHash("$gy$jCT$HM87v.7RwpQLba8fDjNSk1$VgqS7k2OZWhFbAJVBye2vaA7ex/1VtU3a5fmL8Wv/26")); // gost-yescrypt
    EXPECT_FALSE(Util::CouldBeCryptHash("NotACryptHash"));
    EXPECT_FALSE(Util::CouldBeCryptHash("$9$salt$invalidhashvalue")); // Invalid ID
    EXPECT_FALSE(Util::CouldBeCryptHash("$1$salt$short")); // Hash too short
}