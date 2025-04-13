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
#include <optional>
#include <span>
#include <vector>
#include <stdio.h>

#include "UnsafeBuffer.hpp"

class HashList
{
public:
    HashList(void) = default;
    ~HashList(void) { Clear(); };
    const bool Initialize(const std::filesystem::path Path, const size_t DigestLength, const bool ShouldSort = false);
    const bool Initialize(std::span<const uint8_t> Data, const size_t DigestLength, const size_t DigestOffset, const size_t RowWidth, const bool Sort);
    const bool Initialize(std::span<const uint8_t> Data, const size_t DigestLength, const bool ShouldSort = true) {
        return Initialize(Data, DigestLength, 0, DigestLength, ShouldSort);
    }
    const bool Initialize(const uint8_t* Base, const size_t Size, const size_t DigestLength, const bool ShouldSort = true) {
        return Initialize(cracktools::UnsafeSpan<const uint8_t>(Base, Size), DigestLength, ShouldSort);
    }
    const bool Initialized(void) const { return m_Data.size() > 0; }
    const bool LookupFast(std::span<const uint8_t> Hash) const;
    const bool LookupLinear(std::span<const uint8_t> Hash) const;
    const bool LookupBinary(std::span<const uint8_t> Hash) const;
    inline const bool Lookup(std::span<const uint8_t> Hash) const { return LookupFast(Hash); }
    std::optional<size_t> FindLinear(std::span<const uint8_t> Hash) const;
    std::optional<size_t> FindBinary(std::span<const uint8_t> Hash) const;
    std::optional<size_t> Find(std::span<const uint8_t> Hash) const { return FindBinary(Hash); }
    const size_t GetCount(void) const { return m_Data.size() / m_RowWidth; };
    const bool SetBitmaskSize(const size_t BitmaskSize);
    const size_t GetBitmaskSize(void) const { return m_BitmaskSize; };
    inline std::span<const uint8_t> GetRow(std::span<const uint8_t> Span, const size_t Index) const {
        return Span.subspan(Index * m_RowWidth, m_RowWidth);
    }
    inline std::span<const uint8_t> GetHash(std::span<const uint8_t> Span, const size_t Index) const {
        return Span.subspan(Index * m_RowWidth + m_DigestOffset, m_DigestLength);
    }
    inline std::span<const uint8_t> GetRow(const size_t Index) const {
        return GetRow(m_Data, Index);
    }
    inline std::span<const uint8_t> GetHash(const size_t Index) const {
        return GetHash(m_Data, Index);
    }
    void Clear(void);
    void Sort(void);
private:
    const bool InitializeInternal(void);
    std::optional<size_t> FindBinaryInternal(std::span<const uint8_t> HashList, std::span<const uint8_t> Hash) const;
    std::optional<size_t> FindLinearInternal(std::span<const uint8_t> HashList, std::span<const uint8_t> Hash) const;
    std::filesystem::path m_Path;
    size_t m_DigestLength;
    size_t m_RowWidth;
    size_t m_DigestOffset;
    FILE* m_BinaryHashFileHandle = nullptr;
    std::span<const uint8_t> m_Data;
    size_t m_BitmaskSize = 16;
    std::vector<std::span<const uint8_t>> m_LookupTable;
};

#endif //HashList_hpp