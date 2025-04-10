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
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>

#include "DispatchQueue.hpp"
#include "simdhash.h"

#include "Chain.hpp"
#include "HashList.hpp"
#include "Reduce.hpp"
#include "SmallString.hpp"

typedef enum _TableType
{
    TypeUncompressed,
    TypeCompressed,
    TypeInvalid
} TableType;

constexpr uint32_t kMagic = 'rt- ';

typedef struct  __attribute__((__packed__)) _TableHeader
{
    uint32_t magic;
    uint8_t  type:2;
    uint8_t  algorithm:6;
    uint8_t  min;
    uint8_t  max;
    uint8_t  charsetlen;
    uint64_t length;
    char     charset[128];
} TableHeader;

typedef struct _TableRecord
{
    bool operator<(const _TableRecord& other) const
    {
        return endpoint < other.endpoint;
    }
    uint64_t startpoint;
    uint64_t endpoint;
} TableRecord;

typedef struct _TableRecordCompressed
{
    bool operator<(const _TableRecordCompressed& other) const
    {
        return endpoint < other.endpoint;
    }
    _TableRecordCompressed& operator=(const TableRecord& other)
    {
        endpoint = other.endpoint;
        return *this;
    }
    uint64_t endpoint;
} TableRecordCompressed;

class RainbowTable
{
public:
    ~RainbowTable(void);
    void Reset(void);
    void InitAndRunBuild(void);
    bool ValidateConfig(void);
    void SetPath(std::filesystem::path Path) { m_Path = Path; }
    std::filesystem::path GetPath(void) const { return m_Path; }
    void SetAlgorithm(const std::string& Algorithm) { m_Algorithm = ParseHashAlgorithm(Algorithm.c_str()); }
    const std::string GetAlgorithmString(void) const { return HashAlgorithmToString(m_Algorithm); }
    const HashAlgorithm GetAlgorithm(void) const { return m_Algorithm; }
    void SetMin(const size_t Min) { m_Min = Min; }
    const size_t GetMin(void) const { return m_Min; }
    void SetMax(const size_t Max) { m_Max = Max; }
    const size_t GetMax(void) const { return m_Max; }
    void SetLength(const size_t Length) { m_Length = Length; }
    const size_t GetLength(void) const { return m_Length; }
    void SetBlocksize(const size_t Blocksize) { m_Blocksize = Blocksize % SimdLanes() == 0 ? Blocksize : (Blocksize + SimdLanes()) % SimdLanes(); }
    const size_t GetBlocksize(void) const { return m_Blocksize; }
    void SetCount(const size_t Count) { m_Count = Count; }
    const size_t GetCount(void) const;
    void SetThreads(const size_t Threads) { m_Threads = Threads; }
    const size_t GetThreads(void) const { return m_Threads; }
    void SetCharset(const std::string Charset) { m_Charset = ParseCharset(Charset); }
    const std::string& GetCharset(void) const { return m_Charset; }
    void SetType(const TableType Type) { m_TableType = Type; }
    bool SetType(const std::string Type);
    void SetSeparator(const char Separator) { m_Separator = Separator; }
    const char GetSeparator(void) const { return m_Separator; }
    std::string GetType(void) const { return m_TableType == TypeCompressed ? "Compressed" : "Uncompressed";  }
    float GetCoverage(void);
    void DisableIndex(void) { m_IndexDisable = true; }
    bool TableExists(void) const { return std::filesystem::exists(m_Path); }
    static bool GetTableHeader(const std::filesystem::path& Path, TableHeader* Header);
    static bool IsTableFile(const std::filesystem::path& Path);
    bool IsTableFile(void) const { return IsTableFile(m_Path); }
    bool ValidTable(void) const { return TableExists() && IsTableFile(m_Path); }
    bool LoadTable(void);
    bool Complete(void) const { return m_ThreadsCompleted == m_Threads; }
    std::vector<std::tuple<std::string, std::string>> Crack(std::string& Target);
    static const size_t ChainWidthForType(const TableType Type, const size_t Max);
    const size_t GetChainWidth(void) const { return ChainWidthForType(m_TableType, m_Max); }
    static void DoHash(const uint8_t* Data, const size_t Length, uint8_t* Digest, const HashAlgorithm Algorithm) { SimdHashSingle(Algorithm, Length, Data, Digest); };
    static const std::string DoHashHex(const uint8_t* Data, const size_t Length, const HashAlgorithm Algorithm);
    void DoHash(const uint8_t* Data, const size_t Length, uint8_t* Digest) const { DoHash(Data, Length, Digest, m_Algorithm); }
    std::string DoHashHex(const uint8_t* Data, const size_t Length) const { return DoHashHex(Data, Length, m_Algorithm); }
    void Decompress(const std::filesystem::path& Destination) { ChangeType(Destination, TypeUncompressed); }
    void Compress(const std::filesystem::path& Destination) { ChangeType(Destination, TypeCompressed); }
    void SortTable(void);
    static const Chain GetChain(const std::filesystem::path& Path, const size_t Index);
    static const Chain ComputeChain(const size_t Index, const size_t Min, const size_t Max, const size_t Length, const HashAlgorithm Algorithm, const std::string& Charset);
    inline const uint64_t GetEndpointAt(const size_t Index) const;
    inline const TableRecord GetRecordAt(const size_t Index) const;
protected:
    void SortStartpoints(void);
    void RemoveStartpoints(void);
private:
    // General purpose
    void ChangeType(const std::filesystem::path& Destination, const TableType Type);
    std::optional<size_t> FindStartIndexForEndpoint(const uint64_t) const;
    std::optional<std::string> ValidateChain(const size_t ChainIndex, const uint8_t* Hash) const;
    bool TableMapped(void) { return m_MappedTableFd != nullptr; };
    bool MapTable(const bool ReadOnly = true);
    bool UnmapTable(void);
#ifdef BIGINT
    static const mpz_class CalculateLowerBound(const size_t Min, const std::string& Charset) { return WordGenerator::WordLengthIndex(Min, Charset); };
    const mpz_class CalculateLowerBound(void) const { return CalculateLowerBound(m_Min, m_Charset); };
#else
    static const uint64_t CalculateLowerBound(const size_t Min, const std::string& Charset) { return WordGenerator::WordLengthIndex64(Min, Charset); };
    const uint64_t CalculateLowerBound(void) const { return CalculateLowerBound(m_Min, m_Charset); };
#endif
    // Building
    void StoreTableHeader(void) const;
    void GenerateBlock(const size_t ThreadId, const size_t BlockId);
    void SaveBlock(const size_t ThreadId, const size_t BlockId, const std::vector<TableRecord> Block, const uint64_t Time);
    void OutputStatus(const std::string_view LastEndpoint) const;
    void WriteBlock(const size_t BlockId, std::span<const TableRecord> Block);
    void BuildThreadCompleted(const size_t ThreadId);
    // Cracking
    std::optional<std::string> CrackOne(const std::string& Target);
    void CrackOneWorker(const size_t ThreadId, const std::vector<uint8_t> Target);
    std::optional<std::string> CheckIteration(const HybridReducer& Reducer, const std::span<const uint8_t> Hash, const size_t Iteration) const;

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
    size_t m_Threads = 0;
    std::string m_Charset;
    size_t m_HashWidth = 0;
    size_t m_Chains = 0;
    TableType m_TableType = TypeCompressed;
    dispatch::DispatchPoolPtr m_DispatchPool;
    size_t m_TerminalWidth = 80;
    // For building
    size_t m_StartingChains = 0;
    FILE* m_WriteHandle = NULL;
    size_t m_NextWriteBlock = 0;
    std::map<size_t, const std::vector<TableRecord>> m_WriteCache;
    size_t m_ThreadsCompleted = 0;
    size_t m_ChainsWritten = 0;
    std::map<size_t, uint64_t> m_ThreadTimers;
    // For cracking
    std::span<uint8_t> m_MappedTable;
    std::span<TableRecord> m_MappedTableRecords;
    std::span<TableRecordCompressed> m_MappedTableRecordsCompressed;
    FILE* m_MappedTableFd = nullptr;
    size_t m_MappedFileSize;
    size_t m_MappedTableSize;
    static constexpr size_t LOOKUP_SIZE = std::numeric_limits<uint16_t>::max() + 1;
    const uint8_t* m_MappedTableLookup[LOOKUP_SIZE];
    size_t m_MappedTableLookupSize[LOOKUP_SIZE];
    bool m_IndexDisable = false;
    bool m_Indexed = false;
    bool m_MappedReadOnly = false;
    std::ifstream m_HashFileStream;
    std::mutex m_HashFileStreamLock;
    char m_Separator = ':';
    std::atomic<bool> m_Cracked = false;
    std::atomic<size_t> m_CrackingThreadsRunning = 0;
    std::vector<std::tuple<std::string, std::string>> m_CrackedResults;
    std::tuple<std::string, std::string> m_LastCracked;
};

#endif /* RainbowTable_hpp */
