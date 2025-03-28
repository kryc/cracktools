//
//  HashList.cpp
//  HashList
//
//  Created by Kryc on 12/11/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <assert.h>
#include <iostream>
#include <stdio.h>
#include <string.h>
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
    const uint8_t* const Value,
    const size_t Size
)
{
    uint32_t v32 = *(uint32_t*)Value;
#ifndef __ARM__
    v32 = __bswap_32(v32);
#endif
    v32 >>= (32 - Size);
    return v32;
}

const bool
HashList::Lookup(
    const uint8_t* const Base,
    const size_t Size,
    const uint8_t* const Hash,
    const size_t HashSize
)
{
    assert(Size % HashSize == 0);
    // Perform the search
    const uint8_t* base = Base;
    const uint8_t* top = Base + Size;

    uint8_t* low = (uint8_t*)base;
    uint8_t* high = (uint8_t*)top - HashSize;
    uint8_t* mid;

    while (low <= high)
    {
        mid = low + ((high - low) / (2 * HashSize)) * HashSize;
        int cmp = memcmp(mid, Hash, HashSize);
        if (cmp == 0)
        {
            return true;
        }
        else if (cmp < 0)
        {
            low = mid + HashSize;
        }
        else
        {
            high = mid - HashSize;
        }
    }

    return false;
}

const bool
HashList::LookupLinear(
    const uint8_t* const Base,
    const size_t Size,
    const uint8_t* const Hash,
    const size_t HashSize
)
{
    for (const uint8_t* offset = Base; offset < Base + Size; offset += HashSize)
    {
        if (memcmp(offset, Hash, HashSize) == 0)
        {
            return true;
        }
    }
    return false;
}

const bool
HashList::Initialize(
    const std::filesystem::path Path,
    const size_t DigestLength,
    const bool Sort
)
{
    m_Path = Path;
    m_DigestLength = DigestLength;

    // Get the file size
    m_Size = std::filesystem::file_size(m_Path);
    m_Count = m_Size / m_DigestLength;

    m_BinaryHashFileHandle = fopen(m_Path.c_str(), "r");
    if (m_BinaryHashFileHandle == nullptr)
    {
        std::cerr << "Error: unable to open binary hash file" << std::endl;
        return false;
    }

    if (m_Size % m_DigestLength != 0)
    {
        std::cerr << "Error: length of hash file does not match digest" << std::endl;
        return false;
    }

    // Mmap the file
    m_Base = (uint8_t*)mmap(nullptr, m_Size, PROT_READ, MAP_SHARED, fileno(m_BinaryHashFileHandle), 0);
    if (m_Base == nullptr)
    {
        std::cerr << "Error: Unable to map hashes file" << std::endl;
        return false;
    }

    if (Sort)
    {
        this->Sort();
    }

    return InitializeInternal();
}

const bool
HashList::Initialize(
    uint8_t* Base,
    const size_t Size,
    const size_t DigestLength,
    const bool Sort
)
{
    m_Base = Base;
    m_Size = Size;
    m_DigestLength = DigestLength;
    m_Count = m_Size / m_DigestLength;
    
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
    auto ret = madvise(m_Base, m_Size, MADV_RANDOM|MADV_WILLNEED);
    if (ret != 0)
    {
        std::cerr << "Madvise not happy" << std::endl;
        return false;
    }

    // Build lookup table
    std::cerr << "Indexing hash table." << std::flush;

    m_LookupTable.resize(1 << m_BitmaskSize);
    for (auto &entry : m_LookupTable)
    {
        entry.Offset = INVALID_OFFSET;
        entry.Count = 0;
    }

    constexpr size_t READAHEAD = 512;

    // For lists larger than FAST_LOOKUP_THRESHOLD
    // We need to index the offset list
    if (m_Count >= FAST_LOOKUP_THRESHOLD)
    {
        // First pass
        std::cerr << "\rIndexing hash table. Pass 1" << std::flush;
        for (size_t i = 0; i < m_Count; i+= READAHEAD)
        {
            const uint8_t* const pointer = m_Base + (i * m_DigestLength);
            const uint32_t index = Bitmask(pointer, m_BitmaskSize);
            if (m_LookupTable[index].Offset == INVALID_OFFSET)
            {
                m_LookupTable[index].Offset = i;
            }
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
                if (m_LookupTable[i].Offset == INVALID_OFFSET || m_LookupTable[i].Offset == 0)
                {
                    continue;
                }

                const uint8_t* pointer = m_Base + (m_LookupTable[i].Offset * m_DigestLength);

                // Walk backwards until we find the previous
                while (pointer > m_Base)
                {
                    pointer -= m_DigestLength;
                    const uint32_t index = Bitmask(pointer, m_BitmaskSize);
                    if (index == i)
                    {
                        m_LookupTable[i].Offset = (pointer - m_Base) / m_DigestLength;
                    }
                    else
                    {
                        if (m_LookupTable[index].Offset == INVALID_OFFSET)
                        {
                            m_LookupTable[index].Offset = (pointer - m_Base) / m_DigestLength;
                            foundNewEntry = true;
                        }
                        break;
                    }
                }
            }
        } while(foundNewEntry);

        std::cerr << std::endl;

        // Calculate the counts
        // We walk through each item, look for the next offset
        // and calculate the distance between them
        for (size_t i = 0; i < m_LookupTable.size(); i++)
        {
            // Find the next closest offset
            if (m_LookupTable[i].Offset == INVALID_OFFSET)
            {
                continue;
            }

            for (size_t j = i + 1; j < m_LookupTable.size(); j++)
            {
                if (m_LookupTable[j].Offset != INVALID_OFFSET)
                {
                    assert(m_LookupTable[j].Offset > m_LookupTable[i].Offset);
                    m_LookupTable[i].Count = m_LookupTable[j].Offset - m_LookupTable[i].Offset;
                    break;
                }
            }
        }

        // Handle the last entry
        for (size_t i = m_LookupTable.size() - 1; i > 0; i--)
        {
            if (m_LookupTable[i].Offset != INVALID_OFFSET)
            {
                const uint8_t* const end = m_Base + m_Size;
                const uint8_t* const pointer = m_Base + (m_LookupTable[i].Offset * m_DigestLength);
                m_LookupTable[i].Count = (end - pointer) / m_DigestLength;
            }
        }
    }

    return true;
}

const bool
HashList::LookupLinear(
    const uint8_t* Hash
) const
{
    for (uint8_t* offset = m_Base; offset < m_Base + m_Size; offset += m_DigestLength)
    {
        if (memcmp(offset, Hash, m_DigestLength) == 0)
        {
            return true;
        }
    }
    return false;
}

const bool
HashList::LookupFast(
    const uint8_t* Hash
) const
{
    const uint32_t index = Bitmask(Hash, m_BitmaskSize);
    const LookupTable& entry = m_LookupTable[index];

    if (entry.Count == 0)
    {
        return false;
    }

    return Lookup(
        m_Base + entry.Offset * m_DigestLength,
        entry.Count * m_DigestLength,
        Hash,
        m_DigestLength
    );
}

const bool
HashList::LookupBinary(
    const uint8_t* Hash
) const
{
    return Lookup(
        m_Base,
        m_Size,
        Hash,
        m_DigestLength
    );
}

const bool
HashList::Lookup(
    const uint8_t* Hash
) const
{
    if (m_Count >= FAST_LOOKUP_THRESHOLD)
    {
        return LookupFast(Hash);
    }
    else if (m_Count <= LINEAR_LOOKUP_THRESHOLD)
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
    qsort_r(m_Base, m_Count, m_DigestLength, (__compar_d_fn_t)memcmp, (void*)m_DigestLength);
#endif
}