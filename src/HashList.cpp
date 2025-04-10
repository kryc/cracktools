//
//  HashList.cpp
//  HashList
//
//  Created by Kryc on 12/11/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <algorithm>
#include <assert.h>
#include <bit>
#include <cmath>
#include <iostream>
#include <ranges>
#include <span>
#include <stdio.h>
#include <cstring>
#include <sys/mman.h>

#include "HashList.hpp"
#include "UnsafeBuffer.hpp"

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
    uint32_t v32 = cracktools::Uint32FromLittleEndian(Value);
#ifndef __ARM__
    v32 = std::byteswap(v32);
#endif
    v32 >>= (32 - BitmaskSize);
    return v32;
}

std::optional<size_t>
HashList::FindBinaryInternal(
    std::span<const uint8_t> HashList,
    std::span<const uint8_t> Hash
) const
{
    const size_t count = HashList.size() / m_RowWidth;
    ssize_t low = 0;
    ssize_t high = count - 1;

    while (low <= high)
    {
        const ssize_t mid = low + (high - low) / 2;
        auto midhash = GetHash(HashList, mid);
        int cmp = std::memcmp(midhash.data(), Hash.data(), m_DigestLength);
        if (cmp == 0)
        {
            return mid;
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

    return std::nullopt;
}

std::optional<size_t>
HashList::FindLinearInternal(
    std::span<const uint8_t> HashList,
    std::span<const uint8_t> Hash
) const
{
    const size_t count = HashList.size() / m_RowWidth;
    for (size_t offset = 0; offset < count; offset++)
    {
        auto cmphash = GetHash(HashList, offset);
        if (std::equal(cmphash.begin(), cmphash.end(), Hash.begin())) {
            return offset;
        }
    }
    return std::nullopt;
}

void
HashList::Clear(
    void
)
{
    if (m_BinaryHashFileHandle != nullptr)
    {
        cracktools::UnmapFileSpan(m_Data, m_BinaryHashFileHandle);
    }
    m_LookupTable.clear();
    m_Path.clear();
}

const bool
HashList::Initialize(
    const std::filesystem::path Path,
    const size_t DigestLength,
    const bool ShouldSort
)
{
    m_Path = Path;

    auto mapping = cracktools::MmapFileSpan<const uint8_t>(
        m_Path,
        PROT_READ,
        MAP_SHARED
    );

    if (!mapping.has_value())
    {
        std::cerr << "Error: unable to map file" << std::endl;
        return false;
    }

    m_BinaryHashFileHandle = std::get<FILE*>(mapping.value());
    auto data = std::get<std::span<const uint8_t>>(mapping.value());

    return Initialize(
        data,
        DigestLength,
        ShouldSort
    );
}

const bool
HashList::Initialize(
    std::span<const uint8_t> Data,
    const size_t DigestLength,
    const size_t DigestOffset,
    const size_t RowWidth,
    const bool Sort
)
{
    m_Data = Data;
    m_DigestLength = DigestLength;
    m_RowWidth = RowWidth;
    m_DigestOffset = DigestOffset;

    if (Data.size() % m_RowWidth != 0)
    {
        std::cerr << "Error: data size is not a multiple of row width" << std::endl;
        return false;
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
    constexpr size_t INVALID_OFFSET = std::numeric_limits<size_t>::max();

    std::cerr << "HashList::Initialize: " << GetCount() << " rows." << std::endl;
    std::cerr << "Row Width:" << m_RowWidth << std::endl;
    std::cerr << "Digest Length: " << m_DigestLength << std::endl;
    std::cerr << "Digest Offset: " << m_DigestOffset << std::endl;
    std::cerr << "Indexing hash table." << std::flush;

    m_LookupTable.resize(1 << m_BitmaskSize);
    std::vector<size_t> offsets(m_LookupTable.size(), INVALID_OFFSET);

    const size_t readahead = std::max<size_t>(1, GetCount() / std::pow(2, m_BitmaskSize));

    // First pass
    std::cerr << "\rIndexing hash table. Pass 1" << std::flush;
    for (size_t i = 0; i < GetCount(); i += readahead)
    {
        auto hash = GetHash(i);
        const uint32_t index = Bitmask(hash, m_BitmaskSize);
        if (offsets[index] == INVALID_OFFSET)
        {
            offsets[index] = i;
        }
    }

    // Handle the last entry
    auto last = GetHash(GetCount() - 1);
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
        for (size_t i = 0; i < offsets.size(); i++)
        {
            if (offsets[i] == INVALID_OFFSET || offsets[i] == 0)
            {
                continue;
            }

            // Walk backwards until we find the previous
            while (offsets[i] > 0)
            {
                const size_t previousOffset = offsets[i] - 1;
                auto previous = GetHash(previousOffset);
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
                m_LookupTable[i] = m_Data.subspan(offsets[i] * m_RowWidth, Count * m_RowWidth);
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
            m_LookupTable[i] = m_Data.subspan(offsets[i] * m_RowWidth, Count * m_RowWidth);
            break;
        }
    }

    // Check that the total lengths add up
    size_t total = 0;
    for (size_t i = 0; i < m_LookupTable.size(); i++)
    {
        if (m_LookupTable[i].empty())
        {
            continue;
        }
        total += m_LookupTable[i].size();
    }
    if (total != m_Data.size())
    {
        std::cerr << "Error: bitmask lengths do not march hash list length" << std::endl;
        return false;
    }

    std::cerr << std::endl;

    return true;
}

const bool
HashList::LookupLinear(
    std::span<const uint8_t> Hash
) const
{
    return FindLinearInternal(
        m_Data, Hash
    ).has_value();
}
#include "Util.hpp"
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

    return FindBinaryInternal(
        subspan,
        Hash
    ).has_value();
}

const bool
HashList::LookupBinary(
    std::span<const uint8_t> Hash
) const
{
    return FindBinaryInternal(
        m_Data,
        Hash
    ).has_value();
}

std::optional<size_t>
HashList::FindFast(
    std::span<const uint8_t> Hash
) const
{
    const uint32_t index = Bitmask(Hash, m_BitmaskSize);
    auto subspan = m_LookupTable[index];

    if (subspan.empty())
    {
        return std::nullopt;
    }

    return FindBinaryInternal(
        subspan,
        Hash
    );
}

std::optional<size_t>
HashList::FindLinear(
    std::span<const uint8_t> Hash
) const
{
    return FindLinearInternal(
        m_Data,
        Hash
    );
}

typedef struct _CompareArgs
{
    size_t DigestLength;
    size_t RowWidth;
    size_t DigestOffset;
} CompareArgs;

#pragma clang unsafe_buffer_usage begin

#ifdef __APPLE__
static int
RowCompare(
    void* Args,
    const void* Value1,
    const void* Value2
)
#else
static int
RowCompare(
    const void* Value1,
    const void* Value2,
    const void* Args
)
#endif
{
    const CompareArgs* args = (const CompareArgs*)Args;
    const uint8_t* const v1 = (uint8_t*)Value1 + args->DigestOffset;
    const uint8_t* const v2 = (uint8_t*)Value2 + args->DigestOffset;
    return memcmp(v1, v2, (size_t)args->DigestLength);
}

#pragma clang unsafe_buffer_usage end

void
HashList::Sort(
    void
)
{
    CompareArgs args;
    args.DigestLength = m_DigestLength;
    args.RowWidth = m_RowWidth;
    args.DigestOffset = m_DigestOffset;
#ifdef __APPLE__
    qsort_r((void*)m_Data.data(), GetCount(), m_RowWidth, (void*)&args, RowCompare);
#else
    qsort_r((void*)m_Data.data(), GetCount(), m_RowWidth, (__compar_d_fn_t)RowCompare, (void*)&args);
#endif
}