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

/* static */ const size_t
RainbowTable::ChainWidthForType(
    const TableType Type,
    const size_t Max
)
{ 
    return Type == TypeCompressed ? sizeof(TableRecordCompressed) : sizeof(TableRecord);
}

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
    double tableSize = sizeof(TableHeader) + (m_Count * sizeof(TableRecordCompressed));
    std::string tableSizeCh;
    tableSize = Util::SizeFactor(tableSize, tableSizeCh);
    std::cerr << "Estimated table size: " << std::fixed << std::setprecision(2) << tableSize << ' ' << tableSizeCh << " compressed (";
    tableSize = sizeof(TableHeader) + (m_Count * sizeof(TableRecord));
    tableSize = Util::SizeFactor(tableSize, tableSizeCh);
    std::cerr << tableSize << ' ' << tableSizeCh << " uncompressed)" << std::endl;

    // Write the table header if we didn't load from disk
    if (!m_PathLoaded)
    {
        StoreTableHeader();
        m_HashWidth = GetHashWidth(m_Algorithm);
        m_Chains = (std::filesystem::file_size(m_Path) - sizeof(TableHeader)) / GetChainWidth();
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

    WordGenerator wordGenerator(m_Charset);
    wordGenerator.GenerateParsingLookupTable();
    HybridReducer reducer(m_Min, m_Max, m_Charset);
    std::vector<TableRecord> block(m_Blocksize);

    SimdHashBufferFixed<kSmallStringMaxLength> words;
    std::array<uint8_t, MAX_HASH_SIZE * MAX_LANES> hashBuffer;
    auto hashes = cracktools::UnsafeSpan<uint8_t>(hashBuffer.data(), m_HashWidth * SimdLanes());

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
            const size_t length = wordGenerator.Generate(words.GetBufferChar(i), counter++);
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
                auto hash = hashes.subspan(h * hashWidth, hashWidth);
                const size_t length = reducer.Reduce(words.GetBufferChar(h), hash, i);
                words.SetLength(h, length);
            }
        }

        // Save the chain information
        for (size_t h = 0; h < lanes; h++)
        {
            // Get the integer representation of the endpoint
            auto endpointString = words.GetStringView(h);
            auto endpoint = wordGenerator.Parse64Lookup(endpointString);
            block[iteration * lanes + h] = { blockStartId + (iteration * lanes) + h, endpoint };
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
    std::span<const TableRecord> Block
)
{
    // For uncompressed, we can just write the block
    if (m_TableType == TypeUncompressed)
    {
        fwrite(Block.data(), Block.size_bytes(), sizeof(uint8_t), m_WriteHandle);
        fflush(m_WriteHandle);
    }
    // For compressed, we need to reform the data into a single block
    else
    {
        std::vector<TableRecordCompressed> compressedBlock(Block.size());
        std::span<TableRecordCompressed> compressedBlockSpan(compressedBlock);
        for (size_t i = 0; i < Block.size(); i++)
        {
            compressedBlock[i] = Block[i];
        }
        fwrite(compressedBlockSpan.data(), compressedBlockSpan.size_bytes(), sizeof(uint8_t), m_WriteHandle);
        fflush(m_WriteHandle);
    }
    m_ChainsWritten += Block.size();
    return;
}

void
RainbowTable::OutputStatus(
    const std::string_view LastEndpoint
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

    const std::string lastEndpointString(LastEndpoint);

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
    const std::vector<TableRecord> Block,
    const uint64_t Time
)
{
    m_ThreadTimers[ThreadId] = Time;

    // Generate the string for the first endpoint
    auto endpoint = WordGenerator::GenerateWord(Block[0].endpoint, m_Charset);

    OutputStatus(endpoint);

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
    m_Chains = (std::filesystem::file_size(m_Path) - sizeof(TableHeader)) / GetChainWidth();

    size_t dataSize = fileSize - sizeof(TableHeader);
    if (dataSize % GetChainWidth() != 0)
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
    if (m_MappedTableRecords.size() > 0)
    {
        return m_MappedTableRecords.size();
    }
    else if (m_MappedTableRecordsCompressed.size() > 0)
    {
        return m_MappedTableRecordsCompressed.size();
    }
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
    if (upperbound > (size_t)std::numeric_limits<uint64_t>::max())
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
    return cracktools::UnmapFileSpan(m_MappedTable, m_MappedTableFd);
    m_MappedTableRecords = std::span<TableRecord>();
    m_MappedTableRecordsCompressed = std::span<TableRecordCompressed>();
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
    
    auto subspan_data = m_MappedTable.subspan(sizeof(TableHeader));
    if (m_TableType == TypeCompressed)
    {
        
        if (subspan_data.size() % sizeof(TableRecordCompressed) != 0)
        {
            std::cerr << "Invalid or currupt table file. Data not a multiple of chain width" << std::endl;
            return false;
        }
        m_MappedTableRecordsCompressed = cracktools::SpanCast<TableRecordCompressed>(subspan_data);
    }
    else
    {
        if (subspan_data.size() % sizeof(TableRecord) != 0)
        {
            std::cerr << "Invalid or currupt table file. Data not a multiple of chain width" << std::endl;
            return false;
        }
        m_MappedTableRecords = cracktools::SpanCast<TableRecord>(subspan_data);
    }


    return true;
}

const bool
RainbowTable::IndexTable(
    void
)
{
    constexpr size_t INVALID_OFFSET = std::numeric_limits<size_t>::max();
    CHECKA(m_TableType == TypeUncompressed, "Indexing only supported for uncompressed tables");
    m_LookupTable.resize(1 << m_BitmapSize);
    std::vector<size_t> offsets(m_LookupTable.size(), INVALID_OFFSET);

    for (size_t i = 0; i < m_MappedTableRecords.size(); i++)
    {
        auto record = m_MappedTableRecords[i];
        auto endpoint = record.endpoint;
        auto index = endpoint >> (64 - m_BitmapSize);
        if (offsets[index] == INVALID_OFFSET)
        {
            offsets[index] = i;
        }
    }
    // Fill the lookup table with the offsets
    for (size_t i = 0; i < offsets.size() - 1; i++)
    {
        if (offsets[i] == INVALID_OFFSET)
        {
            continue;
        }
        // Find the next non-empty offset
        for (size_t j = i + 1; j < offsets.size(); j++)
        {
            if (offsets[j] != INVALID_OFFSET)
            {
                CHECK(offsets[j] > offsets[i]);
                const size_t Count = offsets[j] - offsets[i];
                m_LookupTable[i] = m_MappedTableRecords.subspan(offsets[i], Count);
                break;
            }
        }
    }
    // Handle the last element
    for (ssize_t i = m_LookupTable.size() - 1; i >= 0; i--)
    {
        if (offsets[i] != INVALID_OFFSET)
        {
            const size_t Count = m_MappedTableRecords.size() - offsets[i];
            m_LookupTable[i] = m_MappedTableRecords.subspan(offsets[i], Count);
            break;
        }
    }
    // Check that the total lengths add up
    size_t total = 0;
    for (size_t i = 0; i < m_LookupTable.size(); i++)
    {
        if (m_LookupTable[i].empty())
        {
            continue;
        }
        total += m_LookupTable[i].size();
    }
    if (total != m_MappedTableRecords.size())
    {
        std::cerr << "Error: bitmask lengths (" << total << ") do not match hash list length (" << m_MappedTableRecords.size() << ")" << std::endl;
        return false;
    }

    m_Indexed = true;
    return true;
}

const TableRecord
RainbowTable::GetRecordAt(
    const size_t Index
) const
{
    if (m_TableType == TypeCompressed)
    {
        TableRecord entry;
        entry.startpoint = Index;
        entry.endpoint = m_MappedTableRecordsCompressed[Index].endpoint;
        return entry;
    }
    else
    {
        return m_MappedTableRecords[Index];
    }
}

const uint64_t
RainbowTable::GetEndpointAt(
    const size_t Index
) const
{
    return GetRecordAt(Index).endpoint;
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

// Searches the table for a given endpoint
// On sucess it returns the startpoint
std::optional<size_t>
RainbowTable::FindStartIndexForEndpoint(
    const uint64_t Endpoint
) const
{
    // Uncompressed tables are just flat files
    // of unsorted endpoints so we need to do a Linear search
    if (m_TableType == TypeCompressed)
    {
        // Use std::find to find the endpoint the endpoint in the m_MappedTableRecordsCompressed span
        auto comparitor = [Endpoint](const TableRecordCompressed& record) {
            return record.endpoint == Endpoint;
        };
        auto it = std::find_if(m_MappedTableRecordsCompressed.begin(), m_MappedTableRecordsCompressed.end(), comparitor);
        if (it != m_MappedTableRecordsCompressed.end())
        {
            return std::distance(m_MappedTableRecordsCompressed.begin(), it);
        }
    }
    // Uncompressed files are flat binary files of TableRecord.
    // Each record has two uint64_t values, the startpoint and the endpoint.
    // They are sorted by endpoint.
    else
    {
        // Perform a binary search based on the endpoint and return the startpoint
        // of a matching record
        auto span = m_Indexed ? m_LookupTable[Endpoint >> (64 - m_BitmapSize)] : m_MappedTableRecords;
        // auto span = m_MappedTableRecords;
        ssize_t low = 0;
        ssize_t high = span.size() - 1;
        while (low <= high)
        {
            ssize_t mid = low + (high - low) / 2;
            if (span[mid].endpoint == Endpoint)
            {
                return span[mid].startpoint;
            }
            else if (span[mid].endpoint < Endpoint)
            {
                low = mid + 1;
            }
            else
            {
                high = mid - 1;
            }
        }
        
    }
    return std::nullopt;
}

// Checks a chain assuming the given iteration
std::optional<std::string>
RainbowTable::CheckIteration(
    const HybridReducer& Reducer,
    std::span<const uint8_t> Target,
    const size_t Iteration
) const
{
    std::array<uint8_t, MAX_HASH_SIZE> hashBuffer;
    auto hash = cracktools::UnsafeSpan<uint8_t>(hashBuffer.data(), m_HashWidth);
    std::array<char, 31> reduced;
    size_t length;

    cracktools::SpanCopy(hash, Target);

    for (size_t j = Iteration; j < m_Length - 1; j++)
    {
        length = Reducer.Reduce(reduced, hash, j);
        DoHash((uint8_t*)&reduced[0], length, &hash[0]);
    }

    // Final reduction
    length = Reducer.Reduce(reduced, hash, m_Length - 1);

    // Convert the endpoint to a uint64_t
    const std::string_view endpointString(&reduced[0], length);
    const uint64_t endpoint = WordGenerator::Parse64(endpointString, m_Charset);

    // Check end, if it matches, we can perform one full chain to see if we find it
    auto index = FindStartIndexForEndpoint(endpoint);
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
    HybridReducer reducer(m_Min, m_Max, m_Charset);

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

    HybridReducer reducer(m_Min, m_Max, m_Charset);
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
        if (!IndexTable())
        {
            std::cerr << "Error indexing table" << std::endl;
            return {};
        }
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
    std::array<uint8_t, MAX_HASH_SIZE> hashBuffer;
    auto hash = cracktools::UnsafeSpan<uint8_t>(hashBuffer.data(), m_HashWidth);
    std::vector<char> reduced(m_Max);
    HybridReducer reducer(m_Min, m_Max, m_Charset);
    size_t length;
#ifdef BIGINT
    mpz_class counter = WordGenerator::WordLengthIndex(m_Min, m_Charset);
#else
    uint64_t counter = WordGenerator::WordLengthIndex64(m_Min, m_Charset);
#endif
    counter += ChainIndex;

    auto start = WordGenerator::GenerateWord(counter, m_Charset);
    length = start.size();
    cracktools::SpanCopy(reduced, start);

    for (size_t i = 0; i < m_Length; i++)
    {
        DoHash((uint8_t*)&reduced[0], length, &hash[0]);
        if (memcmp(Target, &hash[0], m_HashWidth) == 0)
        {
            return std::string(&reduced[0], &reduced[length]);
        }
        length = reducer.Reduce(reduced, hash, i);
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
        // Sort uncompressed tables by endpoint
        std::sort(m_MappedTableRecords.begin(), m_MappedTableRecords.end(),
                    [](const TableRecord& a, const TableRecord& b) {
                        return a.endpoint < b.endpoint;
        });
    }
    else
    {
        std::cerr << "Cannot sort compressed tables" << std::endl;
        return;
    }
}

void
RainbowTable::ChangeType(
    const std::filesystem::path& Destination,
    const TableType DestinationType
)
{
    if (m_TableType == DestinationType)
    {
        std::cerr << "Won't convert to same type" << std::endl;
        return;
    }

    // Output some basic information about the current table
    std::cout << "Table type: " << GetType() << std::endl;
    std::cout << "Exporting " << m_Chains << " chains" << std::endl;

    // Map the current table to memory
    if (!MapTable(true))
    {
        std::cerr << "Error mapping table"  << std::endl;
        return;
    }

    TableHeader hdr;
    hdr.magic = kMagic;
    hdr.algorithm = m_Algorithm;
    hdr.min = m_Min;
    hdr.max = m_Max;
    hdr.length = m_Length;
    hdr.charsetlen = m_Charset.size();
    strncpy(hdr.charset, &m_Charset[0], sizeof(hdr.charset));
    hdr.type = DestinationType;

    FILE* fhw = fopen(Destination.c_str(), "w");
    if (fhw == nullptr)
    {
        std::cerr << "Error opening desination table for write: " << Destination << std::endl;
        return;
    }

    // Write the header
    fwrite(&hdr, sizeof(hdr), 1, fhw);

    if (DestinationType == TypeCompressed)
    {
        // Copy the existing table rows to a new vector then sort by start point
        std::vector<TableRecord> compressedTable(GetCount());
        std::copy(m_MappedTableRecords.begin(), m_MappedTableRecords.end(), compressedTable.begin());
        std::sort(compressedTable.begin(), compressedTable.end(),
                    [](const TableRecord& a, const TableRecord& b) {
                        return a.startpoint < b.startpoint;
        });
        TableRecordCompressed compressedRecord;
        for (const auto& record : compressedTable)
        {
            compressedRecord = record;
            fwrite(&compressedRecord, sizeof(TableRecordCompressed), 1, fhw);
        }
        fclose(fhw);
    }
    else
    {
        for (size_t i = 0; i < GetCount(); i++)
        {
            // Read the compressed record
            TableRecord record;
            record.startpoint = i;
            record.endpoint = m_MappedTableRecordsCompressed[i].endpoint;
            fwrite(&record, sizeof(TableRecord), 1, fhw);
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
}

/* static */ const Chain
RainbowTable::GetChain(
    const std::filesystem::path& Path,
    const size_t Index
)
{
    /*TableHeader hdr;
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

    return chain;*/
    return Chain();
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

    HybridReducer reducer(Min, Max, Charset);

    std::vector<uint8_t> hash(hashLength);
    std::vector<char> reduced(Max);
    size_t reducedLength = start.size();

    cracktools::SpanCopy(reduced, start);

    for (size_t i = 0; i < Length; i++)
    {
        DoHash((uint8_t*)&reduced[0], reducedLength, &hash[0], Algorithm);
        reducedLength = reducer.Reduce(reduced, hash, i);
    }

    std::span<char> reducedSpan(reduced);
    chain.SetEnd(reducedSpan.subspan(0, reducedLength));
    return chain;
}