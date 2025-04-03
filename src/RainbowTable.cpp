//
//  RainbowTable.cpp
//  SimdRainbowCrack
//
//  Created by Kryc on 15/02/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <cinttypes>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <format>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "SimdHashBuffer.hpp"

#include "Chain.hpp"
#include "RainbowTable.hpp"
#include "SmallString.hpp"
#include "Util.hpp"

void
RainbowTable::InitAndRunBuild(
    void
)
{
    if (m_Threads == 0)
    {
        m_Threads = std::thread::hardware_concurrency();
    }

    // Validate config and load header if exists
    if (!ValidateConfig())
    {
        std::cerr << "Configuration error" << std::endl;
        return;
    }

    m_Operation = "Building";

    // Calculate the count if needed
    if (m_Count == 0)
    {
        mpz_class keyspace = WordGenerator::WordLengthIndex(m_Max + 1, m_Charset) - WordGenerator::WordLengthIndex(m_Min, m_Charset);
        keyspace /= m_Length + 1;
        // Add 10% for overhead
        keyspace += (keyspace / 10);
        std::cerr << "Calculated chains required: " << keyspace.get_str() << std::endl;
        m_Count = keyspace.get_ui();
    }

    // Estimate table size
    double tableSize = sizeof(TableHeader) + (m_Count * m_Max);
    std::string tableSizeCh;
    tableSize = Util::SizeFactor(tableSize, tableSizeCh);
    std::cerr << "Estimated table size: " << std::fixed << std::setprecision(2) << tableSize << ' ' << tableSizeCh << " compressed (";
    tableSize = sizeof(TableHeader) + (m_Count * (m_Max + sizeof(rowindex_t)));
    tableSize = Util::SizeFactor(tableSize, tableSizeCh);
    std::cerr << tableSize << ' ' << tableSizeCh << " uncompressed)" << std::endl;

    // Write the table header if we didn't load from disk
    if (!m_PathLoaded)
    {
        StoreTableHeader();
        m_HashWidth = GetHashWidth(m_Algorithm);
        m_ChainWidth = GetChainWidth();
        m_Chains = (std::filesystem::file_size(m_Path) - sizeof(TableHeader)) / m_ChainWidth;
    }

    m_StartingChains = m_Chains;

    m_WriteHandle = fopen(m_Path.c_str(), "a");
    if (m_WriteHandle == nullptr)
    {
        std::cerr << "Unable to open table for writing" << std::endl;
        return;
    }

    // Create the main (io) dispatcher
    auto mainDispatcher = dispatch::CreateDispatcher(
        "main",
        dispatch::DoNothing
    );

    if (m_Threads > 1)
    {
        m_DispatchPool = dispatch::CreateDispatchPool("pool", m_Threads);

        for (size_t i = 0; i < m_Threads; i++)
        {
            m_DispatchPool->PostTask(
                dispatch::bind(
                    &RainbowTable::GenerateBlock,
                    this,
                    i,
                    i
                )
            );
        }
    }
    else
    {
        dispatch::PostTaskFast(
            dispatch::bind(
                &RainbowTable::GenerateBlock,
                this,
                0,
                0
            )
        );
    }

    // Wait on the main thread
    mainDispatcher->Wait();
}

void
RainbowTable::GenerateBlock(
    const size_t ThreadId,
    const size_t BlockId
)
{
    size_t blockStartId = m_StartingChains + (m_Blocksize * BlockId);

    // Check if we should end
    if (blockStartId >= m_Count)
    {
        dispatch::PostTaskToDispatcher(
            "main",
            dispatch::bind(
                &RainbowTable::BuildThreadCompleted,
                this,
                ThreadId
            )
        );
        return;
    }

    HybridReducer reducer(m_Min, m_Max, m_HashWidth, m_Charset);
    std::vector<SmallString> block(m_Blocksize);

    SimdHashBufferFixed<kSmallStringMaxLength> words;
    std::array<uint8_t, MAX_HASH_SIZE * MAX_LANES> hashes;

    // Calculate lower bound and add the current index
#ifdef BIGINT
    mpz_class counter = CalculateLowerBound() + blockStartId;
#else
    uint64_t counter = CalculateLowerBound() + blockStartId;
#endif
    const size_t hashWidth = m_HashWidth;
    const size_t lanes = SimdLanes();

    // Start measuring the block generation time
    const auto start = std::chrono::system_clock::now();

    const size_t iterations = m_Blocksize / lanes;
    for (size_t iteration = 0; iteration < iterations; iteration++)
    {
        // Set the chain start point
        for (size_t i = 0; i < lanes; i++)
        {
            const size_t length = WordGenerator::GenerateWord((char*)words[i], m_Max, counter++, m_Charset);
            words.SetLength(i, length);
        }

        // Perform the hash/reduce cycle
        for (size_t i = 0; i < m_Length; i++)
        {
            // Perform hash
            SimdHashOptimized(
                m_Algorithm,
                words.GetLengths(),
                words.ConstBuffers(),
                &hashes[0]
            );

            // Perform reduce
            for (size_t h = 0; h < lanes; h++)
            {
                const uint8_t* hash = &hashes[h * hashWidth];
                const size_t length = reducer.Reduce((char*)words[h], m_Max, hash, i);
                words.SetLength(h, length);
            }
        }

        // Save the chain information
        for (size_t h = 0; h < lanes; h++)
        {
            block[iteration * lanes + h].Set(words.Get(h));
        }
    }

    const auto end = std::chrono::system_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    //
    // Post a task to the main thread
    // to save this block
    //
    dispatch::PostTaskToDispatcher(
        "main",
        dispatch::bind(
            &RainbowTable::SaveBlock,
            this,
            ThreadId,
            BlockId,
            std::move(block),
            elapsed_ms.count()
        )
    );

    //
    // Post the next task
    //
    const size_t nextblock = BlockId + m_Threads;
    dispatch::PostTaskFast(
        dispatch::bind(
            &RainbowTable::GenerateBlock,
            this,
            ThreadId,
            nextblock
        )
    );
}

void
RainbowTable::WriteBlock(
    const size_t BlockId,
    const std::vector<SmallString>& Block
)
{
    // Create a byte buffer so we can do it on one shot
    size_t bufferSize = GetChainWidth() * GetBlocksize();
    std::vector<uint8_t> buffer(bufferSize);
    std::span<uint8_t> bufferSpan(buffer);
    // uint8_t* bufferptr = &buffer[0];
    size_t index = BlockId;
    // Loop through the chains and add them to the buffer
    for (size_t i = 0; i < Block.size(); i++)
    {
        rowindex_t rowindex = index++;
        auto chainDestination = bufferSpan.subspan(i * GetChainWidth(), GetChainWidth());
        auto endpointDestination = m_TableType == TypeUncompressed ? chainDestination.subspan(sizeof(rowindex_t)) : chainDestination;
        if (m_TableType == TypeUncompressed)
        {
            std::memcpy(chainDestination.data(), &rowindex, sizeof(rowindex_t));
        }
        auto endpointString = Block[i].Get();
        std::memcpy(endpointDestination.data(), endpointString.data(), endpointString.size());
    }
    // Perform the write in a single shot and flush
    fwrite(&buffer[0], bufferSize, sizeof(uint8_t), m_WriteHandle);
    fflush(m_WriteHandle);
    m_ChainsWritten += Block.size();
}

void
RainbowTable::OutputStatus(
    const SmallString& LastEndpoint
) const
{
    assert(dispatch::CurrentDispatcher() == dispatch::GetDispatcher("main").get());

    uint64_t averageMs = 0;
    for (auto const& [thread, val] : m_ThreadTimers)
    {
        averageMs += val;
    }
    averageMs /= m_Threads;

    double chainsPerSec = 1000.f * m_Blocksize / averageMs;
    double hashesPerSec = chainsPerSec * m_Length;

    std::string cpsChar, hpsChar;
    chainsPerSec = Util::NumFactor(chainsPerSec, cpsChar);
    hashesPerSec = Util::NumFactor(hashesPerSec, hpsChar);

    double chains = (double)(m_StartingChains + m_ChainsWritten);
    std::string chainsChar;
    chains = Util::NumFactor(chains, chainsChar);

    double percent = ((double)(m_StartingChains + m_ChainsWritten) / (double)m_Count) * 100.f;

    const std::string lastEndpointString(LastEndpoint.GetString());

    std::string status = std::format(
        "C:{:.1f}{}({:.1f}%) C/s:{:.1f}{} H/s:{:.1f}{} E:\"{}\"",
        chains,
        chainsChar,
        percent,
        chainsPerSec,
        cpsChar,
        hashesPerSec,
        hpsChar,
        lastEndpointString
    );

    if (status.size() > m_TerminalWidth)
    {
        status = status.substr(0, m_TerminalWidth);
    }
    else
    {
        // Pad with spaces to m_TerminalWidth
        if (status.size() < m_TerminalWidth)
        {
            status.append(m_TerminalWidth - status.size(), ' ');
        }
    }

    std::cerr << "\r" << status << std::flush;
}

void
RainbowTable::SaveBlock(
    const size_t ThreadId,
    const size_t BlockId,
    const std::vector<SmallString> Block,
    const uint64_t Time
)
{
    m_ThreadTimers[ThreadId] = Time;

    OutputStatus(Block[0]);

    if (BlockId == m_NextWriteBlock)
    {
        WriteBlock(BlockId, Block);
        m_NextWriteBlock++;
        while (m_WriteCache.find(m_NextWriteBlock) != m_WriteCache.end())
        {
            WriteBlock(m_NextWriteBlock, m_WriteCache.at(m_NextWriteBlock));
            m_WriteCache.erase(m_NextWriteBlock);
            m_NextWriteBlock++;
        }
    }
    else
    {
        m_WriteCache.emplace(BlockId, std::move(Block));
    }
}

bool
RainbowTable::SetType(
    const std::string Type
)
{
    if (Type == "compressed")
    {
        SetType(TypeCompressed);
    }
    else if (Type == "uncompressed")
    {
        SetType(TypeUncompressed);
    }
    else
    {
        SetType(TypeInvalid);
        return false;
    }
    return true;
}

float
RainbowTable::GetCoverage(
    void
)
{
    mpz_class lowerbound = WordGenerator::WordLengthIndex(m_Min, m_Charset);
    mpz_class upperbound = WordGenerator::WordLengthIndex(m_Max + 1, m_Charset);
    mpf_class delta = upperbound - lowerbound;

    mpf_class count = m_Chains * m_Length;
    mpf_class percentage = (count / delta) * 100;

    return percentage.get_d();
}

void
RainbowTable::StoreTableHeader(
    void
) const
{
    TableHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = kMagic;
    hdr.type = m_TableType;
    hdr.algorithm = m_Algorithm;
    hdr.min = m_Min;
    hdr.max = m_Max;
    hdr.length = m_Length;
    hdr.charsetlen = m_Charset.size();
    strncpy(hdr.charset, &m_Charset[0], sizeof(hdr.charset));

    std::ofstream fs(m_Path, std::ios::out | std::ios::binary);
    fs.write((const char*)&hdr, sizeof(hdr));
    fs.close();
}

/* static */ bool
RainbowTable::GetTableHeader(
    const std::filesystem::path& Path,
    TableHeader* Header
)
{
    if (std::filesystem::file_size(Path) < sizeof(TableHeader))
    {
        return false;
    }

    std::ifstream fs(Path, std::ios::binary);
    if (!fs.is_open())
    {
        return false;
    }

    fs.read((char*)Header, sizeof(TableHeader));
    fs.close();

    if (Header->magic != kMagic)
    {
        return false;
    }
    return true;
}

/* static */ bool
RainbowTable::IsTableFile(
    const std::filesystem::path& Path
)
{
    TableHeader hdr;
    return GetTableHeader(Path, &hdr);
}

bool
RainbowTable::LoadTable(
    void
)
{
    TableHeader hdr;

    size_t fileSize = std::filesystem::file_size(m_Path);

    if(fileSize < sizeof(TableHeader))
    {
        std::cerr << "Not enough data in file" << std::endl;
        return false;
    }

    if (!GetTableHeader(m_Path, &hdr))
    {
        std::cerr << "Error reading table header" << std::endl;
        return false;
    }

    m_TableType = (TableType)hdr.type;
    m_Algorithm = (HashAlgorithm)hdr.algorithm;
    m_Min = hdr.min;
    m_Max = hdr.max;
    m_Length = hdr.length;
    std::string_view charset(hdr.charset, hdr.charsetlen);
    m_Charset = charset;
    m_HashWidth = GetHashWidth(m_Algorithm);
    m_ChainWidth = GetChainWidth();
    m_Chains = (std::filesystem::file_size(m_Path) - sizeof(TableHeader)) / m_ChainWidth;

    size_t dataSize = fileSize - sizeof(TableHeader);
    if (dataSize % m_ChainWidth != 0)
    {
        std::cerr << "Invalid or currupt table file. Data not a multiple of chain width" << std::endl;
        return false;
    }

    return true;
}

const size_t
RainbowTable::GetCount(
    void
) const
{
    return (std::filesystem::file_size(m_Path) - sizeof(TableHeader)) / GetChainWidth();
}

bool
RainbowTable::ValidateConfig(
    void
)
{
    if (m_Path.empty())
    {
        std::cerr << "No rainbow table file specified" << std::endl;
        return false;
    }

    if (TableExists())
    {
        LoadTable();
        m_PathLoaded = true;
    }

    if (m_Max == 0)
    {
        std::cerr << "No max length specified" << std::endl;
        return false;
    }

    if (m_Min == 0)
    {
        std::cerr << "No min length specified" << std::endl;
        return false;
    }

#ifndef BIGINT
    mpz_class upperbound = WordGenerator::WordLengthIndex(m_Max + 1, m_Charset);
    if (upperbound > std::numeric_limits<uint64_t>::max())
    {
        std::cerr << "Max length exceeds 64-bit integer limit" << std::endl;
        std::cerr << "To compile with BIGINT support, recompile with -DBIGINT" << std::endl;
        return false;
    }
#endif

    if (m_Max > kSmallStringMaxLength)
    {
        std::cerr << "Max length is above supported maximum" << std::endl;
        return false;
    }

    if (m_Min > kSmallStringMaxLength)
    {
        std::cerr << "Min length is above supported maximum" << std::endl;
        return false;
    }

    if (m_Length == 0)
    {
        std::cerr << "No chain length specified" << std::endl;
        return false;
    }

    if (m_Algorithm == HashAlgorithmUndefined)
    {
        std::cerr << "No algorithm specified" << std::endl;
        return false;
    }

    const size_t optimizedMax = GetOptimizedLength(m_Algorithm);
    if (m_Max > optimizedMax)
    {
        std::cerr << "Max length cannot exceed optimized hash limit (" << optimizedMax << ")" << std::endl;
        return false;
    }

    if (m_Min > optimizedMax)
    {
        std::cerr << "Min length cannot exceed optimized hash limit (" << optimizedMax << ")" << std::endl;
        return false;
    }

    if (m_TableType == TypeInvalid)
    {
        std::cerr << "Invalid table type specified" << std::endl;
        return false;
    }

    if (m_Blocksize == 0)
    {
        std::cerr << "No block size specified" << std::endl;
        return false;
    }

    if (m_Blocksize % SimdLanes() != 0)
    {
        std::cerr << "Block size must be a multiple of Simd width (" << SimdLanes() << ")" << std::endl;
        return false;
    }

    if (m_Charset.empty())
    {
        std::cerr << "No or invalid charset specified" << std::endl;
        return false;
    }

    return true;
}

void
RainbowTable::BuildThreadCompleted(
    const size_t ThreadId
)
{
    m_ThreadsCompleted++;
    if (m_ThreadsCompleted == m_Threads)
    {
        // Stop the pool
        if (m_DispatchPool != nullptr)
        {
            m_DispatchPool->Stop();
            m_DispatchPool->Wait();
        }

        // Stop the current (main) dispatcher
        dispatch::CurrentDispatcher()->Stop();
    }
}

bool
RainbowTable::UnmapTable(
    void
)
{
    // First check if the HashList is initialized
    if (m_RainbowTable.Initialized())
    {
        m_RainbowTable.Clear();
    }

    return cracktools::UnmapFileSpan(m_MappedTable, m_MappedTableFd);
}

bool
RainbowTable::MapTable(
    const bool ReadOnly
)
{
    // Check if it is already mapped
    if (TableMapped())
    {
        if (m_MappedReadOnly == ReadOnly)
        {
            return true;
        }
        // Unmap it to remap writable
        if (!UnmapTable())
        {
            std::cerr << "Unmapping table failed" << std::endl;
            return false;
        }
    }

    int flags = ReadOnly ? MAP_PRIVATE : MAP_SHARED;
    int prot = ReadOnly ? PROT_READ : (PROT_READ | PROT_WRITE);
    auto mapping = cracktools::MmapFileSpan<uint8_t>(m_Path, prot, flags, /*madvise*/ true);

    if (!mapping.has_value())
    {
        std::cerr << "Error: unable to map table file " << m_Path.filename() << std::endl;
        return false;
    }

    auto [mapped, fp] = mapping.value();

    m_MappedTable = mapped;
    m_MappedTableFd = fp;
    const size_t offset = m_TableType == TypeUncompressed ? 0 : sizeof(rowindex_t);
    m_RainbowTable.Initialize(m_MappedTable.subspan(sizeof(TableHeader)), m_HashWidth, offset, GetChainWidth(), false);

    return true;
}

std::span<const uint8_t>
RainbowTable::GetRecordAt(
    const size_t Index
) const
{
    return m_MappedTable.subspan(
        sizeof(TableHeader) + (Index * GetChainWidth()),
        GetChainWidth()
    );
}

std::span<const uint8_t>
RainbowTable::GetEndpointAt(
    const size_t Index
) const
{
    // Get the pointer to the chain
    auto entry = GetRecordAt(Index);
    // Return the pointer to the endpoint
    return m_TableType == TypeUncompressed ? entry.subspan(sizeof(rowindex_t)) : entry;
}

/* static */
const std::string
RainbowTable::DoHashHex(
    const uint8_t* Data,
    const size_t Length,
    const HashAlgorithm Algorithm
)
{
    uint8_t buffer[MAX_BUFFER_SIZE];
    DoHash(Data, Length, buffer, Algorithm);
    return Util::ToHex(buffer, GetHashWidth(Algorithm));
}

std::optional<size_t>
RainbowTable::FindEndpoint(
    const char* Endpoint,
    const size_t Length
) const
{
    // We need a null-padded buffer to compare against
    std::vector<uint8_t> comparitor(m_Max, 0);
    memcpy(&comparitor[0], Endpoint, Length);

    // Uncompressed tables are just flat files
    // of endpoints, each of m_Max width. They are
    // unsorted so we need to do a Linear search
    if (m_TableType == TypeCompressed)
    {
        return m_RainbowTable.FindLinear(
            comparitor
        );
    }
    // Uncompressed files are flat binary files of an
    // unsigned integer index, followed by the text
    // endpoint of m_Max width. They are sortd by endpoint
    // so we can do a binary search
    else
    {
        return m_RainbowTable.Find(comparitor);
    }
}

// Checks a chain assuming the given iteration
std::optional<std::string>
RainbowTable::CheckIteration(
    const HybridReducer& Reducer,
    const std::vector<uint8_t>& Target,
    const size_t Iteration
) const
{
    uint8_t hash[m_HashWidth];
    char    reduced[m_Max];
    size_t  length;

    memcpy(&hash[0], &Target[0], m_HashWidth);

    for (size_t j = Iteration; j < m_Length - 1; j++)
    {
        length = Reducer.Reduce(&reduced[0], m_Max, &hash[0], j);
        DoHash((uint8_t*)&reduced[0], length, &hash[0]);
    }

    // Final reduction
    length = Reducer.Reduce(&reduced[0], m_Max, &hash[0], m_Length - 1);

    // Check end, if it matches, we can perform one full chain to see if we find it
    auto index = FindEndpoint(&reduced[0], length);
    if (index.has_value())
    {
        return ValidateChain(index.value(), &Target[0]);
    }
    return std::nullopt;
}

void
RainbowTable::CrackOneWorker(
    const size_t ThreadId,
    const std::vector<uint8_t> Target
)
{
    HybridReducer reducer(m_Min, m_Max, m_HashWidth, m_Charset);

    m_CrackingThreadsRunning++;

    for (ssize_t i = m_Length - 1 - ThreadId; i >= 0 && !m_Cracked; i -= m_Threads)
    {
        auto result = CheckIteration(reducer, Target, i);
        bool cracked = m_Cracked;
        if (result.has_value() && !cracked && m_Cracked.compare_exchange_strong(cracked, true))
        {
            m_Cracked = true;
            m_LastCracked = std::make_tuple(Util::ToHex(&Target[0], Target.size()), result.value());
            m_CrackedResults.push_back(m_LastCracked);
        }
    }

    m_CrackingThreadsRunning--;
}

std::optional<std::string>
RainbowTable::CrackOne(
    const std::string& Hash
)
{
    if (Hash.size() != m_HashWidth * 2)
    {
        std::cerr << "Invalid length of provided hash: " << Hash.size() << " != " << m_HashWidth * 2 << std::endl;
        std::cerr << "Hash: '" << Hash << "'" << std::endl;
        return std::nullopt;
    }

    HybridReducer reducer(m_Min, m_Max, m_HashWidth, m_Charset);
    auto target = Util::ParseHex(Hash);
    std::optional<std::string> result;

    // Perform linear check
    if (m_Threads == 1)
    {
        for (ssize_t i = m_Length - 1; i >= 0; i--)
        {
            result = CheckIteration(reducer, target, i);
            if (result.has_value())
            {
                break;
            }
        }
    }
    else
    {
        m_ThreadsCompleted = m_Threads;

        // Dispatch the work to the worker threads
        for (size_t i = 0; i < m_Threads; i++)
        {
            m_DispatchPool->PostTask(
                dispatch::bind(
                    &RainbowTable::CrackOneWorker,
                    this,
                    i,
                    target
                )
            );
        }

        // Wait for at least one thread to start
        while (m_CrackingThreadsRunning == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Wait for the threads to finish
        while (m_CrackingThreadsRunning != 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Check if we found the result
        if (m_Cracked)
        {
            result = std::get<1>(m_LastCracked);
        }
    }

    return result;
}

std::vector<std::tuple<std::string, std::string>>
RainbowTable::Crack(
    std::string& Target
)
{
    // Mmap the table
    if (!MapTable(true))
    {
        std::cerr << "Error mapping the table" << std::endl;
        return {};
    }

    // Check the argument
    if (!Util::IsHex(Target) && !std::filesystem::exists(Target))
    {
        std::cerr << "Invalid target hash or file" << std::endl;
        return {};
    }

    m_Operation = "Cracking";

    // Index the table for multiple lookups
    if (!m_IndexDisable)
    {
        std::cerr << "Indexing table..";
        // IndexTable();
        std::cerr << " done." << std::endl;
    }

    if (m_Threads == 0)
    {
        m_Threads = std::thread::hardware_concurrency();
    }

    // Create the dispatch pool
    if (m_Threads > 1)
    {
        m_DispatchPool = dispatch::CreateDispatchPool("pool", m_Threads);
    }

    // Figure out if this is a single hash
    if (Util::IsHex(Target))
    {
        auto result = CrackOne(Target);
        if (result)
        {
            std::cout << Target << m_Separator << result.value() << std::endl;
        }
    }
    // Check if it is a file
    else if (std::filesystem::exists(Target))
    {
        // Open the input file handle
        m_HashFileStream = std::ifstream(Target);

        std::string line;
        while (std::getline(m_HashFileStream, line))
        {
            m_ThreadsCompleted = 0;
            m_Cracked = false;
            auto result = CrackOne(line);
            if (result.has_value())
            {
                std::cout << line << m_Separator << result.value() << std::endl;
            }
        }
    }

    // Stop the pool
    if (m_DispatchPool != nullptr)
    {
        m_DispatchPool->Stop();
        m_DispatchPool->Wait();
    }

    return std::move(m_CrackedResults);
}

std::optional<std::string>
RainbowTable::ValidateChain(
    const size_t ChainIndex,
    const uint8_t* Target
) const
{
    size_t length;
    std::vector<uint8_t> hash(m_HashWidth);
    std::vector<char> reduced(m_Max);
    HybridReducer reducer(m_Min, m_Max, m_HashWidth, m_Charset);
#ifdef BIGINT
    mpz_class counter = WordGenerator::WordLengthIndex(m_Min, m_Charset);
#else
    uint64_t counter = WordGenerator::WordLengthIndex64(m_Min, m_Charset);
#endif
    counter += ChainIndex;

    auto start = WordGenerator::GenerateWord(counter,m_Charset);
    length = start.size();
    memcpy(&reduced[0], start.c_str(), length);

    for (size_t i = 0; i < m_Length; i++)
    {
        DoHash((uint8_t*)&reduced[0], length, &hash[0]);
        if (memcmp(Target, &hash[0], m_HashWidth) == 0)
        {
            return std::string(&reduced[0], &reduced[length]);
        }
        length = reducer.Reduce(&reduced[0], m_Max, &hash[0], i);
    }
    return {};
}

RainbowTable::~RainbowTable(
    void
)
{
    Reset();
}

void
RainbowTable::Reset(
    void
)
{
    UnmapTable();

    m_Path.clear();
    m_PathLoaded = false;
    m_Algorithm = HashAlgorithmUndefined;
    m_Min = 0;
    m_Max = 0;
    m_Length = 0;
    m_Blocksize = 1024;
    m_Count = 0;
    m_Threads = 0;
    m_Charset.clear();
    m_HashWidth = 0;
    m_ChainWidth = 0;
    m_Chains = 0;
    m_TableType = TypeCompressed;
    // For building
    m_StartingChains = 0;
    m_WriteHandle = nullptr;
    m_NextWriteBlock = 0;
    m_WriteCache.clear();
    if (m_DispatchPool != nullptr)
    {
        m_DispatchPool->Stop();
        m_DispatchPool->Wait();
        m_DispatchPool = nullptr;
    }
    m_ThreadsCompleted = 0;
}

#pragma clang unsafe_buffer_usage begin

#ifdef __APPLE__
int
SortCompareEndpoints(
    void* Arguments,
    const void* Comp1,
    const void* Comp2
)
{
    return memcmp((uint8_t*)Comp1 + sizeof(rowindex_t), (uint8_t*)Comp2 + sizeof(rowindex_t), (size_t)Arguments);
}
#else
int
SortCompareEndpoints(
    const void* Comp1,
    const void* Comp2,
    void* Arguments
)
{
    return memcmp((uint8_t*)Comp1 + sizeof(rowindex_t), (uint8_t*)Comp2 + sizeof(rowindex_t), (size_t)Arguments);
}
#endif

int
SortCompareStartpoints(
    const void* Comp1,
    const void* Comp2
)
{
    return *((rowindex_t*)Comp1) - *((rowindex_t*)Comp2);
}

#pragma clang unsafe_buffer_usage end

void
RainbowTable::SortStartpoints(
    void
)
{
    if (m_TableType == TypeCompressed)
    {
        std::cerr << "Unable to sort compressed tables by start point" << std::endl;
        return;
    }
    
    if (!MapTable(false))
    {
        std::cerr << "Error mapping table for sort"  << std::endl;
        return;
    }

    auto tablerows = m_MappedTable.subspan(sizeof(TableHeader));
    qsort(tablerows.data(), GetCount(), GetChainWidth(), SortCompareStartpoints);
}

void
RainbowTable::SortTable(
    void
)
{
    if (!MapTable(false))
    {
        std::cerr << "Error mapping table for sort"  << std::endl;
        return;
    }

    if (m_TableType == TypeUncompressed)
    {
        m_RainbowTable.Sort();
    }
    else
    {
        SortStartpoints();
    }
}

#pragma clang unsafe_buffer_usage begin

void
RainbowTable::RemoveStartpoints(
    void
)
{
    // Ensure the table is mapped writable
    if (!MapTable(false))
    {
        std::cerr << "Unable to map the table" << std::endl;
        return;
    }

    // Change the table type in the header
    (*(TableHeader*)m_MappedTable.data()).type = TypeCompressed;

    // Loop through the chains
    uint8_t* tableBase = m_MappedTable.data() + sizeof(TableHeader);
    uint8_t* writepointer = tableBase;
    for (size_t chain = 0; chain < GetCount(); chain++)
    {
        uint8_t* endpoint = tableBase + (chain * ChainWidthForType(TypeUncompressed, GetMax())) + sizeof(rowindex_t);
        memmove(writepointer, endpoint, GetMax());
        writepointer += GetMax();
    }

    // Truncate the file
    if (!UnmapTable())
    {
        std::cerr << "Error unmapping table after removing start points" << std::endl;
        return;
    }

    size_t newSize = sizeof(TableHeader) + (GetCount() * GetMax());
    auto result = truncate(m_Path.c_str(), newSize);
    if (result != 0)
    {
        std::cerr << "Error truncating file" << std::endl;
        return;
    }
}

#pragma clang unsafe_buffer_usage end

void
RainbowTable::ChangeType(
    const std::filesystem::path& Destination,
    const TableType Type
)
{
    FILE* fhw;
    TableHeader hdr;

    if (m_TableType == Type)
    {
        std::cerr << "Won't convert to same type" << std::endl;
        return;
    }

    // Output some basic information about the
    // current table
    std::cout << "Table type: " << GetType() << std::endl;
    std::cout << "Chain width: " << GetChainWidth() << std::endl;
    std::cout << "Exporting " << m_Chains << " chains" << std::endl;

    // If we are converting to a compressed file we need to do
    // it in two stages. We need a full copy of the file so that
    // we can then sort it by start points. Then we remove all 
    // startpoints from the file
    if (Type == TypeCompressed)
    {
        if (!std::filesystem::copy_file(m_Path, Destination, std::filesystem::copy_options::overwrite_existing))
        {
            std::cerr << "Error copying file for conversion" << std::endl;
            return;
        }
    }
    else
    {
        // Map the current table to memory
        if (!MapTable(true))
        {
            std::cerr << "Error mapping table"  << std::endl;
            return;
        }

        // Copy the existing header but change the type to uncompressed
        hdr = *((TableHeader*)m_MappedTable.data());
        hdr.type = TypeUncompressed;

        // Open the destination for writing
        fhw = fopen(Destination.c_str(), "w");
        if (fhw == nullptr)
        {
            std::cerr << "Error opening desination table for write: " << Destination << std::endl;
            return;
        }

        // Write the header
        fwrite(&hdr, sizeof(hdr), 1, fhw);

        // Pointer to track the next read target
        uint8_t* next = m_MappedTable.data() + sizeof(TableHeader);

        // Loop through and write each index followed by the next endpoint
        for (rowindex_t index = 0; index < m_Chains; index++, next += GetChainWidth())
        {
            fwrite(&index, sizeof(rowindex_t), 1, fhw);
            fwrite(next, sizeof(char), m_Max, fhw);
        }
        fclose(fhw);
    }

    // Perform sort and cleanup work on the new table
    RainbowTable newtable;
    newtable.SetPath(Destination);

    if (!newtable.ValidTable())
    {
        std::cerr << "Decompressed table does not seem valid" << std::endl;
        return;
    }

    if (!newtable.LoadTable())
    {
        std::cerr << "Error loading new table" << std::endl;
        return;
    }

    std::cout << "Sorting " << newtable.GetCount() << " chains" << std::endl;

    if (m_TableType == TypeCompressed)
    {
        newtable.SortTable();
    }
    else
    {
        // Sort the table by startpoint
        newtable.SortStartpoints();
        // Remove all startpoints and truncate
        newtable.RemoveStartpoints();
    }
}

/* static */ const Chain
RainbowTable::GetChain(
    const std::filesystem::path& Path,
    const size_t Index
)
{
    TableHeader hdr;
    GetTableHeader(Path, &hdr);

    std::string charset(&hdr.charset[0], &hdr.charset[hdr.charsetlen]);

    Chain chain;
    chain.SetIndex(Index);
    chain.SetLength(hdr.length);

    FILE* fh = fopen(Path.c_str(), "r");
    fseek(fh, sizeof(TableHeader), SEEK_SET);
    fseek(fh, ChainWidthForType((TableType)hdr.type, hdr.max) * Index, SEEK_CUR);

    rowindex_t start = Index;
    if (hdr.type == (uint8_t)TypeUncompressed)
    {
        fread(&start, sizeof(rowindex_t), 1, fh);
    }
    mpz_class lowerbound = WordGenerator::WordLengthIndex(hdr.min, charset);
    static_assert(sizeof(rowindex_t) == 4 || (sizeof(unsigned long int) == sizeof(rowindex_t)));
    auto word = WordGenerator::GenerateWord(lowerbound + (unsigned long int)start, charset);
    chain.SetStart(std::string_view(word));

    std::string endpoint;
    endpoint.resize(hdr.max);
    fread(&endpoint[0], sizeof(char), hdr.max, fh);
    // Trim nulls
    endpoint.resize(strlen(endpoint.c_str()));
    chain.SetEnd(std::string_view(endpoint));

    return chain;
}

/* static */ const Chain
RainbowTable::ComputeChain(
    const size_t Index,
    const size_t Min,
    const size_t Max,
    const size_t Length,
    const HashAlgorithm Algorithm,
    const std::string& Charset
)
{
    mpz_class counter;
    Chain chain;
    size_t hashLength;
    std::string start;

    hashLength = GetHashWidth(Algorithm);

    chain.SetIndex(Index);
    chain.SetLength(Length);

    counter = WordGenerator::WordLengthIndex(Min, Charset);
    counter += Index;

    start = WordGenerator::GenerateWord(counter, Charset);
    chain.SetStart(std::string_view(start));

    HybridReducer reducer(Min, Max, hashLength, Charset);

    std::vector<uint8_t> hash(hashLength);
    std::vector<char> reduced(Max);
    size_t reducedLength = start.size();

    memcpy(&reduced[0], start.c_str(), reducedLength);

    for (size_t i = 0; i < Length; i++)
    {
        DoHash((uint8_t*)&reduced[0], reducedLength, &hash[0], Algorithm);
        reducedLength = reducer.Reduce(&reduced[0], Max, &hash[0], i);
    }

    std::span<char> reducedSpan(reduced);
    chain.SetEnd(reducedSpan.subspan(0, reducedLength));
    return chain;
}