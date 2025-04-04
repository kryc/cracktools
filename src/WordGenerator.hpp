//
//  WordGenerator.hpp
//  SimdCrack
//
//  Created by Kryc on 20/01/2024.
//  Copyright © 2020 Kryc. All rights reserved.
//

#ifndef WordGenerator_hpp
#define WordGenerator_hpp

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gmpxx.h>

// All values are sorted
// Take care when editing!
static const std::string LOWER = "abcdefghijklmnopqrstuvwxyz";
static const std::string UPPER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const std::string ALPHA = UPPER + LOWER;
static const std::string NUMERIC = "0123456789";
static const std::string ASCII_SPECIAL_LO = " !\"#$%&'()*+,-./";
static const std::string ASCII_SPECIAL_ML = ":;<=>?@";
static const std::string ASCII_SPECIAL_MH = "[\\]^_`";
static const std::string ASCII_SPECIAL_HI = "{|}~";
static const std::string ASCII_SPECIAL = ASCII_SPECIAL_LO + ASCII_SPECIAL_ML + ASCII_SPECIAL_MH + ASCII_SPECIAL_HI;
static const std::string ALPHANUMERIC = NUMERIC + UPPER + LOWER;
static const std::string ASCII = ASCII_SPECIAL_LO + NUMERIC + ASCII_SPECIAL_ML + UPPER + ASCII_SPECIAL_MH + LOWER + ASCII_SPECIAL_HI;
// Based on an analysis of cracked passwords
static const std::string COMMON = "a1e20ion9r3sl85746tumdychbkgfpvjwzxqAE._SRMNILTODCBKPHG-UF!YJVWZ@QX*$#?& :+/";
static const std::string COMMON_SHORT = "a1e20ion9r3sl85746tumdychbkgfpvjwzxqAE._SRMNILTODCBKPHG-UF!YJVWZ@QX";

class WordGenerator
{
public:
    WordGenerator(void) : m_Charset(ALPHANUMERIC) {};
    WordGenerator(std::string_view Charset) : m_Charset(Charset) {};
    WordGenerator(std::string_view Charset, std::string_view Prefix, std::string_view Postfix)
        : m_Charset(Charset), m_Prefix(Prefix), m_Postfix(Postfix) {};
    static std::string GenerateWord(const uint64_t Value, std::string_view Charset);
    static std::string GenerateWordReversed(const uint64_t Value, std::string_view Charset);
    static std::string GenerateWord(const mpz_class& Value, std::string_view Charset);
    static std::string GenerateWordReversed(const mpz_class& Value, std::string_view Charset);
    static const size_t GenerateWord(char* Destination, const size_t DestSize, const mpz_class& Value, std::string_view Charset);
    static const size_t GenerateWordReversed(char* Destination, const size_t DestSize, const mpz_class& Value, std::string_view Charset);
    static const size_t GenerateWord(char* Destination, const size_t DestSize, const uint64_t Value, std::string_view Charset);
    static const size_t GenerateWordReversed(char* Destination, const size_t DestSize, const uint64_t Value, std::string_view Charset);
    const std::string Generate(const uint64_t Value);
    const std::string GenerateReversed(const uint64_t Value);
    const std::string Generate(const mpz_class& Value);
    const std::string GenerateReversed(const mpz_class& Value);
    const size_t Generate(char* Destination, const size_t DestSize, const mpz_class& Value);
    const size_t GenerateReversed(char* Destination, const size_t DestSize, const mpz_class& Value);
    static const std::vector<uint8_t> GenerateParsingLookupTable(std::string_view Charset);
    const void GenerateParsingLookupTable(void) { m_LookupTable = GenerateParsingLookupTable(m_Charset); };
    static const mpz_class Parse(std::string_view Word, std::string_view Charset);
    static const mpz_class ParseReversed(std::string_view Word, std::string_view Charset);
    static const mpz_class Parse(std::string_view Word, const std::vector<uint8_t>& LookupTable);
    static const mpz_class ParseReversed(std::string_view Word, const std::vector<uint8_t>& LookupTable);
    static const uint64_t Parse64(std::string_view Word, std::string_view Charset);
    static const uint64_t ParseReversed64(std::string_view Word, std::string_view Charset);
    static const uint64_t Parse64(std::string_view Word, std::span<const uint8_t> LookupTable);
    static const uint64_t ParseReversed64(std::string_view Word, std::span<const uint8_t> LookupTable);
    const mpz_class Parse(std::string_view Word) const { return Parse(Word, m_Charset); };
    const mpz_class ParseReversed(std::string_view Word) const { return ParseReversed(Word, m_Charset); };
    const uint64_t Parse64(std::string_view Word) const { return Parse64(Word, m_Charset); };
    const uint64_t ParseReversed64(std::string_view Word) const { return ParseReversed64(Word, m_Charset); };
    const uint64_t Parse64Lookup(std::string_view Word) const { return Parse64(Word, m_LookupTable); };
    void SetPrefix(std::string_view Prefix) { m_Prefix = Prefix; };
    void SetPostfix(std::string_view Postfix) { m_Postfix = Postfix; };
    const std::string_view GetCharset(void) { return m_Charset; };
    static void WordLengthIndex(const size_t WordLength, std::string_view Charset, uint64_t& Index);
    static void WordLengthIndex(const size_t WordLength, std::string_view Charset, mpz_class& Index);
    static const mpz_class WordLengthIndex(const size_t WordLength, std::string_view Charset);
    static const uint64_t WordLengthIndex64(const size_t WordLength, std::string_view Charset);
    const mpz_class WordLengthIndex(const size_t WordLength) { return WordGenerator::WordLengthIndex(WordLength, m_Charset); };
private:
    std::string_view m_Charset;
    std::string_view m_Prefix;
    std::string_view m_Postfix;
    std::vector<uint8_t> m_LookupTable;
};

const std::string_view
ParseCharset(
    const std::string_view Name
);

#endif /* WordGenerator_hpp */
