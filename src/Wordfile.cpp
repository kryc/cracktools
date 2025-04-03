//
//  Wordfile.cpp
//  CrackDB++
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <sys/mman.h>

#include "MappedDatabase.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"
#include "Wordfile.hpp"

const std::string
SizeToFilename(
    const size_t Size
)
{
    uint32_t size32 = static_cast<uint32_t>(Size);
    uint8_t bytes[4];
    bytes[0] = (size32 >> 24) & 0xff;
    bytes[1] = (size32 >> 16) & 0xff;
    bytes[2] = (size32 >> 8) & 0xff;
    bytes[3] = size32 & 0xff;
    auto hex = Util::ToHex(bytes, sizeof(bytes));
    return hex + ".txt";
}

const size_t
FilenameToSize(
    const std::filesystem::path Filepath
)
{
    std::string basename = Filepath.filename();

    if (basename.size() < 5 || Filepath.extension() != ".txt")
    {
        std::cerr << "Invalid wordlist file: " << basename << std::endl; 
        return 0;
    }

    // Strip the extension
    basename = basename.substr(0, basename.size() - 4);

    auto bytes = Util::ParseHex(basename);
    size_t size = 0;
    size |= static_cast<size_t>(bytes[0]) << 24;
    size |= static_cast<size_t>(bytes[1]) << 16;
    size |= static_cast<size_t>(bytes[2]) << 8;
    size |= static_cast<size_t>(bytes[3]);

    return size;
}

const size_t
Wordfile::CalculateCount(
    void
) const
{
    const size_t filesize = Filesize();
    if (filesize % m_Size != 0)
    {
        std::cerr << "Corrupted file detected! " << m_Path.filename() << std::endl;
        return 0;
    }
    return filesize / m_Size;
}

Wordfile::Wordfile(
    const std::filesystem::path& Wordfile,
    const bool Write
)
{
    const size_t size = FilenameToSize(Wordfile);
    const std::filesystem::path dbPath = Wordfile.parent_path().parent_path();
    std::cerr << dbPath << ' ' << size << std::endl;
    Initialize(dbPath, size, Write);
}

Wordfile::~Wordfile(
    void
)
{
    if (m_Mapped != nullptr)
    {
        munmap(m_Mapped, Filesize());
        m_Mapped = nullptr;
    }

    if (m_WriteHandle != nullptr)
    {
        fclose(m_WriteHandle);
        m_WriteHandle = nullptr;
    }

    if (m_ReadHandle != nullptr)
    {
        fclose(m_ReadHandle);
        m_ReadHandle = nullptr;
    }
}

void
Wordfile::Initialize(
    const std::filesystem::path& DatabasePath,
    const size_t Size,
    const bool Write
)
{
    m_DatabasePath = DatabasePath;
    m_Size = Size;
    m_Path = DatabasePath / "words" / SizeToFilename(Size);
    m_Count = CalculateCount();
    m_Write = Write;
    OpenFile(Write);
}

bool
Wordfile::OpenFile(
    const bool Write
)
{
    if (Write)
    {
        m_WriteHandle = fopen(m_Path.c_str(), "a+");
        if (m_WriteHandle == nullptr)
        {
            std::cerr << "Unable to open word file " << m_Size << std::endl;
            perror(nullptr);
            return false;
        }
    }
    else
    {
        auto mapping = cracktools::MmapFileSpan<char>(m_Path, PROT_READ, MAP_PRIVATE);

        if (!mapping.has_value())
        {
            std::cerr << "Error: unable to map file" << std::endl;
            return false;
        }

        m_ReadHandle = std::get<FILE*>(mapping.value());
        m_Span = std::get<std::span<char>>(mapping.value());
    }

    return true;
}

std::span<char>
Wordfile::Get(
    const size_t Index
) const
{
    const size_t offset = Index * m_Size;
    return m_Span.subspan(offset, m_Size);
}

std::vector<std::span<char>>
Wordfile::GetAll(
    const size_t Index
) const
{
    // Loop over all possible indices given
    // the N bit index
    std::vector<std::span<char>> result;

    for (size_t i = Index; i <= GetCount(); i += (1 << INDEX_BITS))
    {
        result.push_back(Get(i));
    }

    return result;
}

const std::string
Wordfile::GetString(
    const size_t Index
) const
{
    const auto val = Get(Index);
    return std::string(val.begin(), val.end());
}

std::vector<std::string>
Wordfile::GetAllStrings(
    const size_t Index
) const
{
    // Loop over all possible indices given
    // the N bit index
    std::vector<std::string> result;

    for (size_t i = Index; i <= GetCount(); i += (1 << INDEX_BITS))
    {
        result.push_back(GetString(i));
    }

    return result;
}

const size_t
Wordfile::Add(
    const std::string& Word
)
{
    if (!m_Write)
    {
        std::cerr << "Wordlist opened for write" << std::endl;
        return -1;
    }

    if (Word.size() != m_Size)
    {
        std::cerr << "Trying to add an incorrect word size" << std::endl;
        return -1;
    }

    if (m_WriteHandle == nullptr)
    {
        std::cerr << "Fatal error, unable to open wordfile handle" << std::endl;
    }

    fwrite(&Word[0], sizeof(char), Word.size(), m_WriteHandle);

    return m_Count++;
}