//
//  RainbowTable.cpp
//  SimdRainbowCrack
//
//  Created by Kryc on 15/02/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <atomic>
#include <charconv>
#include <cinttypes>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <format>
#include <iomanip>
#include <iostream>
#include <latch>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>
#include <openssl/md5.h>
#include <openssl/sha.h>

namespace {
std::atomic<bool>* g_BuildComplete = nullptr;
std::atomic<bool> g_BuildInterrupted{false};

void HandleSigInt(int)
{
    g_BuildInterrupted.store(true, std::memory_order_relaxed);
    if (g_BuildComplete)
        g_BuildComplete->store(true, std::memory_order_relaxed);
}
}

#include "SimdHashBuffer.hpp"

#include "Chain.hpp"
#include "RainbowTable.hpp"
#include "SmallString.hpp"
#include "Util.hpp"

/* static */ const size_t
RainbowTable::ChainWidth(
    const uint8_t IndexWidth
)
{ 
    return IndexWidth * 2;
}

bool
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
        return false;
    }

    m_Operation = "Building";

    // Compute index width if not already loaded from an existing table
    if (!m_PathLoaded)
    {
        m_IndexWidth = ComputeIndexWidth(m_Min, m_Max, m_Charset);
    }

    // Calculate the count if needed
    if (m_Count == 0)
    {
        __uint128_t keyspace = WordGenerator::WordLengthIndex128(m_Max + 1, m_Charset) - WordGenerator::WordLengthIndex128(m_Min, m_Charset);
        // m = N * -ln(1 - coverage) / t
        double factor = -std::log(1.0 - m_Coverage);
        double chains = static_cast<double>(keyspace) * factor / m_Length;
        std::cerr << "Target coverage: " << std::fixed << std::setprecision(0) << (m_Coverage * 100) << "%" << std::endl;
        std::cerr << "Calculated chains required: " << static_cast<size_t>(chains) << std::endl;
        m_Count = static_cast<size_t>(chains);
    }

    // Estimate table size
    double tableSize = sizeof(TableHeader) + (m_Count * ChainWidth(m_IndexWidth));
    std::string tableSizeCh;
    tableSize = Util::SizeFactor(tableSize, tableSizeCh);
    std::cerr << "Estimated table size: " << std::fixed << std::setprecision(2) << tableSize << ' ' << tableSizeCh << std::endl;

    // Write the table header if we didn't load from disk
    if (!m_PathLoaded)
    {
        m_HashWidth = GetHashWidth(m_Algorithm);
    }

    // Detect existing build state (interrupted segments or completed base table)
    // and set m_StartingChains, m_Chains accordingly
    OpenExistingBuildState();

    // Pre-reserve the pending vector and create the bloom filter sized for
    // a full flush window so we don't pay rehash/realloc cost during the hot
    // dedup path.
    m_PendingChains.reserve(m_FlushThresholdChains);
    m_PendingFilter = std::make_unique<BloomFilter>(m_FlushThresholdChains);

    // Install SIGINT handler for graceful shutdown
    g_BuildComplete = &m_BuildComplete;
    struct sigaction sa = {}, oldsa = {};
    sa.sa_handler = HandleSigInt;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &oldsa);

    if (m_Threads == 1)
    {
        // Single-threaded: generate and save blocks directly
        for (size_t blockId = 0; ; blockId++)
        {
            if (m_BuildComplete.load(std::memory_order_relaxed))
            {
                break;
            }

            size_t blockStartId = m_StartingChains + (m_Blocksize * blockId);
            auto [block, elapsed_ms] = GenerateBlockData(blockStartId);
            SaveBlock(0, blockId, std::move(block), elapsed_ms);
        }
    }
    else
    {
        // Multi-threaded: use dispatch pool
        auto mainDispatcher = dispatch::CreateDispatcher(
            "main",
            dispatch::DoNothing
        );

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

        // Wait on the main thread
        mainDispatcher->Wait();
    }

    // Restore original signal handler
    sigaction(SIGINT, &oldsa, nullptr);
    g_BuildComplete = nullptr;

    // Final flush of any remaining pending chains
    if (!m_PendingChains.empty())
    {
        FlushPending();
    }

    // Report dedup stats
    if (m_ChainsGenerated > 0)
    {
        size_t discarded = m_ChainsGenerated - m_ChainsWritten;
        std::cerr << std::endl;
        std::cerr << "Generated: " << m_ChainsGenerated
                  << ", Written: " << m_ChainsWritten
                  << ", Discarded: " << discarded
                  << " (" << std::fixed << std::setprecision(1)
                  << (100.0 * discarded / m_ChainsGenerated) << "%)" << std::endl;
    }

    // Merge all segments into the final sorted table file
    std::cerr << "Merging segments..." << std::endl;
    MergeSegmentsIntoFinal();
    CleanupSegments();

    std::cout << std::endl;
    return !g_BuildInterrupted.load(std::memory_order_relaxed);
}

std::tuple<std::vector<InternalRecord>, uint64_t>
RainbowTable::GenerateBlockData(
    const size_t BlockStartId
)
{
    WordGenerator wordGenerator(m_Charset);
    wordGenerator.GenerateParsingLookupTable();
    HybridReducer reducer(m_Min, m_Max, m_Charset, m_Seed);
    std::vector<InternalRecord> block(m_Blocksize);

    SimdHashBufferFixed<kSmallStringMaxLength> words;
    std::array<uint8_t, MAX_HASH_SIZE * MAX_LANES> hashBuffer;
    auto hashes = cracktools::UnsafeSpan<uint8_t>(hashBuffer.data(), m_HashWidth * SimdLanes());

    // Calculate lower bound and add the current index
    __uint128_t counter = CalculateLowerBound() + BlockStartId;
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
            auto endpoint = WordGenerator::Parse128(endpointString, m_Charset);
            block[iteration * lanes + h] = { static_cast<__uint128_t>(BlockStartId + (iteration * lanes) + h), endpoint };
        }
    }

    const auto end = std::chrono::system_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    return { std::move(block), static_cast<uint64_t>(elapsed_ms.count()) };
}

void
RainbowTable::GenerateBlock(
    const size_t ThreadId,
    const size_t BlockId
)
{
    // Check if we've written enough unique chains or saturated
    if (m_BuildComplete.load(std::memory_order_relaxed))
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

    size_t blockStartId = m_StartingChains + (m_Blocksize * BlockId);
    auto [block, elapsed_ms] = GenerateBlockData(blockStartId);

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
            elapsed_ms
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
RainbowTable::OpenExistingBuildState(
    void
)
{
    // Discover existing segment files matching <m_Path>.seg.<N>
    std::vector<std::pair<size_t, std::filesystem::path>> segPaths;
    auto dir = m_Path.parent_path();
    if (dir.empty())
    {
        dir = ".";
    }
    const std::string prefix = m_Path.filename().string() + ".seg.";
    if (std::filesystem::exists(dir))
    {
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;
            const auto& name = entry.path().filename().string();
            if (!name.starts_with(prefix))
                continue;
            std::string suffix = name.substr(prefix.size());
            // Skip in-progress writes
            if (suffix.ends_with(".tmp"))
            {
                std::error_code ec;
                std::filesystem::remove(entry.path(), ec);
                continue;
            }
            // Parse trailing decimal number
            size_t id = 0;
#pragma clang unsafe_buffer_usage begin
            const char* suffixEnd = suffix.data() + suffix.size();
            auto parseRes = std::from_chars(
                suffix.data(),
                suffixEnd,
                id);
            if (parseRes.ec != std::errc() || parseRes.ptr != suffixEnd)
                continue;
#pragma clang unsafe_buffer_usage end
            segPaths.emplace_back(id, entry.path());
        }
    }

    std::sort(segPaths.begin(), segPaths.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const size_t recordWidth = ChainWidth(m_IndexWidth);

    for (auto& [id, path] : segPaths)
    {
        BuildSegment seg;
        seg.path = path;
        auto mapping = cracktools::MmapFileSpan<uint8_t>(path, PROT_READ, MAP_PRIVATE, /*madvise*/ true);
        if (!mapping.has_value())
        {
            std::cerr << "Warning: failed to map segment " << path << std::endl;
            continue;
        }
        auto [mapped, fp] = mapping.value();
        // Close fd immediately; mmap keeps its own reference
        fclose(fp);
        seg.mapped = mapped;
        seg.fp = nullptr;
        seg.headerOffset = 0;
        seg.recordCount = mapped.size() / recordWidth;
        m_Segments.push_back(std::move(seg));
        if (id + 1 > m_NextSegmentId)
            m_NextSegmentId = id + 1;
    }

    // If a completed table file exists at m_Path, treat its records as a base segment.
    // (Only valid if it was produced by a completed merge — i.e. sorted by endpoint.)
    if (std::filesystem::exists(m_Path) && IsTableFile(m_Path))
    {
        size_t fsz = std::filesystem::file_size(m_Path);
        if (fsz > sizeof(TableHeader))
        {
            BuildSegment seg;
            seg.path = m_Path;
            auto mapping = cracktools::MmapFileSpan<uint8_t>(m_Path, PROT_READ, MAP_PRIVATE, /*madvise*/ true);
            if (mapping.has_value())
            {
                auto [mapped, fp] = mapping.value();
                fclose(fp);
                seg.mapped = mapped;
                seg.fp = nullptr;
                seg.headerOffset = sizeof(TableHeader);
                seg.recordCount = (mapped.size() - sizeof(TableHeader)) / recordWidth;
                m_Segments.push_back(std::move(seg));
            }
        }
    }

    if (m_Segments.empty())
    {
        m_StartingChains = 0;
        m_Chains = 0;
        return;
    }

    // Find max startpoint and total existing chain count
    size_t maxStart = 0;
    size_t totalChains = 0;
    DispatchByWidth(m_IndexWidth, [&]<typename IndexT>()
    {
        for (const auto& seg : m_Segments)
        {
            auto data = seg.mapped.subspan(seg.headerOffset);
            auto records = cracktools::SpanCast<TableRecord<IndexT>>(data);
            for (const auto& r : records)
            {
                if (static_cast<size_t>(r.startpoint) > maxStart)
                    maxStart = static_cast<size_t>(r.startpoint);
            }
            totalChains += records.size();
        }
    });

    m_Chains = totalChains;
    m_StartingChains = maxStart + 1;
    std::cerr << "Resuming: " << totalChains << " existing chains in "
              << m_Segments.size() << " segment(s), continuing from index "
              << m_StartingChains << std::endl;
}

void
RainbowTable::FlushPending(
    void
)
{
    if (m_PendingChains.empty())
        return;

    // Sort the pending buffer by endpoint
    std::sort(m_PendingChains.begin(), m_PendingChains.end(),
              [](const InternalRecord& a, const InternalRecord& b)
              {
                  return a.endpoint < b.endpoint;
              });

    auto tmpPath = m_Path;
    tmpPath += ".seg." + std::to_string(m_NextSegmentId) + ".tmp";
    auto finalPath = m_Path;
    finalPath += ".seg." + std::to_string(m_NextSegmentId);

    FILE* fp = fopen(tmpPath.c_str(), "wb");
    if (fp == nullptr)
    {
        std::cerr << "Error: failed to open segment file " << tmpPath << " for write" << std::endl;
        return;
    }

    DispatchByWidth(m_IndexWidth, [&]<typename IndexT>()
    {
        std::vector<TableRecord<IndexT>> narrowed;
        narrowed.reserve(m_PendingChains.size());
        for (const auto& r : m_PendingChains)
        {
            narrowed.push_back({
                static_cast<IndexT>(r.startpoint),
                static_cast<IndexT>(r.endpoint)
            });
        }
#pragma clang unsafe_buffer_usage begin
        fwrite(narrowed.data(), sizeof(TableRecord<IndexT>), narrowed.size(), fp);
#pragma clang unsafe_buffer_usage end
    });

    fflush(fp);
    fclose(fp);

    std::error_code ec;
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec)
    {
        std::cerr << "Error: failed to rename segment: " << ec.message() << std::endl;
        return;
    }

    BuildSegment seg;
    seg.path = finalPath;
    auto mapping = cracktools::MmapFileSpan<uint8_t>(finalPath, PROT_READ, MAP_PRIVATE, /*madvise*/ true);
    if (mapping.has_value())
    {
        auto [mapped, fph] = mapping.value();
        fclose(fph);
        seg.mapped = mapped;
        seg.fp = nullptr;
        seg.headerOffset = 0;
        seg.recordCount = m_PendingChains.size();
    }
    else
    {
        std::cerr << "Warning: failed to map newly written segment " << finalPath << std::endl;
    }

    m_ChainsWritten += m_PendingChains.size();
    m_PendingChains.clear();
    m_PendingChains.reserve(m_FlushThresholdChains);
    m_PendingFilter->Clear();
    m_NextSegmentId++;
    m_Segments.push_back(std::move(seg));
}

void
RainbowTable::MergeSegmentsIntoFinal(
    void
)
{
    auto tmpPath = m_Path;
    tmpPath += ".tmp";

    FILE* fp = fopen(tmpPath.c_str(), "wb");
    if (fp == nullptr)
    {
        std::cerr << "Error: failed to open output " << tmpPath << " for merge" << std::endl;
        return;
    }

    // Write header
    TableHeader hdr = {};
    hdr.magic = kMagic;
    hdr.algorithm = static_cast<uint8_t>(m_Algorithm);
    hdr.min = static_cast<uint8_t>(m_Min);
    hdr.max = static_cast<uint8_t>(m_Max);
    hdr.charsetlen = static_cast<uint8_t>(m_Charset.size());
    hdr.length = m_Length;
    hdr.seed = m_Seed;
#pragma clang unsafe_buffer_usage begin
    std::memset(hdr.charset, 0, sizeof(hdr.charset));
    std::memcpy(hdr.charset, m_Charset.data(), m_Charset.size());
    fwrite(&hdr, sizeof(hdr), 1, fp);
#pragma clang unsafe_buffer_usage end

    size_t finalCount = 0;

    DispatchByWidth(m_IndexWidth, [&]<typename IndexT>()
    {
        struct Entry
        {
            IndexT endpoint;
            size_t segIdx;
            size_t recordIdx;
            bool operator>(const Entry& o) const { return endpoint > o.endpoint; }
        };

        std::vector<std::span<const TableRecord<IndexT>>> segRecords;
        segRecords.reserve(m_Segments.size());
        for (const auto& seg : m_Segments)
        {
            auto data = seg.mapped.subspan(seg.headerOffset);
            segRecords.push_back(cracktools::SpanCast<const TableRecord<IndexT>>(data));
        }

        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> pq;
        for (size_t i = 0; i < segRecords.size(); i++)
        {
            if (!segRecords[i].empty())
            {
                pq.push({segRecords[i][0].endpoint, i, 0});
            }
        }

        IndexT lastEndpoint = 0;
        bool first = true;

        while (!pq.empty())
        {
            auto top = pq.top();
            pq.pop();
            const auto& r = segRecords[top.segIdx][top.recordIdx];

            if (first || r.endpoint != lastEndpoint)
            {
                fwrite(&r, sizeof(TableRecord<IndexT>), 1, fp);
                lastEndpoint = r.endpoint;
                first = false;
                finalCount++;
            }

            size_t next = top.recordIdx + 1;
            if (next < segRecords[top.segIdx].size())
            {
                pq.push({segRecords[top.segIdx][next].endpoint, top.segIdx, next});
            }
        }
    });

    fflush(fp);
    fclose(fp);

    // Unmap segments before rename so the OS lets us replace them
    for (auto& seg : m_Segments)
    {
        if (!seg.mapped.empty())
        {
            cracktools::UnmapFileSpan(seg.mapped, seg.fp);
            seg.mapped = {};
            seg.fp = nullptr;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmpPath, m_Path, ec);
    if (ec)
    {
        std::cerr << "Error: failed to rename merged file to " << m_Path
                  << ": " << ec.message() << std::endl;
        return;
    }

    std::cerr << "Merged " << finalCount << " unique chains into " << m_Path.filename().string() << std::endl;
}

void
RainbowTable::CleanupSegments(
    void
)
{
    for (const auto& seg : m_Segments)
    {
        if (seg.path == m_Path)
            continue;
        std::error_code ec;
        std::filesystem::remove(seg.path, ec);
    }
    m_Segments.clear();
}

void
RainbowTable::OutputStatus(
    const std::string_view LastEndpoint
) const
{
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

    double chains = (double)(m_Chains + m_ChainsWritten + m_PendingChains.size());
    std::string chainsChar;
    chains = Util::NumFactor(chains, chainsChar);

    double percent = ((double)(m_Chains + m_ChainsWritten + m_PendingChains.size()) / (double)m_Count) * 100.f;

    double discardPct = m_ChainsGenerated > 0 ?
        100.0 * (m_ChainsGenerated - m_ChainsWritten) / m_ChainsGenerated : 0.0;

    const std::string lastEndpointString(LastEndpoint);

    std::string status = std::format(
        "C:{:.1f}{}({:.1f}%) C/s:{:.1f}{} H/s:{:.1f}{} D:{:.1f}% E:\"{}\"",
        chains,
        chainsChar,
        percent,
        chainsPerSec,
        cpsChar,
        hashesPerSec,
        hpsChar,
        discardPct,
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
    std::vector<InternalRecord> Block,
    const uint64_t Time
)
{
    (void)BlockId;
    m_ThreadTimers[ThreadId] = Time;

    m_ChainsGenerated += Block.size();

    // Dedup: against on-disk segments (binary search) and in-memory pending (hash).
    DispatchByWidth(m_IndexWidth, [&]<typename IndexT>()
    {
        std::vector<std::span<const TableRecord<IndexT>>> segRecords;
        segRecords.reserve(m_Segments.size());
        for (const auto& seg : m_Segments)
        {
            auto data = seg.mapped.subspan(seg.headerOffset);
            segRecords.push_back(cracktools::SpanCast<const TableRecord<IndexT>>(data));
        }

        std::erase_if(Block, [&](const InternalRecord& record)
        {
            const IndexT narrow = static_cast<IndexT>(record.endpoint);
            // Check on-disk segments (sorted) via binary search
            for (const auto& records : segRecords)
            {
                auto it = std::lower_bound(
                    records.begin(), records.end(), narrow,
                    [](const TableRecord<IndexT>& r, IndexT v)
                    {
                        return r.endpoint < v;
                    });
                if (it != records.end() && it->endpoint == narrow)
                    return true;
            }
            // Check + insert into in-memory pending bloom filter.
            // Insert returns true if the bits were not all set (probably new).
            return !m_PendingFilter->Insert(record.endpoint);
        });
    });

    if (Block.empty())
    {
        m_ConsecutiveEmptyBlocks++;
        size_t maxEmpty = std::max(size_t(10), m_Count / m_Blocksize);
        if (m_ConsecutiveEmptyBlocks >= maxEmpty)
        {
            std::cerr << std::endl << "Endpoint space saturated, stopping build" << std::endl;
            m_BuildComplete.store(true, std::memory_order_relaxed);
        }
        return;
    }

    m_ConsecutiveEmptyBlocks = 0;

    // Truncate to avoid overshooting the target count
    size_t totalSoFar = m_Chains + m_ChainsWritten + m_PendingChains.size();
    size_t remaining = m_Count > totalSoFar ? m_Count - totalSoFar : 0;
    if (Block.size() > remaining)
    {
        // Bloom filter has no erase; the dropped chains' bits remain set,
        // marginally raising FP rate within this flush window. Acceptable.
        Block.resize(remaining);
    }

    if (Block.empty())
    {
        m_BuildComplete.store(true, std::memory_order_relaxed);
        return;
    }

    // Display progress using last accepted endpoint
    auto endpoint = WordGenerator::GenerateWord(Block.back().endpoint, m_Charset);
    OutputStatus(endpoint);

    // Append survivors to pending buffer
    m_PendingChains.insert(
        m_PendingChains.end(),
        std::make_move_iterator(Block.begin()),
        std::make_move_iterator(Block.end()));

    // Reached target? signal completion
    if (m_Chains + m_ChainsWritten + m_PendingChains.size() >= m_Count)
    {
        m_BuildComplete.store(true, std::memory_order_relaxed);
    }

    // Flush if pending buffer hits threshold
    if (m_PendingChains.size() >= m_FlushThresholdChains)
    {
        FlushPending();
    }
}

float
RainbowTable::GetCoverageEstimate(
    void
)
{
    __uint128_t lowerbound = WordGenerator::WordLengthIndex128(m_Min, m_Charset);
    __uint128_t upperbound = WordGenerator::WordLengthIndex128(m_Max + 1, m_Charset);
    double N = static_cast<double>(upperbound - lowerbound);

    double mt = static_cast<double>(m_Chains) * m_Length;
    // 1 - e^(-mt/N)
    double coverage = (1.0 - std::exp(-mt / N)) * 100.0;

    return static_cast<float>(coverage);
}

void
RainbowTable::StoreTableHeader(
    void
) const
{
    TableHeader hdr;
#pragma clang unsafe_buffer_usage begin
    memset(&hdr, 0, sizeof(hdr));
#pragma clang unsafe_buffer_usage end
    hdr.magic = kMagic;
    hdr.algorithm = m_Algorithm;
    hdr.min = m_Min;
    hdr.max = m_Max;
    hdr.length = m_Length;
    hdr.seed = m_Seed;
    hdr.charsetlen = m_Charset.size();
#pragma clang unsafe_buffer_usage begin
    strncpy(hdr.charset, &m_Charset[0], sizeof(hdr.charset));
#pragma clang unsafe_buffer_usage end

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

    m_Algorithm = (HashAlgorithm)hdr.algorithm;
    m_Min = hdr.min;
    m_Max = hdr.max;
    m_Length = hdr.length;
    m_Seed = hdr.seed;
    std::string_view charset(hdr.charset, hdr.charsetlen);
    m_Charset = charset;
    m_IndexWidth = ComputeIndexWidth(m_Min, m_Max, m_Charset);
    m_HashWidth = GetHashWidth(m_Algorithm);

    size_t dataSize = fileSize - sizeof(TableHeader);
    size_t chainWidth = GetChainWidth();
    size_t remainder = dataSize % chainWidth;
    if (remainder != 0)
    {
        std::cerr << "Warning: truncating " << remainder << " bytes of partial chain data" << std::endl;
        std::filesystem::resize_file(m_Path, fileSize - remainder);
        dataSize -= remainder;
    }
    m_Chains = dataSize / chainWidth;

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

    mpz_class upperbound = WordGenerator::WordLengthIndex(m_Max + 1, m_Charset);
    mpz_class uint128max = mpz_class(1) << 128;
    uint128max -= 1;
    if (upperbound > uint128max)
    {
        std::cerr << "Max length exceeds 128-bit integer limit" << std::endl;
        return false;
    }

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
    bool result = cracktools::UnmapFileSpan(m_MappedTable, m_MappedTableFd);
    return result;
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
    size_t chainWidth = GetChainWidth();
    if (subspan_data.size() % chainWidth != 0)
    {
        std::cerr << "Invalid or corrupt table file. Data not a multiple of chain width" << std::endl;
        return false;
    }

    return true;
}

const InternalRecord
RainbowTable::GetRecordAt(
    const size_t Index
) const
{
    return DispatchByWidth(m_IndexWidth, [&]<typename IndexT>() -> InternalRecord
    {
        auto data = m_MappedTable.subspan(sizeof(TableHeader));
        auto records = cracktools::SpanCast<TableRecord<IndexT>>(data);
        return InternalRecord{ static_cast<__uint128_t>(records[Index].startpoint), static_cast<__uint128_t>(records[Index].endpoint) };
    });
}

const __uint128_t
RainbowTable::GetEndpointAt(
    const size_t Index
) const
{
    return GetRecordAt(Index).endpoint;
}

size_t
RainbowTable::CountUniqueEndpoints(void)
{
    if (!MapTable(true))
        return 0;

    return DispatchByWidth(m_IndexWidth, [&]<typename IndexT>() -> size_t
    {
        auto data = m_MappedTable.subspan(sizeof(TableHeader));
        auto records = cracktools::SpanCast<TableRecord<IndexT>>(data);

        struct Hash128 {
            size_t operator()(const __uint128_t& v) const noexcept {
                auto lo = static_cast<uint64_t>(v);
                auto hi = static_cast<uint64_t>(v >> 64);
                return std::hash<uint64_t>{}(lo) ^ (std::hash<uint64_t>{}(hi) * 0x9e3779b97f4a7c15ULL);
            }
        };
        using Set = std::conditional_t<
            std::is_same_v<IndexT, __uint128_t>,
            std::unordered_set<IndexT, Hash128>,
            std::unordered_set<IndexT>
        >;

        Set seen;
        seen.reserve(records.size());
        for (const auto& r : records)
            seen.insert(r.endpoint);
        return seen.size();
    });
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
    const __uint128_t Endpoint
) const
{
    return DispatchByWidth(m_IndexWidth, [&]<typename IndexT>() -> std::optional<size_t>
    {
        auto data = m_MappedTable.subspan(sizeof(TableHeader));
        const IndexT narrowEndpoint = static_cast<IndexT>(Endpoint);

        // Table is sorted by endpoint — binary search
        auto records = cracktools::SpanCast<TableRecord<IndexT>>(data);
        ssize_t low = 0;
        ssize_t high = records.size() - 1;
        while (low <= high)
        {
            ssize_t mid = low + (high - low) / 2;
            if (records[mid].endpoint == narrowEndpoint)
            {
                return static_cast<size_t>(records[mid].startpoint);
            }
            else if (records[mid].endpoint < narrowEndpoint)
            {
                low = mid + 1;
            }
            else
            {
                high = mid - 1;
            }
        }
        return std::nullopt;
    });
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

    // Convert the endpoint to a __uint128_t
    const std::string_view endpointString(&reduced[0], length);
    const __uint128_t endpoint = WordGenerator::Parse128(endpointString, m_Charset);

    // Check end, if it matches, we can perform one full chain to see if we find it
    auto index = FindStartIndexForEndpoint(endpoint);
    if (index.has_value())
    {
        return ValidateChain(index.value(), Target);
    }
    return std::nullopt;
}

void
RainbowTable::CrackOneWorker(
    const size_t ThreadId,
    const std::vector<uint8_t> Target,
    std::latch& Done
)
{
    HybridReducer reducer(m_Min, m_Max, m_Charset, m_Seed);

    for (ssize_t i = m_Length - 1 - ThreadId; i >= 0 && !m_Cracked; i -= m_Threads)
    {
        auto result = CheckIteration(reducer, Target, i);
        bool cracked = m_Cracked;
        if (result.has_value() && !cracked && m_Cracked.compare_exchange_strong(cracked, true))
        {
            m_LastCracked = std::make_tuple(Util::ToHex(&Target[0], Target.size()), result.value());
            m_CrackedResults.push_back(m_LastCracked);
        }
    }

    Done.count_down();
}

std::optional<std::string>
RainbowTable::CrackOne(
    const std::string_view Hash
)
{
    if (Hash.size() != m_HashWidth * 2)
    {
        std::cerr << "Invalid length of provided hash: " << Hash.size() << " != " << m_HashWidth * 2 << std::endl;
        std::cerr << "Hash: '" << Hash << "'" << std::endl;
        return std::nullopt;
    }

    HybridReducer reducer(m_Min, m_Max, m_Charset, m_Seed);
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
                m_LastCracked = std::make_tuple(Util::ToHex(&target[0], target.size()), result.value());
                m_CrackedResults.push_back(m_LastCracked);
                break;
            }
        }
    }
    else
    {
        std::latch done(m_Threads);

        // Dispatch the work to the worker threads
        for (size_t i = 0; i < m_Threads; i++)
        {
            m_DispatchPool->PostTask(
                dispatch::bind(
                    &RainbowTable::CrackOneWorker,
                    this,
                    i,
                    target,
                    std::ref(done)
                )
            );
        }

        // Wait for all threads to finish
        done.wait();

        // Check if we found the result
        if (m_Cracked)
        {
            result = std::get<1>(m_LastCracked);
        }
    }

    return result;
}

bool
RainbowTable::PrepareCrack(
    void
)
{
    if (!MapTable(true))
    {
        std::cerr << "Error mapping the table" << std::endl;
        return false;
    }

    m_Operation = "Cracking";

    if (m_Threads == 0)
    {
        m_Threads = std::thread::hardware_concurrency();
    }

    if (m_Threads > 1)
    {
        m_DispatchPool = dispatch::CreateDispatchPool("pool", m_Threads);
    }

    return true;
}

void
RainbowTable::FinishCrack(
    void
)
{
    if (m_DispatchPool != nullptr)
    {
        m_DispatchPool->Stop();
        m_DispatchPool->Wait();
        m_DispatchPool = nullptr;
    }
    UnmapTable();
}

std::vector<std::tuple<std::string, std::string>>
RainbowTable::Crack(
    const std::string_view Target
)
{
    if (!PrepareCrack())
    {
        return {};
    }

    // Check the argument
    if (!Util::IsHex(Target) && !std::filesystem::exists(Target))
    {
        std::cerr << "Invalid target hash or file" << std::endl;
        return {};
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
        m_HashFileStream = std::ifstream(std::string(Target));

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

    FinishCrack();

    return std::move(m_CrackedResults);
}

std::optional<std::string>
RainbowTable::ValidateChain(
    const size_t ChainIndex,
    const std::span<const uint8_t> Target
) const
{
    std::array<uint8_t, MAX_HASH_SIZE> hashBuffer;
    std::span<uint8_t> hashspan = hashBuffer;
    auto hash = hashspan.subspan(0, m_HashWidth);
    std::vector<char> reduced(m_Max);
    HybridReducer reducer(m_Min, m_Max, m_Charset, m_Seed);
    size_t length;
    __uint128_t counter = WordGenerator::WordLengthIndex128(m_Min, m_Charset);
    counter += ChainIndex;

    auto start = WordGenerator::GenerateWord(counter, m_Charset);
    length = start.size();
    cracktools::SpanCopy(reduced, start);

    for (size_t i = 0; i < m_Length; i++)
    {
        DoHash((uint8_t*)&reduced[0], length, &hash[0]);
        if (std::equal(hash.begin(), hash.end(), Target.begin(), Target.end()))
        {
            return std::string(reduced.data(), length);
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
    m_Seed = 0;
    m_Blocksize = 1024;
    m_Count = 0;
    m_Threads = 0;
    m_Charset.clear();
    m_HashWidth = 0;
    m_Chains = 0;
    m_IndexWidth = 16;
    // For building
    m_StartingChains = 0;
    m_PendingChains.clear();
    m_PendingFilter.reset();
    for (auto& seg : m_Segments)
    {
        if (!seg.mapped.empty())
        {
            cracktools::UnmapFileSpan(seg.mapped, seg.fp);
            seg.mapped = {};
            seg.fp = nullptr;
        }
    }
    m_Segments.clear();
    m_NextSegmentId = 0;
    m_ChainsWritten = 0;
    m_ChainsGenerated = 0;
    m_ConsecutiveEmptyBlocks = 0;
    m_BuildComplete.store(false, std::memory_order_relaxed);
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

    DispatchByWidth(m_IndexWidth, [&]<typename IndexT>()
    {
        auto data = m_MappedTable.subspan(sizeof(TableHeader));
        auto records = cracktools::SpanCast<TableRecord<IndexT>>(data);
        std::sort(records.begin(), records.end(),
                    [](const TableRecord<IndexT>& a, const TableRecord<IndexT>& b) {
                        return a.endpoint < b.endpoint;
        });
    });
}

/* static */ const Chain
RainbowTable::GetChain(
    const std::filesystem::path& Path,
    const size_t Index
)
{
    TableHeader hdr;
    if (!GetTableHeader(Path, &hdr))
    {
        return Chain();
    }

    std::string charset(hdr.charset, hdr.charsetlen);
    uint8_t indexWidth = ComputeIndexWidth(hdr.min, hdr.max, charset);
    size_t chainWidth = ChainWidth(indexWidth);

    // Verify the index is within bounds
    size_t fileSize = std::filesystem::file_size(Path);
    size_t dataSize = fileSize - sizeof(TableHeader);
    size_t chainCount = dataSize / chainWidth;
    if (Index >= chainCount)
    {
        return Chain();
    }

    // Read the record at the given index
    std::ifstream fs(Path, std::ios::binary);
    if (!fs.is_open())
    {
        return Chain();
    }
    fs.seekg(sizeof(TableHeader) + chainWidth * Index);

    __uint128_t startpoint;
    __uint128_t endpoint;

    DispatchByWidth(indexWidth, [&]<typename IndexT>()
    {
        TableRecord<IndexT> record;
        fs.read(reinterpret_cast<char*>(&record), sizeof(record));
        startpoint = static_cast<__uint128_t>(record.startpoint);
        endpoint = static_cast<__uint128_t>(record.endpoint);
    });
    fs.close();

    __uint128_t lowerbound = WordGenerator::WordLengthIndex128(hdr.min, charset);

    Chain chain;
    chain.SetIndex(startpoint);
    chain.SetLength(hdr.length);
    chain.SetStart(WordGenerator::GenerateWord(lowerbound + startpoint, charset));
    chain.SetEnd(WordGenerator::GenerateWord(endpoint, charset));

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
    __uint128_t counter;
    Chain chain;
    size_t hashLength;
    std::string start;

    hashLength = GetHashWidth(Algorithm);

    chain.SetIndex(Index);
    chain.SetLength(Length);

    counter = WordGenerator::WordLengthIndex128(Min, Charset);
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