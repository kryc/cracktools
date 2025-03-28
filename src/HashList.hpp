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
#include <vector>
#include <stdio.h>

typedef struct __attribute__((packed)) _LookupTable
{
    uint32_t Offset;
    uint32_t Count;
} LookupTable;

#define INVALID_OFFSET ((uint32_t)-1)

class HashList
{
public:
    HashList(void) = default;
    const bool Initialize(const std::filesystem::path Path, const size_t DigestLength, const bool Sort = false);
    const bool Initialize(uint8_t* Base, const size_t Size, const size_t DigestLength, const bool Sort = true);
    const bool Lookup(const uint8_t* Hash) const;
    const bool LookupLinear(const uint8_t* Hash) const;
    const bool LookupFast(const uint8_t* Hash) const;
    const bool LookupBinary(const uint8_t* Hash) const;
    void Sort(void);
    const size_t GetCount(void) const { return m_Count; };
    void SetBitmaskSize(const size_t BitmaskSize) { m_BitmaskSize = BitmaskSize; };
    const size_t GetBitmaskSize(void) const { return m_BitmaskSize; };
    // Static
    static const bool Lookup(const uint8_t* const Base, const size_t Size, const uint8_t* const Hash, const size_t HashSize);
    static const bool LookupLinear(const uint8_t* const Base, const size_t Size, const uint8_t* const Hash, const size_t HashSize);
private:
    const bool InitializeInternal(void);
    std::filesystem::path m_Path;
    size_t m_DigestLength;
    FILE* m_BinaryHashFileHandle;
    uint8_t* m_Base;
    size_t m_Size;
    size_t m_Count;
    size_t m_BitmaskSize = 16;
    std::vector<LookupTable> m_LookupTable;
};

#endif //HashList_hpp