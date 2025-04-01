//
//  HashList.hpp
//  HashList
//
//  Created by Kryc on 12/11/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef HashList_hpp
#define HashList_hpp

#include <filesystem>
#include <span>
#include <vector>
#include <stdio.h>

#include "UnsafeBuffer.hpp"

class HashList
{
public:
    HashList(void) = default;
    const bool Initialize(const std::filesystem::path Path, const size_t DigestLength, const bool ShouldSort = false);
    const bool Initialize(std::span<const uint8_t> Data, const size_t DigestLength, const bool ShouldSort = true);
    const bool Initialize(const uint8_t* Base, const size_t Size, const size_t DigestLength, const bool ShouldSort = true) {
        return Initialize(cracktools::UnsafeSpan<const uint8_t>(Base, Size), DigestLength, ShouldSort);
    }
    const bool Lookup(std::span<const uint8_t> Hash) const;
    const bool LookupLinear(std::span<const uint8_t> Hash) const;
    const bool LookupFast(std::span<const uint8_t> Hash) const;
    const bool LookupBinary(std::span<const uint8_t> Hash) const;
    const size_t GetCount(void) const { return m_Data.size() / m_DigestLength; };
    void SetBitmaskSize(const size_t BitmaskSize) { m_BitmaskSize = BitmaskSize; };
    const size_t GetBitmaskSize(void) const { return m_BitmaskSize; };
    // Static
    static const bool Lookup(std::span<const uint8_t> HashList, std::span<const uint8_t> Hash);
    static const bool LookupLinear(std::span<const uint8_t> HashList, std::span<const uint8_t> Hash);
private:
    void Sort(void);
    const bool InitializeInternal(void);
    std::filesystem::path m_Path;
    size_t m_DigestLength;
    FILE* m_BinaryHashFileHandle;
    std::span<const uint8_t> m_Data;
    size_t m_BitmaskSize = 16;
    std::vector<std::span<const uint8_t>> m_LookupTable;
};

#endif //HashList_hpp