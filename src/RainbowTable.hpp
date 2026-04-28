//
//  RainbowTable.hpp
//  SimdCrack
//
//  Created by Kryc on 15/02/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef RainbowTable_hpp
#define RainbowTable_hpp


#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <latch>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "DispatchQueue.hpp"
#include "simdhash.h"

#include "BloomFilter.hpp"
#include "Chain.hpp"
#include "HashList.hpp"
#include "Reduce.hpp"
#include "SmallString.hpp"
#include "Util.hpp"
#include "WordGenerator.hpp"

constexpr uint32_t kMagic = 'rt- ';

typedef struct  __attribute__((__packed__)) _TableHeader
{
    uint32_t magic;
    uint8_t  algorithm;
    uint8_t  min;
    uint8_t  max;
    uint8_t  charsetlen;
    uint64_t length;
    uint32_t seed;
    char     charset[128];
} TableHeader;

template<typename IndexT>
struct TableRecord
{
    bool operator<(const TableRecord& other) const
    {
        return endpoint < other.endpoint;
    }
    IndexT startpoint;
    IndexT endpoint;
};

// Internal record types always use __uint128_t
using InternalRecord = TableRecord<__uint128_t>;

// Determine the narrowest index width that fits the keyspace
static inline uint8_t
ComputeIndexWidth(
    const size_t Min,
    const size_t Max,
    const std::string& Charset
)
{
    __uint128_t keyspace = WordGenerator::WordLengthIndex128(Max + 1, Charset);
    size_t bits = Util::BitWidth128(keyspace);
    if (bits <= 32)  return 4;
    if (bits <= 64)  return 8;
    return 16;
}

// Runtime dispatch on index width
template<typename Func>
decltype(auto) DispatchByWidth(uint8_t Width, Func&& f)
{
    switch (Width)
    {
        case 4:  return f.template operator()<uint32_t>();
        case 8:  return f.template operator()<uint64_t>();
        default: return f.template operator()<__uint128_t>();
    }
}

class RainbowTable
{
public:
    ~RainbowTable(void);
    void Reset(void);
    bool InitAndRunBuild(void);
    bool ValidateConfig(void);
    void SetPath(std::filesystem::path Path) { m_Path = Path; }
    std::filesystem::path GetPath(void) const { return m_Path; }
    void SetAlgorithm(const std::string_view Algorithm) { m_Algorithm = ParseHashAlgorithm(Algorithm.data()); }
    const std::string GetAlgorithmString(void) const { return HashAlgorithmToString(m_Algorithm); }
    const HashAlgorithm GetAlgorithm(void) const { return m_Algorithm; }
    void SetMin(const size_t Min) { m_Min = Min; }
    const size_t GetMin(void) const { return m_Min; }
    void SetMax(const size_t Max) { m_Max = Max; }
    const size_t GetMax(void) const { return m_Max; }
    void SetLength(const size_t Length) { m_Length = Length; }
    const size_t GetLength(void) const { return m_Length; }
    void SetBlocksize(const size_t Blocksize) { m_Blocksize = Blocksize % SimdLanes() == 0 ? Blocksize : ((Blocksize + SimdLanes() - 1) / SimdLanes()) * SimdLanes(); }
    const size_t GetBlocksize(void) const { return m_Blocksize; }
    void SetCount(const size_t Count) { m_Count = Count; }
    const size_t GetCount(void) const;
    void SetCoverage(const double Coverage) { m_Coverage = std::clamp(Coverage, 0.01, 0.999); }
    const double GetCoverage(void) const { return m_Coverage; }
    void SetThreads(const size_t Threads) { m_Threads = Threads; }
    const size_t GetThreads(void) const { return m_Threads; }
    void SetCharset(const std::string_view Charset) { m_Charset = ParseCharset(Charset); }
    void SetCharsetRaw(const std::string_view Charset) { m_Charset = Charset; }
    const std::string& GetCharset(void) const { return m_Charset; }
    void SetSeed(const uint32_t Seed) { m_Seed = Seed; }
    const uint32_t GetSeed(void) const { return m_Seed; }
    void SetSeparator(const char Separator) { m_Separator = Separator; }
    const char GetSeparator(void) const { return m_Separator; }
    void SetFlushThreshold(const size_t N) { m_FlushThresholdChains = N; }
    const size_t GetFlushThreshold(void) const { return m_FlushThresholdChains; }
    float GetCoverageEstimate(void);
    bool TableExists(void) const { return std::filesystem::exists(m_Path); }
    static bool GetTableHeader(const std::filesystem::path& Path, TableHeader* Header);
    static bool IsTableFile(const std::filesystem::path& Path);
    bool IsTableFile(void) const { return IsTableFile(m_Path); }
    bool ValidTable(void) const { return TableExists() && IsTableFile(m_Path); }
    bool LoadTable(void);
    bool Complete(void) const { return m_ThreadsCompleted == m_Threads; }
    std::vector<std::tuple<std::string, std::string>> Crack(const std::string_view Target);
    // For external orchestration (e.g. RainbowTableSet)
    bool PrepareCrack(void);
    void FinishCrack(void);
    std::optional<std::string> CrackOne(const std::string_view Hash);
    static const size_t ChainWidth(const uint8_t IndexWidth);
    const size_t GetChainWidth(void) const { return ChainWidth(m_IndexWidth); }
    static void DoHash(const uint8_t* Data, const size_t Length, uint8_t* Digest, const HashAlgorithm Algorithm) { SimdHashSingle(Algorithm, Length, Data, Digest); };
    static const std::string DoHashHex(const uint8_t* Data, const size_t Length, const HashAlgorithm Algorithm);
    void DoHash(const uint8_t* Data, const size_t Length, uint8_t* Digest) const { DoHash(Data, Length, Digest, m_Algorithm); }
    std::string DoHashHex(const uint8_t* Data, const size_t Length) const { return DoHashHex(Data, Length, m_Algorithm); }

    void SortTable(void);
    static const Chain GetChain(const std::filesystem::path& Path, const size_t Index);
    static const Chain ComputeChain(const size_t Index, const size_t Min, const size_t Max, const size_t Length, const HashAlgorithm Algorithm, const std::string& Charset);
    const __uint128_t GetEndpointAt(const size_t Index) const;
    const InternalRecord GetRecordAt(const size_t Index) const;
    size_t CountUniqueEndpoints(void);
private:
    // General purpose
    std::optional<size_t> FindStartIndexForEndpoint(const __uint128_t) const;
    std::optional<std::string> ValidateChain(const size_t ChainIndex, const std::span<const uint8_t> Hash) const;
    bool TableMapped(void) { return m_MappedTableFd != nullptr; };
    bool MapTable(const bool ReadOnly = true);
    bool UnmapTable(void);
    static const __uint128_t CalculateLowerBound(const size_t Min, const std::string& Charset) { return WordGenerator::WordLengthIndex128(Min, Charset); };
    const __uint128_t CalculateLowerBound(void) const { return CalculateLowerBound(m_Min, m_Charset); };
    // Building
    void StoreTableHeader(void) const;
    std::tuple<std::vector<InternalRecord>, uint64_t> GenerateBlockData(const size_t BlockStartId);
    void GenerateBlock(const size_t ThreadId, const size_t BlockId);
    void SaveBlock(const size_t ThreadId, const size_t BlockId, std::vector<InternalRecord> Block, const uint64_t Time);
    void OutputStatus(const std::string_view LastEndpoint) const;
    void BuildThreadCompleted(const size_t ThreadId);
    void FlushPending(void);
    void OpenExistingBuildState(void);
    void MergeSegmentsIntoFinal(void);
    void CleanupSegments(void);
    // Cracking
    void CrackOneWorker(const size_t ThreadId, const std::vector<uint8_t> Target, std::latch& Done);
    std::optional<std::string> CheckIteration(const HybridReducer& Reducer, const std::span<const uint8_t> Hash, const size_t Iteration) const;
    // SIMD-batched offset checker. Issues offsets starting at StartOffset and
    // stepping by Step (typically negative) while the next-issue cursor stays
    // strictly past MinOffsetExcl (e.g. -1 to include offset 0). Returns the
    // cracked plaintext if any chain hits and validates.
    std::optional<std::string> CheckIterationsSimd(
        const HybridReducer& Reducer,
        const std::span<const uint8_t> Target,
        const ssize_t StartOffset,
        const ssize_t Step,
        const ssize_t MinOffsetExcl) const;

    // General purpose
    std::string m_Operation;
    std::filesystem::path m_Path;
    bool m_PathLoaded = false;
    HashAlgorithm m_Algorithm = HashAlgorithmUndefined;
    size_t m_Min = 0;
    size_t m_Max = 0;
    size_t m_Length = 0;
    size_t m_Blocksize = 1024;
    size_t m_Count = 0;
    double m_Coverage = 0.99;
    size_t m_Threads = 0;
    std::string m_Charset;
    uint32_t m_Seed = 0;
    size_t m_HashWidth = 0;
    size_t m_Chains = 0;
    dispatch::DispatchPoolPtr m_DispatchPool;
    size_t m_TerminalWidth = 80;
    // For building
    struct Hash128
    {
        size_t operator()(const __uint128_t& v) const noexcept
        {
            auto lo = static_cast<uint64_t>(v);
            auto hi = static_cast<uint64_t>(v >> 64);
            return std::hash<uint64_t>{}(lo) ^ (std::hash<uint64_t>{}(hi) * 0x9e3779b97f4a7c15ULL);
        }
    };
    struct BuildSegment
    {
        std::filesystem::path path;
        std::span<uint8_t> mapped;
        FILE* fp = nullptr;
        size_t headerOffset = 0;   // 0 for .seg files; sizeof(TableHeader) for resume base
        size_t recordCount = 0;
    };
    size_t m_StartingChains = 0;
    size_t m_ThreadsCompleted = 0;
    size_t m_ChainsWritten = 0;
    size_t m_ChainsGenerated = 0;
    size_t m_ConsecutiveEmptyBlocks = 0;
    std::atomic<bool> m_BuildComplete = false;
    std::vector<InternalRecord> m_PendingChains;
    std::unique_ptr<BloomFilter> m_PendingFilter;
    std::vector<BuildSegment> m_Segments;
    size_t m_FlushThresholdChains = 1'000'000;
    // Wall-clock checkpoint: flush pending buffer at least this often so a hard
    // crash (SIGKILL/OOM/power loss) on a large-RAM build doesn't lose hours of
    // work sitting in m_PendingChains. 0 disables.
    size_t m_FlushIntervalSeconds = 300;
    std::chrono::steady_clock::time_point m_LastFlushTime;
    size_t m_NextSegmentId = 0;
    std::map<size_t, uint64_t> m_ThreadTimers;
    // For cracking
    std::span<uint8_t> m_MappedTable;
    FILE* m_MappedTableFd = nullptr;
    uint8_t m_IndexWidth = 16;
    bool m_MappedReadOnly = false;
    std::ifstream m_HashFileStream;
    char m_Separator = ':';
    std::atomic<bool> m_Cracked = false;
    std::vector<std::tuple<std::string, std::string>> m_CrackedResults;
    std::tuple<std::string, std::string> m_LastCracked;
};

#endif /* RainbowTable_hpp */
