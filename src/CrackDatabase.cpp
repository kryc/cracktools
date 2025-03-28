//
//  Database.cpp
//  CrackDB++
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <openssl/sha.h>
#include <cstring>
#include <sys/mman.h>

#include "CrackDBCommon.hpp"
#include "CrackDatabase.hpp"
#include "Util.hpp"

static int
Compare(
    const void* Value1,
    const void* Value2
)
{
    uint8_t* v1 = ((uint8_t*)Value1) + sizeof(uint32_t);
    uint8_t* v2 = ((uint8_t*)Value2) + sizeof(uint32_t);
    return memcmp(v1, v2, HASH_BYTES);
}

CrackDatabase::CrackDatabase(
    const std::filesystem::path Path
)
{
    m_Path = Path;

    if (std::filesystem::exists(GetWordsPath()))
    {
        std::vector<size_t> sizes;
        for (auto const& dir_entry : std::filesystem::directory_iterator{GetWordsPath()})
        {
            auto path = dir_entry.path();
            auto basename = path.filename();

            // Skip any unexpected files
            if (path.extension() != ".txt")
            {
                continue;
            }

            const size_t length = FilenameToSize(basename);
            if (length == 0)
            {
                continue;
            }

            if (length > m_MaxWordSize)
            {
                m_MaxWordSize = length;
            }
            
            AddWordSize(length);
        }
    }
}

CrackDatabase::~CrackDatabase(
    void
)
{

}

inline const bool
CrackDatabase::HasWordSize(
    const size_t Size
) const
{
    return std::binary_search(m_Wordsizes.begin(), m_Wordsizes.end(), Size);
}

void
CrackDatabase::AddWordSize(
    const size_t Size
)
{
    if (!HasWordSize(Size))
    {
        m_Wordsizes.push_back(Size);
        std::sort(m_Wordsizes.begin(), m_Wordsizes.end());
    }
}

void
CrackDatabase::Sort(
    const HashAlgorithm Algorithm
) const
{
    std::cerr << "Sorting " << HashAlgorithmToString(Algorithm) <<  " hashes..." << std::flush;

    // Mmap the file
    auto path = m_HashDatabases.at(Algorithm);

    if (!std::filesystem::exists(path))
    {
        std::cerr << "Trying to sort non-existant file" << std::endl;
        return;
    }

    size_t len = std::filesystem::file_size(path);
    size_t count = len / sizeof(DatabaseRecord);
    FILE* fp = fopen(path.c_str(), "r+");
    if (fp == nullptr)
    {
        std::cerr << "Error opening file for sorting" << std::endl;
        return;
    }

    uint8_t* mapped = (uint8_t*)mmap(nullptr, len, PROT_READ|PROT_WRITE, MAP_SHARED, fileno(fp), 0);
    if (mapped == MAP_FAILED)
    {
        std::cerr << "Error mapping file " << path.filename() << std::endl;
        return;
    }

    auto ret = madvise(mapped, len, MADV_RANDOM | MADV_WILLNEED);
    if (ret != 0)
    {
        std::cerr << "Madvise not happy" << std::endl;
    }

    qsort(mapped, count, sizeof(DatabaseRecord), Compare);
    fclose(fp);

    std::cerr << " Completed" << std::endl;
}

const bool
CrackDatabase::Build(
    const std::vector<HashAlgorithm> Algorithms,
    const std::filesystem::path InputWords
)
{
    std::cerr << "Building database" << std::endl;

    std::filesystem::path wordDir = GetWordsPath();
    if (!std::filesystem::create_directories(wordDir))
    {
        std::cerr << "Error creating words directory" << std::endl;
        return false;
    }

    // Create a map of handles to output databases
    std::map<HashAlgorithm, std::ofstream> dbHandleMap;

    for (auto algorithm : Algorithms)
    {
        if (algorithm == HashAlgorithmUndefined)
        {
            std::cerr << "Invalid hash algorithm specified" << std::endl;
            return false;
        }

        std::filesystem::path dbPath = DatabaseFile(algorithm);

        if (std::filesystem::exists(dbPath))
        {
            std::cerr << "Database exists, appending not available" << std::endl;
            continue;
        }

        dbHandleMap[algorithm] = std::ofstream(dbPath, std::ios::out|std::ios::binary);
    }

    if (dbHandleMap.empty())
    {
        std::cerr << "No valid databases to build." << std::endl;
        return false;
    }

    std::ifstream istr;

    if (InputWords != "-")
    {
        istr.open(InputWords, std::ios::in);
    }

    std::istream& input = istr.is_open() ? istr : std::cin;

    uint8_t digest[MAX_DIGEST_LENGTH];
    DatabaseRecord record;
    for( std::string line; getline(input, line); )
    {
        // Strip cr and nl
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

        if (line.size() < m_Min)
        {
            continue;
        }

        if (line.size() > m_Max)
        {
            continue;
        }

        size_t wordIndex;
        if (m_CacheWordFiles)
        {
            if (m_Wordfiles.find(line.size()) == m_Wordfiles.end())
            {
                auto wf = std::make_unique<Wordfile>(m_Path, line.size(), true);
                m_Wordfiles[line.size()] = std::move(wf);
                // The maximum number of open file handles on ubuntu is 1024
                // If we reach 1010 open handles we need to close some
                // if (m_Wordfiles.size() > 100)
                // {
                //     // Find the maximum key in the m_Wordfiles map
                //     auto maxKey = std::max_element(m_Wordfiles.begin(), m_Wordfiles.end(),
                //         [](const auto& a, const auto& b) { return a.first < b.first; })->first;
                //     if (maxKey != line.size())
                //     {
                //         m_Wordfiles.erase(maxKey);
                //     }
                // }
            }
            wordIndex = m_Wordfiles.at(line.size())->Add(line);
        }
        else
        {
            Wordfile wf(m_Path, line.size(), true);
            wordIndex = wf.Add(line);
        }

        for (auto algorithm : Algorithms)
        {
            // Do the hash
            DoHash(algorithm, (uint8_t*)&line[0], line.size(), digest);

            // Copy the hash into the next record
            memcpy(record.Hash, digest, sizeof(record.Hash));

            // Set the index and the length
            record.Length = line.size();
            record.Index = wordIndex;

            // Write the record to the file
            dbHandleMap[algorithm].write((char*)&record, sizeof(record));
        }
    }

    istr.close();

    for (auto& [algorithm, handle] : dbHandleMap)
    {
        handle.close();

        // Add the unsorted database
        m_HashDatabases[algorithm] = DatabaseFile(algorithm);

         // Sort the hashes
        Sort(algorithm);
    }

    return true;
}

const
std::filesystem::path
CrackDatabase::DatabaseFile(
    const HashAlgorithm Algorithm
) const
{
    std::string basename = HashAlgorithmToString(Algorithm) + ".db";
    return m_Path / basename;
}

WordfilePtr
CrackDatabase::GetWordfile(
    const size_t Length,
    const bool Write
) const
{
    if (m_Wordfiles.find(Length) == m_Wordfiles.end())
    {
        return std::make_shared<Wordfile>(m_Path, Length, Write);
    }

    return m_Wordfiles.at(Length);
}

std::vector<WordfilePtr>
CrackDatabase::GetAllWordFiles(
    const size_t Length,
    const bool Write
) const
{
    std::vector<WordfilePtr> result;

    for (size_t l = Length; l <= m_MaxWordSize; l += (1 << LENGTH_BITS))
    {
        if (HasWordSize(l))
        {
            result.push_back(GetWordfile(l, Write));
        }
    }

    return result;
}

const bool
CrackDatabase::HasAlgorithm(
    const HashAlgorithm Algorithm
) const
{
    return std::filesystem::exists(DatabaseFile(Algorithm));
}

const std::optional<std::string>
CrackDatabase::CheckResult(
    const uint8_t* const Target,
    const size_t TargetSize,
    const DatabaseRecord* const Value,
    const DatabaseRecord* const Base,
    const DatabaseRecord* const Top,
    const HashAlgorithm Algorithm
) const
{
    // Check this entry then seek backwards while we have
    // a matching initial hash bytes
    uint8_t temp[TargetSize];
    for (const DatabaseRecord* seek = Value;
        seek >= Base && memcmp(seek->Hash, &Target[0], HASH_BYTES) == 0;
        --seek
    )
    {
        for (auto& wf : GetAllWordFiles(seek->Length, false))
        {
            auto words = wf->GetAll(seek->Index);
            for (auto& word : words)
            {
                // Check the hash
                DoHash(Algorithm, (uint8_t*)&word[0], word.size(), temp);
                if (memcmp(temp, &Target[0], TargetSize) == 0)
                {
                    return std::string(&word[0], word.size());
                }
            }
        }
    }

    // Seek forwards
    for (
        const DatabaseRecord* seek = Value + 1;
        seek <= Top && memcmp(seek->Hash, &Target[0], HASH_BYTES) == 0;
        ++seek
    )
    {
        for (auto& wf : GetAllWordFiles(seek->Length, false))
        {
            auto words = wf->GetAll(seek->Index);
            for (auto& word : words)
            {
                // Check the hash
                DoHash(Algorithm, (uint8_t*)&word[0], word.size(), temp);
                if (memcmp(temp, &Target[0], TargetSize) == 0)
                {
                    return std::string(&word[0], word.size());
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<const MappedDatabase> 
CrackDatabase::OpenDatabaseNoCache(
    const HashAlgorithm Algorithm
) const
{
    // Check if we have a database for that hash algorithm
    if (!HasAlgorithm(Algorithm))
    {
        std::cerr << "Detected algorithm not in database" << std::endl;
        return std::nullopt;
    }

    // Open a handle and mmap the file
    return MappedDatabase(Algorithm, DatabaseFile(Algorithm));
}


std::optional<std::shared_ptr<const MappedDatabase>>
CrackDatabase::GetDatabase(
    const HashAlgorithm Algorithm
) const
{
    // Check if it is in the cache
    if (m_DatabaseCache.find(Algorithm) != m_DatabaseCache.end())
    {
        return m_DatabaseCache.at(Algorithm);
    }

    // Check if we have a database for that hash algorithm
    if (!HasAlgorithm(Algorithm))
    {
        std::cerr << "Detected algorithm not in database" << std::endl;
        return std::nullopt;
    }

    return std::make_shared<const MappedDatabase>(Algorithm, DatabaseFile(Algorithm));
}

std::optional<const FileMapping>
CrackDatabase::GetCachedDatabaseMapping(
    const HashAlgorithm Algorithm
) const
{
    // Check if it is in the cache
    if (m_DatabaseCache.find(Algorithm) != m_DatabaseCache.end())
    {
        return m_DatabaseCache.at(Algorithm)->GetMapping();
    }

    return std::nullopt;
}

const std::optional<std::string>
CrackDatabase::Lookup(
    const HashAlgorithm Algorithm,
    const FileMapping& Mapping,
    const uint8_t* const Hash,
    const size_t Length
) const
{
    assert(Mapping.Size % sizeof(DatabaseRecord) == 0);
    const size_t count = Mapping.Size / sizeof(DatabaseRecord);

    // Perform the search
    const DatabaseRecord * const base = (const DatabaseRecord*) Mapping.Base;
    const DatabaseRecord * const top = base + count;

    const DatabaseRecord* low = (const DatabaseRecord*) base;
    const DatabaseRecord* high = (const DatabaseRecord*) top - 1;
    const DatabaseRecord* mid;

    while (low <= high)
    {
        mid = low + ((high - low) / 2);
        int cmp = memcmp(&mid->Hash[0], &Hash[0], HASH_BYTES);
        if (cmp == 0)
        {
            auto result = CheckResult(
                Hash,
                Length,
                mid,
                base,
                top,
                Algorithm
            );
            
            if (result.has_value())
            {
                return result;
            }
            else
            {
                break;
            }
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

const std::optional<std::string>
CrackDatabase::Lookup(
    const HashAlgorithm Algorithm,
    const uint8_t* const Hash,
    const size_t Length
) const
{
    // Try pulling the mapping directly from the cache
    auto mapping = GetCachedDatabaseMapping(Algorithm);
    if (mapping.has_value())
    {
        return Lookup(Algorithm, mapping.value(), Hash, Length);
    }
    
    // Fallback to open the database. This will check if we have this algorithm
    auto database = GetDatabase(Algorithm);
    if (!database.has_value())
    {
        return std::nullopt;
    }

    return Lookup(Algorithm, database.value()->GetMapping(), Hash, Length);
}

const std::optional<std::string>
CrackDatabase::Lookup(
    const HashAlgorithm Algorithm,
    const std::vector<uint8_t>& Hash
) const
{
    return Lookup(Algorithm, &Hash[0], Hash.size());
}

const std::optional<std::string>
CrackDatabase::Lookup(
    const std::vector<uint8_t>& Hash
) const
{
    const HashAlgorithm algorithm = DetectHashAlgorithm(Hash);

    if (algorithm == HashAlgorithmUndefined)
    {
        std::cerr << "Invalid hash: " << Util::ToHex(&Hash[0], Hash.size()) << std::endl;
        return std::nullopt;
    }

    return Lookup(algorithm, &Hash[0], Hash.size());
}

void
CrackDatabase::OutputResult(
    const std::string& Hash,
    const std::string& Value,
    std::ostream& Stream
) const
{
    auto formatted = m_Hex ? AsciiOrHex(Value) : Value;
    if (m_PasswordsOnly)
    {
        Stream << formatted << std::endl;
    }
    else
    {
        Stream << Util::ToLower(Hash) << m_Separator << formatted << std::endl;
    }
}

void
CrackDatabase::CrackFileInternal(
    void
)
{
    // Read a block of inputs
    std::vector<std::string> block;
    block.reserve(m_BlockSize);
    std::istream& input = m_InputFileStream.is_open() ? m_InputFileStream : std::cin;
    std::ostream& output = m_OutputFileStream.is_open() ? m_OutputFileStream : std::cout;

    {
        std::lock_guard<std::mutex> lock(m_InputMutex);

        if (input.eof())
        {
            dispatch::CurrentQueue()->Stop();
            return;
        }

        for (size_t i = 0; i < m_BlockSize && !input.eof(); i++)
        {
            std::string line;

            getline(input, line); 

            block.push_back(line);
        }
    }

    std::vector<std::string> uncrackable;
    std::vector<std::tuple<std::string, std::string>> cracked;
    cracked.reserve(block.size());

    for (auto& line : block)
    {
        // Strip cr and nl
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

        if (line.size() == 0 || !Util::IsHex(line))
        {
            uncrackable.push_back(line);
            continue;
        }

        auto result = Lookup(line);
        if (result.has_value())
        {
            cracked.push_back({line, result.value()});
        }
        else
        {
            uncrackable.push_back(line);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_OutputMutex);
        for (auto& [h,v] : cracked)
        {
            OutputResult(h, v, output);
        }

        for (auto& v : uncrackable)
        {
            m_UncrackableStream << v << std::endl;
        }
    }

    dispatch::PostTaskFast(
        std::bind(
            &CrackDatabase::CrackFileInternal,
            this
        )
    );
}

const bool
CrackDatabase::CrackFileLinear(
    void
)
{
    std::istream& input = m_InputFileStream.is_open() ? m_InputFileStream : std::cin;
    std::ostream& output = m_OutputFileStream.is_open() ? m_OutputFileStream : std::cout;

    std::string lastHash, lastResult;

    for (std::string line; getline(input, line); )
    {
        // Strip cr and nl
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

        if (line.size() == 0 || !Util::IsHex(line))
        {
            m_UncrackableStream << line << std::endl;
            continue;
        }

        if (line == lastHash)
        {
            OutputResult(line, lastResult, output);
            continue;
        }

        auto result = Lookup(line);
        if (result.has_value())
        {
            OutputResult(line, result.value(), output);
            lastHash = std::move(line);
            lastResult = std::move(result.value());
        }
        else
        {
            m_UncrackableStream << line << std::endl;
        }
    }

    return true;
}

const size_t
CrackDatabase::OpenWordfilesForLookup(
    void
)
{
    // Iterate over all wordfiles
    for (const size_t length : m_Wordsizes)
    {
        auto wf = std::make_unique<Wordfile>(m_Path, length, false);
        if (!wf->IsOpen())
        {
            std::cerr << "Error opening wordfile " << length << std::endl;
            continue;
        }
        m_Wordfiles[length] = std::move(wf);
    }
    return m_Wordfiles.size();
}

const size_t
CrackDatabase::OpenDatabaseFilesForLookup(
    void
)
{
    for (size_t i = 0; i < HashAlgoritmCount; i++)
    {
        auto algorithm = HashAlgorithms[i];
        if (HasAlgorithm(algorithm))
        {
            m_DatabaseCache[algorithm] = std::make_shared<const MappedDatabase>(algorithm, DatabaseFile(algorithm));
        }
    }
    return m_DatabaseCache.size();
}

const bool
CrackDatabase::CrackFile(
    const std::string& HashfileInput
)
{
    // Open the input file
    if (HashfileInput != "-")
    {
        if (!std::filesystem::exists(HashfileInput))
        {
            std::cerr << "Specified file does not exist" << std::endl;
            return false;
        }
        m_InputFileStream.open(HashfileInput, std::ios::in);
    }

    // Open the output file
    if (m_Output != "")
    {
        m_OutputFileStream.open(m_Output, std::ios::out);
    }

    // If we have an uncrackable, open it
    if (m_Uncrackable != "")
    {
        m_UncrackableStream.open(m_Uncrackable, std::ios::out);
    }

    // Open all wordfiles
    if (m_CacheWordFiles)
    {
        OpenDatabaseFilesForLookup();
        OpenWordfilesForLookup();
    }

    if (m_Threads == 1)
    {
        return CrackFileLinear();
    }
    else
    {
        // Create our worker pool
        m_DispatchPool = dispatch::CreateDispatchPool("worker", m_Threads);

        for (size_t i = 0; i < m_Threads; i++)
        {
            m_DispatchPool->PostTask(
                dispatch::bind(
                    &CrackDatabase::CrackFileInternal,
                    this
                )
            );
        }

        // Wait for tasks to complete
        m_DispatchPool->Wait();
        return true;
    }
}

const std::optional<std::string>
CrackDatabase::Test(
    const HashAlgorithm Algorithm,
    const std::string& Value
)
{
    std::vector<uint8_t> digest(GetDigestLength(Algorithm));
    DoHash(Algorithm, (uint8_t*)&Value[0], Value.size(), &digest[0]);
    return Lookup(Algorithm, &digest[0], digest.size());
}