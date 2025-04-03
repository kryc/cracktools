//
//  Database.hpp
//  CrackDB++
//
//  Created by Kryc on 27/02/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#include "simdhash.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>

#include "UnsafeBuffer.hpp"

/*
 * A single record definition. This can be modified to
 * optimise the storage requirements.
 * The bit field of index and length gives you the original
 * word's length and the index within that file where it can
 * be found
 * The hash is the first N bytes of the hash
 */
#define INDEX_BITS 26
#define LENGTH_BITS 6
#define HASH_BYTES 6
typedef struct __attribute__((__packed__)) _Record
{
    uint32_t Index : INDEX_BITS;
    uint32_t Length : LENGTH_BITS;
    uint8_t  Hash[HASH_BYTES];
} DatabaseRecord;

typedef std::span<const DatabaseRecord> DatabaseFileMapping;

/*
 * The index and length bits will likely wrap
 * For example, if LENGTH_BITS is only 6, then
 * the maximum length we can store is 2**6 - 1 = 63
 * 64 will wrap back to 0.
 * Calculate that wrap value given the following
 */
#define INDEX_WRAP(x) = ((x) + (1 << INDEX_BITS))
#define LENGTH_WRAP(x) = ((x) + (1 << LENGTH_BITS))

static_assert(sizeof(DatabaseRecord) == sizeof(uint32_t) + HASH_BYTES);

class MappedDatabase
{
public:
    MappedDatabase(HashAlgorithm Algorithm, const std::filesystem::path Path) {
        m_Algorithm = Algorithm;
        m_Path = Path;

        auto mapping = cracktools::MmapFileSpan<DatabaseRecord>(Path, PROT_READ, MAP_PRIVATE, /*Madvise*/ true);

        if (!mapping.has_value())
        {
            std::cerr << "Error: unable to map database file " << Path.filename() << std::endl;
            return;
        }

        auto [mapped, fp] = mapping.value();
        m_Mapping = mapped;
        m_Handle = fp;
    };
    MappedDatabase& operator=(MappedDatabase&& Other){
        m_Path = Other.m_Path;
        m_Algorithm = Other.m_Algorithm;
        m_Handle = Other.m_Handle;
        m_Mapping = Other.m_Mapping;
        Other.m_Handle = nullptr;
        Other.m_Mapping = std::span<DatabaseRecord>();
        return *this;
    };
    MappedDatabase(MappedDatabase&& Other){
        *this = std::move(Other);
    }
    
    MappedDatabase(const MappedDatabase&) = delete;
    MappedDatabase& operator=(const MappedDatabase&) = delete;
    ~MappedDatabase(void) {
        cracktools::UnmapFileSpan(m_Mapping, m_Handle);
    };
    const std::span<DatabaseRecord> GetMapping(void) const { return m_Mapping; };
    const HashAlgorithm GetAlgorithm(void) const { return m_Algorithm; };
    const std::filesystem::path GetPath(void) const { return m_Path; };
private:
    std::filesystem::path m_Path;
    HashAlgorithm m_Algorithm;
    FILE*  m_Handle;
    std::span<DatabaseRecord> m_Mapping;
};