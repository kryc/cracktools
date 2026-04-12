//
//  BloomFilter.cpp
//  SimdRainbowCrack
//
//  Created on 11/04/2026.
//

#include "BloomFilter.hpp"

// MurmurHash3-style 64-bit finalizer for mixing
static inline uint64_t
Mix64(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

BloomFilter::BloomFilter(
    size_t expectedCount
)
{
    // Minimum size: 64 bits
    m_NumBits = std::max(size_t(64),
        static_cast<size_t>(expectedCount * kBitsPerElement));
    m_Bits.resize(m_NumBits, false);
}

BloomFilter::HashArray
BloomFilter::GetHashes(
    uint64_t value
) const
{
    HashArray hashes;
    uint64_t h1 = Mix64(value);
    uint64_t h2 = Mix64(value ^ 0x9e3779b97f4a7c15ULL);
    for (size_t i = 0; i < kHashCount; i++)
    {
        hashes[i] = (h1 + i * h2) % m_NumBits;
    }
    return hashes;
}

BloomFilter::HashArray
BloomFilter::GetHashes(
    __uint128_t value
) const
{
    auto lo = static_cast<uint64_t>(value);
    auto hi = static_cast<uint64_t>(value >> 64);
    uint64_t h1 = Mix64(lo ^ (hi * 0x9e3779b97f4a7c15ULL));
    uint64_t h2 = Mix64(hi ^ (lo * 0x517cc1b727220a95ULL));
    HashArray hashes;
    for (size_t i = 0; i < kHashCount; i++)
    {
        hashes[i] = (h1 + i * h2) % m_NumBits;
    }
    return hashes;
}

bool
BloomFilter::Insert(
    uint32_t value
)
{
    return Insert(static_cast<uint64_t>(value));
}

bool
BloomFilter::Insert(
    uint64_t value
)
{
    auto hashes = GetHashes(value);

    bool allSet = true;
    for (auto h : hashes)
    {
        if (!m_Bits[h])
        {
            allSet = false;
        }
        m_Bits[h] = true;
    }

    if (!allSet)
    {
        m_Count++;
        return true;  // New element
    }
    return false;  // Probably already present
}

bool
BloomFilter::Insert(
    __uint128_t value
)
{
    auto hashes = GetHashes(value);

    bool allSet = true;
    for (auto h : hashes)
    {
        if (!m_Bits[h])
        {
            allSet = false;
        }
        m_Bits[h] = true;
    }

    if (!allSet)
    {
        m_Count++;
        return true;
    }
    return false;
}

bool
BloomFilter::MayContain(
    uint32_t value
) const
{
    return MayContain(static_cast<uint64_t>(value));
}

bool
BloomFilter::MayContain(
    uint64_t value
) const
{
    auto hashes = GetHashes(value);

    for (auto h : hashes)
    {
        if (!m_Bits[h])
        {
            return false;
        }
    }
    return true;
}

bool
BloomFilter::MayContain(
    __uint128_t value
) const
{
    auto hashes = GetHashes(value);

    for (auto h : hashes)
    {
        if (!m_Bits[h])
        {
            return false;
        }
    }
    return true;
}

void
BloomFilter::Clear(
    void
)
{
    m_Bits.assign(m_NumBits, false);
    m_Count = 0;
}
