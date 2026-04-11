//
//  Reduce.hpp
//  SimdCrack
//
//  Created by Kryc on 15/02/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef Reduce_hpp
#define Reduce_hpp

#include <assert.h>
#include <bit>
#include <cinttypes>
#include <cstddef>
#include <iostream>
#include <math.h>
#include <span>
#include <string>
#include <string_view>

#include "Check.hpp"
#include "SmallString.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"
#include "WordGenerator.hpp"

typedef __uint128_t index_t;

// For a given charset size, calculate the number of bits
// required to represent the maximum value
static inline const size_t
CalculateBitsRequired(
    const size_t Value,
    uint8_t* MaskOut
)
{
    if (Value == 0)
    {
        *MaskOut = 0;
        return 0;
    }

    // Calculate the number of bits required
    size_t bitsRequired = std::bit_width(Value);

    // Create the mask
    uint64_t mask = (1ULL << bitsRequired) - 1;

    DCHECK(bitsRequired <= 8);
    *MaskOut = static_cast<uint8_t>(mask);
    return bitsRequired;
}

static inline void
CalculateBytesRequired(
    const index_t Value,
    size_t* BitsRequired,
    size_t* BytesRequired,
    index_t* Mask
)
{
    const size_t bitsRequired = Util::BitWidth128(Value);
    const size_t bytesRequired = (bitsRequired + 7) / 8;
    const index_t mask = bitsRequired >= 128 ? ~index_t(0) : (index_t(1) << bitsRequired) - 1;
    
    if (BitsRequired != nullptr)
    {
        *BitsRequired = bitsRequired;
    }
    if (BytesRequired != nullptr)
    {
        *BytesRequired = bytesRequired;
    }
    if (Mask != nullptr)
    {
        *Mask = mask;
    }
}

static inline index_t
LoadBytesToIndex(
    std::span<const uint8_t> Buffer,
    const size_t Offset,
    const size_t Length
)
{
    assert(Length <= sizeof(index_t));
    assert(Offset + Length <= Buffer.size());
    // If there are enough bytes after the offset we can optimize the load
    index_t reduction = 0;
    if (Length <= sizeof(uint64_t) && Buffer.size() >= Offset + sizeof(uint64_t))
    {
        reduction = cracktools::LoadUint64BigEndian(
            Buffer.subspan(Offset, sizeof(uint64_t))
        );
        // Shift off the unused bytes
        reduction >>= (sizeof(uint64_t) - Length) * 8;
    }
    else
    {
        for (size_t i = 0; i < Length; i++)
        {
            reduction <<= 8;
            reduction |= Buffer[Offset + i];
        }
    }
    return reduction;
}

class Reducer
{
public:
    Reducer(
        const size_t Min,
        const size_t Max,
        const std::string_view Charset
    ) : m_Min(Min),
        m_Max(Max),
        m_Charset(Charset),
        m_MinIndex(WordGenerator::WordLengthIndex128(Min, Charset)),
        m_MaxIndex(WordGenerator::WordLengthIndex128(Max + 1, Charset))
        { };
    virtual size_t Reduce(
        std::span<char> Destination,
        std::span<const uint8_t> Hash,
        const size_t Iteration
    ) const = 0;
    virtual ~Reducer() {};
    const size_t GetMin(void) const { return m_Min; }
    const size_t GetMax(void) const { return m_Max; }
    const std::string_view GetCharset(void) const  { return m_Charset; }
    const index_t GetMinIndex(void) const { return m_MinIndex; }
    const index_t GetMaxIndex(void) const { return m_MaxIndex; }
    const index_t GetKeyspace(void) const { return m_MaxIndex - m_MinIndex; }
protected:
    // A basic entropy extension function based on SHA256 extension
    // It replaces the data in a destination buffer. there are two entropy
    // extenstion algirthms. (where n is the length of the buffer in words)
    // 1. EXTEND_SIMPLE:
    //    out[i] = std::rotl(out[i - n] ^ out[i - 1]) + out[i - 7]
    // 2. EXTEND_SHA256:
    //    s0 = (out[i - n] >> 7) ^ (out[i - n] >> 18) ^ (out[i - n] >> 3)
    //    s1 = (out[i - 2] >> 17) ^ (out[i - 2] >> 19) ^ (out[i - 2] >> 10)
    //    out[i] = s0 + s1 + out[i - 3]
    inline static void ExtendEntropy(
        std::span<uint32_t> Buffer
    )
    {
        for (size_t i = 0; i < Buffer.size(); i++)
        {
            const uint32_t d1 = Buffer[i];
            const uint32_t d2 = Buffer[(Buffer.size() - 2 + i) % Buffer.size()];
            const uint32_t d3 = Buffer[(Buffer.size() - 3 + i) % Buffer.size()];
#ifndef EXTEND_SIMPLE
            const uint32_t s0 = std::rotr(d1, 7) ^ std::rotr(d1, 18) ^ (d1 >> 3);
            const uint32_t s1 = std::rotr(d2, 17) ^ std::rotr(d2, 19) ^ (d2 >> 10);
            Buffer[i] = s0 + s1 + d3;
#else
            Buffer[i] = std::rotl(d1 ^ d2, 1) + d3;
#endif
        }
    }

    inline static void ExtendEntropy(
        std::span<uint8_t> Buffer
    )
    {
        ExtendEntropy(cracktools::SpanCast<uint32_t>(Buffer));
    }

    inline const size_t
    GetCharsUnbiased(
        std::span<char> Destination,
        std::span<uint8_t> Buffer,
        const size_t Offset,
        const size_t Length
    ) const
    {
        // Now read bytes from the remaining input buffer
        size_t bytesWritten = 0;
        const size_t charsetSize = m_Charset.size();
        uint8_t mask;
        const size_t bitsRequired = CalculateBitsRequired(charsetSize, &mask);
        size_t offset = Offset;
        while (bytesWritten < Length)
        {
            if (offset >= Buffer.size())
            {
                ExtendEntropy(Buffer);
                offset = 0;
            }

            uint8_t nextByte = Buffer[offset++];
            // Repeatedly shift the next byte until it is in range
            for (size_t i = bitsRequired; i <= 8; i++)
            {
                uint8_t value = nextByte & mask;
                if (value < m_Charset.size())
                {
                    Destination[bytesWritten++] = m_Charset[value % charsetSize];
                    break;
                }
                nextByte >>= 1;
            }
        }
        return bytesWritten;
    }

    const size_t m_Min;
    const size_t m_Max;
    const std::string_view m_Charset;
    const index_t m_MinIndex;
    const index_t m_MaxIndex;
};

class BasicModuloReducer : public Reducer
{
public:
    BasicModuloReducer(
        const size_t Min,
        const size_t Max,
        const std::string_view Charset
    ) : Reducer(Min, Max, Charset) {}

    size_t Reduce(
        std::span<char> Destination,
        std::span<const uint8_t> Hash,
        const size_t Iteration
    ) const override
    {
        
        // Parse the hash as a single bigint
        index_t reduction = LoadBytesToIndex(Hash, 0, Hash.size());
        return PerformReduction(
            Destination,
            reduction,
            Iteration
        );
    }
    
protected:

    inline size_t PerformReduction(
        std::span<char> Destination,
        index_t& Value,
        const size_t Iteration
    ) const
    {
        // XOR the current rainbow collumn number
        Value ^= Iteration;
        // Constrain it within the index range
        Value %= GetKeyspace();
        // Add the minimum index to ensure it is >min and <max
        Value += m_MinIndex;
        // Generate and return the word for the index
        return WordGenerator::GenerateWord(
            Destination,
            Value,
            m_Charset
        );
    }
};

class ModuloReducer final : public BasicModuloReducer
{
public:
    ModuloReducer(
        const size_t Min,
        const size_t Max,
        const std::string_view Charset
    ) : BasicModuloReducer(Min, Max, Charset)
    {
        // Figure out the smallest number of bits of
        // input hash data required to generate a
        // word in the password space
        CalculateBytesRequired(
            GetKeyspace(),
            &m_BitsRequired,
            &m_BytesRequired,
            &m_Mask
        );
    };

    size_t Reduce(
        std::span<char> Destination,
        std::span<const uint8_t> Hash,
        const size_t Iteration
    ) const override
    {
        std::vector<uint8_t> hashBuffer(Hash.size());
        std::copy(Hash.begin(), Hash.end(), hashBuffer.begin());
        // Repeatedly try to load the hash integer until it is in range
        // we do this to avoid a modulo bias favouring reduction at the bottom
        // end of the password space
        index_t reduction = GetKeyspace() + 1;
        size_t offset = 0;
        while (reduction > GetKeyspace())
        {
            if (offset + m_BytesRequired == Hash.size())
            {
                ExtendEntropy(hashBuffer);
                offset = 0;
            }
            // Parse the hash as a single integer
            reduction = LoadBytesToIndex(hashBuffer, offset, m_BytesRequired);
            // Mask the value to ensure it is in range
            reduction &= m_Mask;
            // Move the offset along
            offset++;
        }
        // Now return the password as normal
        return PerformReduction(
            Destination,
            reduction,
            Iteration
        );
    }
protected:
    size_t m_BitsRequired;
    size_t m_BytesRequired;
    index_t m_Mask;
};

class HybridReducerOld final : public Reducer
{
    static constexpr size_t kHybridReducerMaxHashSize = 512u / 8u;
    static constexpr uint64_t kHybridReducerRoundConstant = 0x5a827999;
public:
    HybridReducerOld(
        const size_t Min,
        const size_t Max,
        const std::string_view Charset
    ) : Reducer(Min, Max, Charset)
    {
        index_t total = 0;
        for (size_t i = Min; i <= Max; i++)
        {
            const __uint128_t lower = WordGenerator::WordLengthIndex128(i, Charset);
            const __uint128_t upper = WordGenerator::WordLengthIndex128(i + 1, Charset);
            const index_t keyspace = upper - lower;
            total += keyspace;
            m_Limits[i] = total;
        }

        // We need to calculate the number of bytes required
        // to represent the largest range of the keyspace
        CalculateBytesRequired(
            total,
            &m_BitsRequired,
            &m_BytesRequired,
            &m_Mask
        );

        assert(m_BytesRequired <= sizeof(index_t));
    }

    size_t Reduce(
        std::span<char> Destination,
        std::span<const uint8_t> Hash,
        const size_t Iteration
    ) const override
    {
        std::array<uint8_t, kHybridReducerMaxHashSize> tempBuffer;
        // Create a span of the temporary hash buffer that just covers
        // the part that we will use in this operation
        auto buffer = cracktools::UnsafeSpan<uint8_t>(tempBuffer.data(), Hash.size());
        // Two utility uint32 spans to access the hash and the temp
        // buffer as DWORDs
        const std::span<uint32_t> buffer32 = cracktools::SpanCast<uint32_t>(buffer);
        const std::span<const uint32_t> hash32 = cracktools::SpanCast<const uint32_t>(Hash);
        size_t length, offset = 0;
        // Copy and mix in the iteration
        for (size_t i = 0; i < hash32.size(); i++)
        {
            buffer32[i] = hash32[i] ^ std::rotl(kHybridReducerRoundConstant * Iteration, i);
        }
        
        // If we are using variable lengths we use a bigint
        // within the keyspace range to get the length
        if (m_Min != m_Max)
        {
            // Repeatedly try to load the hash integer until it is in range
            // we do this to avoid a modulo bias favouring reduction at the bottom
            // end of the password space
            index_t reduction = m_Limits[m_Max] + 1;
            while (reduction >= m_Limits[m_Max])
            {
                if (offset + m_BytesRequired == Hash.size())
                {
                    ExtendEntropy(buffer32);
                    offset = 0;
                }
                // Parse the hash as a single integer
                reduction = LoadBytesToIndex(buffer, offset, m_BytesRequired);
                // If the value is too big we can reuse this entropy and
                // reverse the byte order to see if the result is smaller.
                if ((reduction & m_Mask) >= m_Limits[m_Max])
                {
                    reduction = Util::Byteswap128(reduction) >> (sizeof(index_t) * 8 - m_BytesRequired * 8);
                }
                reduction &= m_Mask;
                // Move the offset along
                offset++;
            }

            length = 0;
            for (size_t i = m_Min; i <= m_Max && length == 0; i++)
            {
                if (reduction < m_Limits[i])
                {
                    length = i;
                }
            }
        }
        else
        {
            length = m_Max;
        }

        // Now read bytes from the remaining input buffer
        // Update the used bytes to account for entropy used in length calculation
        offset += m_BytesRequired - 1;
        // Or... just reset it to zero to minimize entropy extension.. Is reusing
        // the entropy used in the length calculation a bad thing...?
        // offset = 0;

        return GetCharsUnbiased(
            Destination,
            buffer,
            offset,
            length
        );
    }
private:
    size_t m_BitsRequired;
    size_t m_BytesRequired;
    index_t m_Mask;
    std::array<index_t, kSmallStringMaxLength> m_Limits{};
};

class HybridReducer final : public Reducer
{
    static constexpr size_t kHybridReducerMaxHashSize = 512u / 8u;
    static constexpr uint64_t kHybridReducerRoundConstant = 0x6a09e667f3bcc908;
public:
    HybridReducer(
        const size_t Min,
        const size_t Max,
        const std::string_view Charset
    ) : Reducer(Min, Max, Charset)
    {
        const __uint128_t lower = WordGenerator::WordLengthIndex128(Min, Charset);
        const __uint128_t upper = WordGenerator::WordLengthIndex128(Max + 1, Charset);
        m_KeyspaceSize = upper - lower;

        // We need to calculate the number of bytes required
        // to represent the largest range of the keyspace
        CalculateBytesRequired(
            m_KeyspaceSize,
            &m_BitsRequired,
            &m_BytesRequired,
            &m_Mask
        );

        assert(m_BytesRequired <= sizeof(index_t));
    }

    size_t Reduce(
        std::span<char> Destination,
        std::span<const uint8_t> Hash,
        const size_t Iteration
    ) const override
    {
        uint64_t iterationModifier = kHybridReducerRoundConstant * Iteration;
        size_t roundConstantModifier = 0;
        
        // Repeatedly try to load the hash integer until it is in range
        // we do this to avoid a modulo bias favouring reduction at the bottom
        // end of the password space
        size_t offset = 0;
        index_t reduction = m_KeyspaceSize + 1;
        while (reduction >= m_KeyspaceSize)
        {
            if (offset + m_BytesRequired == Hash.size())
            {
                CHECKA(roundConstantModifier < 64, "Exhausted reduction rounds. Unable to reduce hash.");
                roundConstantModifier++;
                iterationModifier = std::rotl(iterationModifier, 1);
                offset = 0;
            }
            // Parse the hash as a single integer
            reduction = LoadBytesToIndex(Hash, offset++, m_BytesRequired);
            reduction ^= iterationModifier;
            // Apply the mask to ensure it is in range
            reduction &= m_Mask;
        }

        // Now just use the reduction to generate the word
        size_t length = WordGenerator::GenerateWord(
            Destination,
            reduction,
            m_Charset
        );

        return length;
    }
private:
    index_t m_KeyspaceSize;
    size_t m_BitsRequired;
    size_t m_BytesRequired;
    index_t m_Mask;
};

class BytewiseReducer final : public Reducer
{
public:
    BytewiseReducer(
        const size_t Min,
        const size_t Max,
        const std::string_view Charset
    ) : Reducer(Min, Max, Charset)
    {
        assert(Min == Max);
    }
    
    size_t Reduce(
        std::span<char> Destination,
        std::span<const uint8_t> Hash,
        const size_t Iteration
    ) const override
    {
        std::vector<uint8_t> buffer(Hash.size());
        // Copy hash to buffer so we can update it
        std::copy(Hash.begin(), Hash.end(), buffer.begin());

        return GetCharsUnbiased(
            Destination,
            buffer,
            0,
            m_Max
        );
    };
};

#endif /* Reduce_hpp */
