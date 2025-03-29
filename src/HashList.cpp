//
//  HashList.cpp
//  HashList
//
//  Created by Kryc on 12/11/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <algorithm>
#include <assert.h>
#include <iostream>
#include <ranges>
#include <span>
#include <stdio.h>
#include <cstring>
#include <sys/mman.h>

#include "HashList.hpp"

// The threshold at which we perform an index
// of all of the hashes and perform fast lookup
#define FAST_LOOKUP_THRESHOLD (65536 * 4)
// The threshold below which we just perform linear
// lookups and not bother with binary search
#define LINEAR_LOOKUP_THRESHOLD (512)

static const uint32_t
Bitmask(
    std::span<const uint8_t> Value,
    const size_t BitmaskSize
)
{
    uint32_t v32 = *(uint32_t*)Value.data();
#ifndef __ARM__
    v32 = __bswap_32(v32);
#endif
    v32 >>= (32 - BitmaskSize);
    return v32;
}

/* static */
const bool
HashList::Lookup(
    std::span<const uint8_t> HashList,
    std::span<const uint8_t> Hash
)
{
    const size_t Size = HashList.size();
    const size_t HashSize = Hash.size();
    const size_t Count = Size / HashSize;

    assert(Size % HashSize == 0);

    ssize_t low = 0;
    ssize_t high = Count - 1;

    while (low <= high)
    {
        const ssize_t mid = low + (high - low) / 2;
        auto subspan = HashList.subspan(mid * HashSize, HashSize);
        int cmp = std::memcmp(subspan.data(), Hash.data(), HashSize);
        if (cmp == 0)
        {
            return true;
        }
        else if (cmp < 0)
        {
            low = mid + 1;
        }
        else
        {
            high = mid - 1;
        }
    }

    return false;
}

inline const bool
HashList::LookupLinear(
    std::span<const uint8_t> HashList,
    std::span<const uint8_t> Hash
)
{
#ifdef SAFEANDSLOW
    for (const auto& subspan : HashList | std::ranges::views::slide(Hash.size())) {
        if (std::equal(subspan.begin(), subspan.end(), Hash.begin())) {
            return true;
        }
    }
    return false;
#else
    for (size_t offset = 0; offset < HashList.size(); offset += Hash.size())
    {
        auto subspan = HashList.subspan(offset, Hash.size());
        if (std::equal(subspan.begin(), subspan.end(), Hash.begin())) {
            return true;
        }
    }
    return false;
#endif
}

const bool
HashList::Initialize(
    const std::filesystem::path Path,
    const size_t DigestLength,
    const bool ShouldSort
)
{
    m_Path = Path;
    m_DigestLength = DigestLength;

    // Get the file size
    const size_t size = std::filesystem::file_size(m_Path);

    m_BinaryHashFileHandle = fopen(m_Path.c_str(), "r");
    if (m_BinaryHashFileHandle == nullptr)
    {
        std::cerr << "Error: unable to open binary hash file" << std::endl;
        return false;
    }

    if (size % m_DigestLength != 0)
    {
        std::cerr << "Error: length of hash file does not match digest" << std::endl;
        return false;
    }

    // Mmap the file
    const uint8_t * const base = (uint8_t*)mmap(nullptr, size, PROT_READ, MAP_SHARED, fileno(m_BinaryHashFileHandle), 0);
    if (base == nullptr)
    {
        std::cerr << "Error: Unable to map hashes file" << std::endl;
        return false;
    }

    m_Data = std::span<const uint8_t>(base, size);

    if (ShouldSort)
    {
        Sort();
    }

    return InitializeInternal();
}

const bool
HashList::Initialize(
    std::span<const uint8_t> Data,
    const size_t DigestLength,
    const bool Sort
)
{
    m_Data = Data;
    m_DigestLength = DigestLength;
    const size_t count = Data.size() / m_DigestLength;

    std::cerr << "HashList::Initialize: " << count << " hashes" << std::endl;
    
    if (count < LINEAR_LOOKUP_THRESHOLD)
    {
        std::cerr << "Using linear search" << std::endl;
    }
    else if (count < FAST_LOOKUP_THRESHOLD)
    {
        std::cerr << "Using binary search" << std::endl;
    }
    else
    {
        std::cerr << "Using fast lookup" << std::endl;
    }
    
    if (Sort)
    {
        this->Sort();
    }

    return InitializeInternal();
}

const bool
HashList::InitializeInternal(
    void
)
{
    constexpr size_t READAHEAD = 512;
    constexpr size_t INVALID_OFFSET = std::numeric_limits<size_t>::max();

    auto ret = madvise((void*)m_Data.data(), m_Data.size(), MADV_RANDOM|MADV_WILLNEED);
    if (ret != 0)
    {
        std::cerr << "Madvise not happy" << std::endl;
    }

    // Build lookup table
    std::cerr << "Indexing hash table." << std::flush;

    m_LookupTable.resize(1 << m_BitmaskSize);
    std::vector<size_t> offsets(m_LookupTable.size(), INVALID_OFFSET);

    // For lists larger than FAST_LOOKUP_THRESHOLD
    // We need to index the offset list
    if (GetCount() >= FAST_LOOKUP_THRESHOLD)
    {
        // First pass
        std::cerr << "\rIndexing hash table. Pass 1" << std::flush;
        for (size_t i = 0; i < GetCount(); i+= READAHEAD)
        {
            auto hash = m_Data.subspan(i * m_DigestLength, m_DigestLength);
            const uint32_t index = Bitmask(hash, m_BitmaskSize);
            if (offsets[index] == INVALID_OFFSET)
            {
                offsets[index] = i;
            }
        }

        // Handle the last entry
        auto last = m_Data.subspan((GetCount() - 1) * m_DigestLength, m_DigestLength);
        const uint32_t lastIndex = Bitmask(last, m_BitmaskSize);
        if (offsets[lastIndex] == INVALID_OFFSET)
        {
            offsets[lastIndex] = GetCount() - 1;
        }

        // Third pass -> N'th pass
        // Loop over each known endpoint and check the previous entry
        size_t pass = 2;
        bool foundNewEntry;
        do
        {
            foundNewEntry = false;
            std::cerr << "\rIndexing hash table. Pass " << pass++ << std::flush;
            for (size_t i = 0; i < m_LookupTable.size(); i++)
            {
                if (offsets[i] == INVALID_OFFSET || offsets[i] == 0)
                {
                    continue;
                }

                // Walk backwards until we find the previous
                while (offsets[i] > 0)
                {
                    const size_t previousOffset = offsets[i] - 1;
                    auto previous = m_Data.subspan(previousOffset * m_DigestLength, m_DigestLength);
                    const uint32_t index = Bitmask(previous, m_BitmaskSize);
                    if (index == i)
                    {
                        offsets[i] = previousOffset;
                    }
                    else
                    {
                        if (offsets[index] == INVALID_OFFSET)
                        {
                            offsets[index] = previousOffset;
                            foundNewEntry = true;
                        }
                        break;
                    }
                }
            }
        } while(foundNewEntry);

        // Calculate the counts
        // We walk through each item, look for the next offset
        // and calculate the distance between them
        for (size_t i = 0; i < m_LookupTable.size(); i++)
        {
            // Find the next closest offset
            if (offsets[i] == INVALID_OFFSET)
            {
                continue;
            }

            for (size_t j = i + 1; j < m_LookupTable.size(); j++)
            {
                if (offsets[j] != INVALID_OFFSET)
                {
                    assert(offsets[j] > offsets[i]);
                    const size_t Count = offsets[j] - offsets[i];
                    m_LookupTable[i] = m_Data.subspan(offsets[i] * m_DigestLength, Count * m_DigestLength);
                    break;
                }
            }
        }

        // Handle the last entry
        for (size_t i = m_LookupTable.size() - 1; i > 0; i--)
        {
            if (offsets[i] != INVALID_OFFSET)
            {
                const size_t Count = GetCount() - offsets[i];
                m_LookupTable[i] = m_Data.subspan(offsets[i] * m_DigestLength, Count * m_DigestLength);
            }
        }
    }

    std::cerr << std::endl;

    return true;
}

const bool
HashList::LookupLinear(
    std::span<const uint8_t> Hash
) const
{
    return LookupLinear(m_Data, Hash);
}



const bool
HashList::LookupFast(
    std::span<const uint8_t> Hash
) const
{
    const uint32_t index = Bitmask(Hash, m_BitmaskSize);
    auto subspan = m_LookupTable[index];

    if (subspan.empty())
    {
        return false;
    }

    return Lookup(
        subspan,
        Hash
    );
}

const bool
HashList::LookupBinary(
    std::span<const uint8_t> Hash
) const
{
    return Lookup(
        m_Data,
        Hash
    );
}

const bool
HashList::Lookup(
    std::span<const uint8_t> Hash
) const
{
    const size_t count = GetCount();
    if (count >= FAST_LOOKUP_THRESHOLD)
    {
        return LookupFast(Hash);
    }
    else if (count <= LINEAR_LOOKUP_THRESHOLD)
    {
        return LookupLinear(Hash);
    }
    else
    {
        return LookupBinary(Hash);
    }
}

#ifdef __APPLE__
int
Compare(
    void* Length,
    const void* Value1,
    const void* Value2
)
{
    return memcmp(Value1, Value2, (size_t)Length);
}
#endif

void
HashList::Sort(
    void
)
{
#ifdef __APPLE__
    qsort_r(m_Targets, m_TargetsCount, m_HashWidth, (void*)m_HashWidth, Compare);
#else
    qsort_r((void*)m_Data.data(), GetCount(), m_DigestLength, (__compar_d_fn_t)memcmp, (void*)m_DigestLength);
#endif
}