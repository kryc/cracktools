//
//  Database.cpp
//  CrackDB++
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "SimdHash.hpp"

#include "CrackDatabase.hpp"
#include "LineReader.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"

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

    auto path = m_HashDatabases.at(Algorithm);

    if (!std::filesystem::exists(path))
    {
        std::cerr << "Trying to sort non-existant file" << std::endl;
        return;
    }

    auto mapping = cracktools::MmapFileSpan<DatabaseRecord>(
        path,
        PROT_READ|PROT_WRITE,
        MAP_SHARED
    );

    if (!mapping.has_value())
    {
        std::cerr << "Error mapping file " << path.filename() << "for sorting" << std::endl;
        return;
    }

    auto [mapped, fp] = mapping.value();

    std::sort(mapped.begin(), mapped.end());
    
    cracktools::UnmapFileSpan(mapped, fp);

    std::cerr << " Completed" << std::endl;
}



void
CrackDatabase::BuildWorker(
    const size_t ThreadIndex
)
{
    std::array<uint8_t, MAX_DIGEST_LENGTH> digest;
    std::span<uint8_t> digestSpan = digest;
    std::span<uint8_t, HASH_BYTES> hashSpan = digestSpan.first<HASH_BYTES>();

    // Keep a local copy of the map of wordfile handles to avoid locking for each write
    std::map<size_t, std::shared_ptr<Wordfile>> localWordfiles;

    DatabaseRecord record;
    std::string_view line;
    std::string temp;
    size_t index = 0;
    while (m_LineReader.ReadLine(line, m_InputMutex))
    {
        m_Processed++;

        if (Util::IsHexlified(line))
        {
            temp = Util::UnHexlify(line);
            line = temp;
        }

        const size_t size = line.size();

        if (size < m_Min)
        {
            m_Small++;
            continue;
        }

        if (size > m_Max)
        {
            m_Large++;
            continue;
        }

        // Make sure that the wordfile mutex exists
        if (m_WordfileMutexes.find(size) == m_WordfileMutexes.end())
        {
            std::lock_guard<std::mutex> lock(m_WordfilesMutex);
            if (m_WordfileMutexes.find(size) == m_WordfileMutexes.end())
            {
                m_WordfileMutexes[size]; // Default construct the mutex
            }
        }

        // Make sure the wordfile exists
        if (m_CacheWordFiles && localWordfiles.find(size) == localWordfiles.end())
        {
            std::lock_guard<std::mutex> lock(m_WordfilesMutex);
            if (m_Wordfiles.find(size) == m_Wordfiles.end())
            {
                auto wordfile = std::make_shared<Wordfile>(m_Path, size, true);
                localWordfiles[size] = wordfile;
                m_Wordfiles[size] = wordfile;
            }
            else
            {
                localWordfiles[size] = m_Wordfiles[size];
            }
        }

        size_t wordIndex;
        if (m_CacheWordFiles)
        {
            std::lock_guard<std::mutex> lock(m_WordfileMutexes[size]);
            auto wordfile = localWordfiles[size];
            wordIndex = wordfile->Add(line);
        }
        else
        {
            std::lock_guard<std::mutex> lock(m_WordfileMutexes[size]);
            Wordfile wf(m_Path, size, true);
            wordIndex = wf.Add(line);
        }

        for (auto algorithm : m_Algorithms)
        {
            // Do the hash
            simdhash::SimdHashSingle(algorithm, line, digestSpan);

            // Copy the hash into the next record
            record.SetHash(hashSpan);

            // Set the index and the length
            record.Length = line.size();
            record.Index = wordIndex;

            // Write the record to the file
            {
                std::lock_guard<std::mutex> lock(m_HandleMutexes[algorithm]);
                m_HandleMap[algorithm].write((char*)&record, sizeof(record));
            }
        }

        if (ThreadIndex == 0 && index++ % 10000 == 0)
        {
            std::cerr << "\r#: " << m_Processed.load()
                      << "/" << m_InputLineCount << " (" << (m_InputLineCount > 0 ? (m_Processed.load() * 100 / m_InputLineCount) : 0) << "%)"
                      << " <: " << m_Small.load()
                      << " >: " << m_Large.load()
                      << std::flush;
        }
    }
}

const bool
CrackDatabase::Build(
    const std::span<HashAlgorithm> Algorithms,
    const std::string_view InputWords
)
{
    std::cerr << "Building database" << std::endl;

    // Check number of threads
    if (m_Threads == 0)
    {
        m_Threads = std::thread::hardware_concurrency();
    }

    std::filesystem::path wordDir = GetWordsPath();
    if (!std::filesystem::create_directories(wordDir))
    {
        std::cerr << "Error creating words directory" << std::endl;
        return false;
    }

    // If the algorithms vector is empty, build all supported algorithms
    m_Algorithms = Algorithms;
    if (m_Algorithms.size() == 0)
    {
        m_Algorithms = simdhash::SimdHashAlgorithms;
    }

    for (auto algorithm : m_Algorithms)
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

        m_HandleMap[algorithm] = std::ofstream(dbPath, std::ios::out|std::ios::binary);
    }

    if (m_HandleMap.empty())
    {
        std::cerr << "No valid databases to build." << std::endl;
        return false;
    }

    m_LineReader.SetInputFile(InputWords);

    // Get the number of lines in the input file
    m_InputLineCount = 0;
    if (!m_NoCount)
    {
        std::cerr << "Counting input lines..." << std::flush;
        LineCounter<> lineCounter(InputWords);
        m_InputLineCount = lineCounter.CountLines();
    }

    // Initialize all algorithm handle mutexes
    for (auto algorithm : m_Algorithms)
    {
        m_HandleMutexes[algorithm]; // Default construct the mutex
    }

    // Create our worker pool
    m_DispatchPool = dispatch::CreateDispatchPool("worker", m_Threads);
    for (size_t i = 0; i < m_Threads; i++)
    {
        m_DispatchPool->PostTask(
            dispatch::bind(
                &CrackDatabase::BuildWorker,
                this,
                i
            )
        );
    }
    m_DispatchPool->KeepAlive(false);
    m_DispatchPool->Wait();
    m_DispatchPool->Stop();

    std::cerr << "Sorting databases..." << std::endl;

    for (auto& [algorithm, handle] : m_HandleMap)
    {
        handle.close();
        // Add the unsorted database
        m_HashDatabases[algorithm] = DatabaseFile(algorithm);
    }

    const size_t new_threads = std::min(m_Threads, m_HashDatabases.size());
    m_DispatchPool = dispatch::CreateDispatchPool("sorter", new_threads);
    for (auto& [algorithm, path] : m_HashDatabases)
    {
        m_DispatchPool->PostTask(
            dispatch::bind(
                &CrackDatabase::Sort,
                this,
                algorithm
            )
        );
    }
    m_DispatchPool->KeepAlive(false);
    m_DispatchPool->Wait();

    return true;
}

const
std::filesystem::path
CrackDatabase::DatabaseFile(
    const HashAlgorithm Algorithm
) const
{
    std::string basename = std::string(HashAlgorithmToString(Algorithm)) + ".db";
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
    const std::span<const uint8_t> Target,
    const DatabaseFileMapping Mapping,
    const size_t Index,
    const HashAlgorithm Algorithm
) const
{
    // Check this entry then seek backwards while we have
    // a matching initial hash bytes
    std::array<uint8_t, MAX_HASH_SIZE> temp_hash;
    std::span<uint8_t> temp_hash_span = temp_hash;
    std::span<uint8_t> hash_span = temp_hash_span.subspan(0, Target.size());

    // Define a subspan for the hash bytes only
    std::span<const uint8_t> targetBytes = Target.subspan(0, HASH_BYTES);

    // Scan from the provided index backwards
    for (ssize_t i = Index;
        i >= 0 && cracktools::Memcmp(Mapping[i].GetHash(), targetBytes) == 0;
        --i
    )
    {
        for (auto& wf : GetAllWordFiles(Mapping[i].Length, false))
        {
            auto words = wf->GetAll(Mapping[i].Index);
            for (auto& word : words)
            {
                // Check the hash
                simdhash::SimdHashSingle(Algorithm, word, temp_hash_span);
                if (cracktools::Memcmp(hash_span, Target) == 0)
                {
                    return std::string(&word[0], word.size());
                }
            }
        }
    }

    // Seek forwards
    for (
        size_t i = Index + 1;
        i < Mapping.size() && cracktools::Memcmp(Mapping[i].GetHash(), targetBytes) == 0;
        ++i
    )
    {
        for (auto& wf : GetAllWordFiles(Mapping[i].Length, false))
        {
            auto words = wf->GetAll(Mapping[i].Index);
            for (auto& word : words)
            {
                // Check the hash
                simdhash::SimdHashSingle(Algorithm, word, temp_hash_span);
                if (cracktools::Memcmp(hash_span, Target) == 0)
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

std::optional<const DatabaseFileMapping>
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
    const DatabaseFileMapping Mapping,
    const std::span<const uint8_t> Hash
) const
{
    ssize_t low = 0;
    ssize_t high = Mapping.size() - 1;
    std::span<const uint8_t> hashSpan = Hash.subspan(0, HASH_BYTES);

    while (low <= high)
    {
        const ssize_t mid = low + (high - low) / 2;
        const int cmp = cracktools::Memcmp(Mapping[mid].GetHash(), hashSpan);
        if (cmp == 0)
        {
            auto result = CheckResult(
                Hash,
                Mapping,
                mid,
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
    const std::span<const uint8_t> Hash
) const
{
    // Try pulling the mapping directly from the cache
    auto mapping = GetCachedDatabaseMapping(Algorithm);
    if (mapping.has_value())
    {
        return Lookup(Algorithm, mapping.value(), Hash);
    }
    
    // Fallback to open the database. This will check if we have this algorithm
    auto database = GetDatabase(Algorithm);
    if (!database.has_value())
    {
        return std::nullopt;
    }

    return Lookup(Algorithm, database.value()->GetMapping(), Hash);
}

const std::optional<std::string>
CrackDatabase::Lookup(
    const std::span<uint8_t> Hash
) const
{
    const HashAlgorithm algorithm = DetectHashAlgorithm(Hash.size());

    if (algorithm == HashAlgorithmUndefined)
    {
        std::cerr << "Invalid hash: " << Util::ToHex(&Hash[0], Hash.size()) << std::endl;
        return std::nullopt;
    }

    return Lookup(algorithm, Hash);
}

void
CrackDatabase::OutputResult(
    const std::string_view Hash,
    std::string_view Value,
    std::ostream& Stream
) const
{
    std::string temp;
    if (m_Hex && Util::NeedsHexlify(Value))
    {
        temp = Util::Hexlify(Value);
        Value = temp;
    }
    
    if (m_PasswordsOnly)
    {
        Stream << Value << std::endl;
    }
    else
    {
        Stream << Util::ToLower(Hash) << m_Separator << Value << std::endl;
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
    for (auto algorithm : simdhash::SimdHashAlgorithms)
    {
        if (HasAlgorithm(algorithm))
        {
            m_DatabaseCache[algorithm] = std::make_shared<const MappedDatabase>(algorithm, DatabaseFile(algorithm));
        }
    }
    return m_DatabaseCache.size();
}

const bool
CrackDatabase::CrackFile(
    const std::string_view HashfileInput
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
        m_InputFileStream.open(std::string(HashfileInput), std::ios::in);
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
    const std::string_view Value
)
{
    std::vector<uint8_t> digest(GetDigestLength(Algorithm));
    simdhash::SimdHashSingle(Algorithm, Value, digest);
    return Lookup(Algorithm, digest);
}