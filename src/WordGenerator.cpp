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
#include <string>
#include <vector>
#include <gmpxx.h>

#include "WordGenerator.hpp"

#define D(val) (std::cout << "DBG: " << val << std::endl)

// Static
std::string
WordGenerator::GenerateWord(
    const uint64_t Value,
    const std::string& Charset
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
    const std::string& Charset
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
    const std::string& Charset
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
    const std::string& Charset
)
{
    std::string word = GenerateWord(Value, Charset);
    std::reverse(std::begin(word), std::end(word));
    return word;
}

// Static
const size_t
WordGenerator::GenerateWord(
    char* Destination,
    const size_t DestSize,
    const mpz_class& Value,
    const std::string& Charset
)
{
    mpz_class i(Value);
    mpz_class q,r;
    size_t length = 0;

    do {
        mpz_fdiv_qr_ui(i.get_mpz_t(), r.get_mpz_t(), i.get_mpz_t(), Charset.length());
        if (length == DestSize)
            return (size_t)-1;
        Destination[length++] = Charset[r.get_ui()];
    } while (i > 0);

    return length;
}

// Static
const size_t
WordGenerator::GenerateWordReversed(
    char* Destination,
    const size_t DestSize,
    const mpz_class& Value,
    const std::string& Charset
)
{
    size_t length = GenerateWord(Destination, DestSize, Value, Charset);
    std::reverse(Destination, Destination + length);
    return length;
}

// Static
const size_t
WordGenerator::GenerateWord(
    char* Destination,
    const size_t DestSize,
    const uint64_t Value,
    const std::string& Charset
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
    char* Destination,
    const size_t DestSize,
    const uint64_t Value,
    const std::string& Charset
)
{
    size_t length = GenerateWord(Destination, DestSize, Value, Charset);
    std::reverse(Destination, Destination + length);
    return length;
}

const std::string
WordGenerator::Generate(
    const uint64_t Value
)
{
    return m_Prefix + GenerateWord(Value, m_Charset) + m_Postfix;
}

const std::string
WordGenerator::GenerateReversed(
    const uint64_t Value
)
{
    return m_Prefix + GenerateWordReversed(Value, m_Charset) + m_Postfix;
}

const size_t
WordGenerator::Generate(
    char* Destination,
    const size_t DestSize,
    const mpz_class& Value
)
{
    return GenerateWord(Destination, DestSize, Value, m_Charset);
}

const size_t
WordGenerator::GenerateReversed(
    char* Destination,
    const size_t DestSize,
    const mpz_class& Value
)
{
    return GenerateWordReversed(Destination, DestSize, Value, m_Charset);
}

const std::string
WordGenerator::Generate(
    const mpz_class& Value
)
{
    return m_Prefix + GenerateWord(Value, m_Charset) + m_Postfix;
}

const std::string
WordGenerator::GenerateReversed(
    const mpz_class& Value
)
{
    return m_Prefix + GenerateWordReversed(Value, m_Charset) + m_Postfix;
}

// static
const mpz_class
WordGenerator::Parse(
    const std::string& Word,
    const std::string& Charset
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
    const std::string& Word,
    const std::string& Charset
)
{
    std::string reversed = Word;
    std::reverse(reversed.begin(), reversed.end());
    return WordGenerator::Parse(reversed, Charset);
}

// static
const mpz_class
WordGenerator::Parse(
    const std::string& Word,
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
    const std::string& Word,
    const std::vector<uint8_t>& LookupTable
)
{
    std::string reversed = Word;
    std::reverse(reversed.begin(), reversed.end());
    return WordGenerator::Parse(reversed, LookupTable);
}

// static
const std::vector<uint8_t>
WordGenerator::GenerateParsingLookupTable(
    const std::string& Charset
)
{
    std::vector<uint8_t> table;

    table.resize(256);
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
    const std::string& Charset,
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
    const std::string& Charset,
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
    const std::string& Charset
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
    const std::string& Charset
)
{
    uint64_t index;
    WordLengthIndex(WordLength, Charset, index);
    return index;
}

const std::string&
ParseCharset(
    const std::string& Name
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