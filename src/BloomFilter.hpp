//
//  BloomFilter.hpp
//  SimdRainbowCrack
//
//  Created on 11/04/2026.
//

#ifndef BloomFilter_hpp
#define BloomFilter_hpp

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class BloomFilter
{
public:
    // Construct a Bloom filter sized for expectedCount elements
    // at roughly 1% false positive rate (~10 bits per element, 7 hashes).
    explicit BloomFilter(size_t expectedCount);

    // Insert a value. Returns true if the value was probably not
    // already present (i.e. all probed bits were not set).
    // Returns false if the value was probably already present.
    bool Insert(uint32_t value);
    bool Insert(uint64_t value);
    bool Insert(__uint128_t value);

    // Test whether a value is probably present.
    bool MayContain(uint32_t value) const;
    bool MayContain(uint64_t value) const;
    bool MayContain(__uint128_t value) const;

    // Number of insertions performed
    size_t Count(void) const { return m_Count; }

    // Memory usage in bytes
    size_t MemoryUsage(void) const { return m_Bits.size() / 8; }

    // Reset the filter
    void Clear(void);

private:
    static constexpr size_t kHashCount = 7;
    static constexpr double kBitsPerElement = 10.0;

    using HashArray = std::array<uint64_t, kHashCount>;

    HashArray GetHashes(uint64_t value) const;
    HashArray GetHashes(__uint128_t value) const;

    std::vector<bool> m_Bits;
    size_t m_NumBits;
    size_t m_Count = 0;
};

#endif /* BloomFilter_hpp */
