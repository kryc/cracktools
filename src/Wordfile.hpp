//
//  Wordfile.hpp
//  CrackDB++
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef Wordfile_hpp
#define Wordfile_hpp

#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

class Wordfile
{
public:
    Wordfile(const std::filesystem::path& Wordfile, const bool Write);
    Wordfile(const std::filesystem::path& DatabasePath, const size_t Size, const bool Write) { Initialize(DatabasePath, Size, Write); };
    ~Wordfile(void);
    const bool IsOpen(void) const { return m_Write ? m_WriteHandle != nullptr : m_ReadHandle != nullptr; };
    const size_t Filesize(void) const { return std::filesystem::exists(m_Path) ? std::filesystem::file_size(m_Path) : 0; };
    const size_t GetCount(void) const { return m_Count; };
    std::span<char> Get(const size_t Index) const;
    std::vector<std::span<char>> GetAll(const size_t Index) const;
    const std::string GetString(const size_t Index) const;
    std::vector<std::string> GetAllStrings(const size_t Index) const;
    const size_t Add(const std::string& Word);
private:
    void Initialize(const std::filesystem::path& DatabasePath, const size_t Size, const bool Write);
    const size_t CalculateCount(void) const;
    bool OpenFile(const bool Write);
    std::filesystem::path m_Path;
    std::filesystem::path m_DatabasePath;
    size_t m_Size;
    FILE* m_ReadHandle = nullptr;
    void* m_Mapped = nullptr;
    FILE* m_WriteHandle = nullptr;
    std::span<char> m_Span;
    size_t m_Count;
    bool m_Write;
};

using WordfilePtr = std::shared_ptr<Wordfile>;

const std::string
SizeToFilename(
    const size_t Size
);

const size_t
FilenameToSize(
    const std::filesystem::path Filepath
);

#endif /* Wordfile_hpp */