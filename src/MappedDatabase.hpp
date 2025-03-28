//
//  Database.hpp
//  CrackDB++
//
//  Created by Kryc on 27/02/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

typedef struct _FileMapping
{
    void*  Base;
    size_t Size;
} FileMapping;

class MappedDatabase
{
public:
    MappedDatabase(HashAlgorithm Algorithm, const std::filesystem::path Path) {
        m_Algorithm = Algorithm;
        m_Path = Path;
        m_Handle = fopen(Path.c_str(), "r");
        if (m_Handle == nullptr)
        {
            std::cerr << "Error opening file for sorting" << std::endl;
            return;
        }
        m_Mapping.Size = std::filesystem::file_size(Path);
        m_Mapping.Base = (uint8_t*)mmap(nullptr, m_Mapping.Size, PROT_READ, MAP_PRIVATE, fileno(m_Handle), 0);
        if (m_Mapping.Base == MAP_FAILED)
        {
            std::cerr << "Error mapping file " << Path.filename() << std::endl;
            fclose(m_Handle);
            m_Handle = nullptr;
            return;
        }
        auto ret = madvise(m_Mapping.Base, m_Mapping.Size, MADV_RANDOM | MADV_WILLNEED);
        if (ret != 0)
        {
            std::cerr << "Madvise not happy" << std::endl;
        }
    };
    MappedDatabase& operator=(MappedDatabase&& Other){
        m_Path = Other.m_Path;
        m_Algorithm = Other.m_Algorithm;
        m_Handle = Other.m_Handle;
        m_Mapping = Other.m_Mapping;
        Other.m_Handle = nullptr;
        Other.m_Mapping.Base = nullptr;
        Other.m_Mapping.Size = 0;
        return *this;
    };
    MappedDatabase(MappedDatabase&& Other){
        *this = std::move(Other);
    }
    
    MappedDatabase(const MappedDatabase&) = delete;
    MappedDatabase& operator=(const MappedDatabase&) = delete;
    ~MappedDatabase(void) {
        if (m_Mapping.Base != nullptr)
        {
            munmap(m_Mapping.Base, m_Mapping.Size);
            m_Mapping.Base = nullptr;
            m_Mapping.Size = 0;
        }
        if (m_Handle != nullptr)
        {
            fclose(m_Handle);
            m_Handle = nullptr;
        }
    };
    const FileMapping& GetMapping(void) const { return m_Mapping; };
    const HashAlgorithm GetAlgorithm(void) const { return m_Algorithm; };
    const std::filesystem::path GetPath(void) const { return m_Path; };
private:
    std::filesystem::path m_Path;
    HashAlgorithm m_Algorithm;
    FILE*  m_Handle;
    FileMapping m_Mapping;
};