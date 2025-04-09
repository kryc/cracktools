//
//  WordGenerator.hpp
//  SimdCrack
//
//  Created by Kryc on 20/01/2024.
//  Copyright © 2020 Kryc. All rights reserved.
//

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <gmpxx.h>

#include "WordGenerator.hpp"

#define D(val) (std::cout << "DBG: " << val << std::endl)

// Static
std::string
WordGenerator::GenerateWord(
    const uint64_t Value,
    std::string_view Charset
)
{
    std::string out;
    uint64_t r, i = Value;
    const size_t charsetSize = Charset.size();
    
    do
    {
        r = i % charsetSize;
        i /= charsetSize;
        out += Charset[r];
        
    } while (i > 0);
     
    return out;
}

// Static
std::string
WordGenerator::GenerateWordReversed(
    const uint64_t Value,
    std::string_view Charset
)
{
    std::string word = GenerateWord(Value, Charset);
    std::reverse(std::begin(word), std::end(word));
    return word;
}

// Static
std::string 
WordGenerator::GenerateWord(
    const mpz_class& Value,
    std::string_view Charset
)
{
    std::string out;

    mpz_class i(Value);
    mpz_class r;
    
    do {
        mpz_fdiv_qr_ui(i.get_mpz_t(), r.get_mpz_t(), i.get_mpz_t(), Charset.length());
        out += Charset[r.get_ui()];
    } while (i > 0);

    return out;
}

// static
std::string
WordGenerator::GenerateWordReversed(
    const mpz_class& Value,
    std::string_view Charset
)
{
    std::string word = GenerateWord(Value, Charset);
    std::reverse(std::begin(word), std::end(word));
    return word;
}

// Static
const size_t
WordGenerator::GenerateWord(
    std::span<char> Destination,
    const mpz_class& Value,
    std::string_view Charset
)
{
    mpz_class i(Value);
    mpz_class q,r;
    size_t length = 0;

    do {
        mpz_fdiv_qr_ui(i.get_mpz_t(), r.get_mpz_t(), i.get_mpz_t(), Charset.length());
        if (length == Destination.size())
            return (size_t)-1;
        Destination[length++] = Charset[r.get_ui()];
    } while (i > 0);

    return length;
}

// Static
const size_t
WordGenerator::GenerateWordReversed(
    std::span<char> Destination,
    const mpz_class& Value,
    std::string_view Charset
)
{
    size_t length = GenerateWord(Destination, Value, Charset);
    std::reverse(Destination.begin(), Destination.end());
    return length;
}

// Static
const size_t
WordGenerator::GenerateWord(
    std::span<char> Destination,
    const uint64_t Value,
    std::string_view Charset
)
{
    uint64_t r, i = Value;
    size_t length = 0;
    const size_t charsetSize = Charset.size();

    do {
        r = i % charsetSize;
        i /= charsetSize;
        Destination[length++] = Charset[r];
    } while (i > 0);

    return length;
}

// Static
const size_t
WordGenerator::GenerateWordReversed(
    std::span<char> Destination,
    const uint64_t Value,
    std::string_view Charset
)
{
    size_t length = GenerateWord(Destination, Value, Charset);
    std::reverse(Destination.begin(), Destination.end());
    return length;
}

const std::string
WordGenerator::Generate(
    const uint64_t Value
)
{
    return std::string(m_Prefix) + GenerateWord(Value, m_Charset) + std::string(m_Postfix);
}

const std::string
WordGenerator::GenerateReversed(
    const uint64_t Value
)
{
    return std::string(m_Prefix) + GenerateWordReversed(Value, m_Charset) + std::string(m_Postfix);
}

const size_t
WordGenerator::Generate(
    std::span<char> Destination,
    const mpz_class& Value
)
{
    return GenerateWord(Destination, Value, m_Charset);
}

const size_t
WordGenerator::GenerateReversed(
    std::span<char> Destination,
    const mpz_class& Value
)
{
    return GenerateWordReversed(Destination, Value, m_Charset);
}

const size_t
WordGenerator::Generate(
    std::span<char> Destination,
    const uint64_t Value
)
{
    return GenerateWord(Destination, Value, m_Charset);
}

const size_t
WordGenerator::GenerateReversed(
    std::span<char> Destination,
    const uint64_t Value
)
{
    return GenerateWordReversed(Destination, Value, m_Charset);
}

const std::string
WordGenerator::Generate(
    const mpz_class& Value
)
{
    return std::string(m_Prefix) + GenerateWord(Value, m_Charset) + std::string(m_Postfix);
}

const std::string
WordGenerator::GenerateReversed(
    const mpz_class& Value
)
{
    return std::string(m_Prefix) + GenerateWordReversed(Value, m_Charset) + std::string(m_Postfix);
}

// static
const mpz_class
WordGenerator::Parse(
    std::string_view Word,
    std::string_view Charset
)
{
    mpz_class num;
    for (const char& c : Word)
    {
        num = num * Charset.size() + (Charset.find_first_of(c) + 1);
    }
    return num;
}

// static
const mpz_class
WordGenerator::ParseReversed(
    std::string_view Word,
    std::string_view Charset
)
{
    std::string reversed(Word);
    std::reverse(reversed.begin(), reversed.end());
    return WordGenerator::Parse(reversed, Charset);
}

// static
const mpz_class
WordGenerator::Parse(
    std::string_view Word,
    const std::vector<uint8_t>& LookupTable
)
{
    mpz_class num;
    for (const char& c : Word)
    {
        num = num * LookupTable[256] + (LookupTable[c] + 1);
    }
    return num;
}

// static
const mpz_class
WordGenerator::ParseReversed(
    std::string_view Word,
    const std::vector<uint8_t>& LookupTable
)
{
    std::string reversed(Word);
    std::reverse(reversed.begin(), reversed.end());
    return WordGenerator::Parse(reversed, LookupTable);
}

/* static */ const uint64_t 
WordGenerator::Parse64(
    std::string_view Word,
    std::string_view Charset
)
{
    uint64_t num = 0;
    for (const char& c : Word)
    {
        num = num * Charset.size() + (Charset.find_first_of(c) + 1);
    }
    return num;
}

/* static */ const uint64_t
WordGenerator::ParseReversed64(
    std::string_view Word,
    std::string_view Charset
)
{
    std::string reversed(Word);
    std::reverse(reversed.begin(), reversed.end());
    return WordGenerator::Parse64(reversed, Charset);
}

/* static */ const uint64_t
WordGenerator::Parse64(
    std::string_view Word,
    std::span<const uint8_t> LookupTable
)
{
    uint64_t num = 0;
    for (const char& c : Word)
    {
        num = num * LookupTable[256] + (LookupTable[c] + 1);
    }
    return num;
}

/* static */ const uint64_t
WordGenerator::ParseReversed64(
    std::string_view Word,
    std::span<const uint8_t> LookupTable
)
{
    std::string reversed(Word);
    std::reverse(reversed.begin(), reversed.end());
    return WordGenerator::Parse64(reversed, LookupTable);
}

// static
const std::vector<uint8_t>
WordGenerator::GenerateParsingLookupTable(
    std::string_view Charset
)
{
    std::vector<uint8_t> table;

    table.resize(257);
    memset(&table[0], 0, table.size());
    table[256] = Charset.size();

    for (const char& c : Charset)
    {
        table[c] = Charset.find_first_of(c);
    }

    return table;
}

// static
void
WordGenerator::WordLengthIndex(
    const size_t WordLength,
    std::string_view Charset,
    uint64_t& Index
)
{
    uint64_t product = 0;
    Index = 0;
    for (size_t i = 0; i < WordLength; i++)
    {
        product = std::pow(Charset.size(), i);
        Index += product;
    }
}

// static
void
WordGenerator::WordLengthIndex(
    const size_t WordLength,
    std::string_view Charset,
    mpz_class& Index
)
{
    Index = 0;
    mpz_class product;
    for (size_t i = 0; i < WordLength; i++)
    {
        mpz_ui_pow_ui(product.get_mpz_t(), Charset.size(), i);
        Index += product;
    }
}

// Static
const mpz_class
WordGenerator::WordLengthIndex(
    const size_t WordLength,
    std::string_view Charset
)
{
    mpz_class index;
    WordLengthIndex(WordLength, Charset, index);
    return index;
}

// static
const uint64_t
WordGenerator::WordLengthIndex64(
    const size_t WordLength,
    std::string_view Charset
)
{
    uint64_t index;
    WordLengthIndex(WordLength, Charset, index);
    return index;
}

const std::string_view
ParseCharset(
    std::string_view Name
)
{
    if (Name == "ASCII" || Name == "ascii")
    {
        return ASCII;
    }
    else if (Name == "lower")
    {
        return LOWER;
    }
    else if (Name == "upper")
    {
        return UPPER;
    }
    else if (Name == "numeric" || Name == "num")
    {
        return NUMERIC;
    }
    else if (Name == "alphanumeric" || Name == "alnum")
    {
        return ALPHANUMERIC;
    }
    else if (Name == "common")
    {
        return COMMON;
    }
    else if (Name == "commonshort")
    {
        return COMMON_SHORT;
    }
    return ASCII;
}