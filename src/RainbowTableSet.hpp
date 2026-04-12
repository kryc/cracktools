//
//  RainbowTableSet.hpp
//  SimdRainbowCrack
//
//  Created on 11/04/2026.
//

#ifndef RainbowTableSet_hpp
#define RainbowTableSet_hpp

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "simdhash.h"
#include "RainbowTable.hpp"

struct TableInfo
{
    std::filesystem::path path;
    HashAlgorithm algorithm;
    uint8_t min;
    uint8_t max;
    uint64_t length;
    std::string charset;
    uint32_t seed;
    size_t count;
    float coverage;
};

class RainbowTableSet
{
public:
    // Configuration
    void SetDirectory(const std::filesystem::path& Dir) { m_Directory = Dir; }
    const std::filesystem::path& GetDirectory(void) const { return m_Directory; }
    void SetAlgorithm(const std::string_view Algorithm) { m_Algorithm = ParseHashAlgorithm(Algorithm.data()); }
    void SetAlgorithm(const HashAlgorithm Algorithm) { m_Algorithm = Algorithm; }
    const std::string GetAlgorithmString(void) const { return HashAlgorithmToString(m_Algorithm); }
    const HashAlgorithm GetAlgorithm(void) const { return m_Algorithm; }
    void SetMin(const size_t Min) { m_Min = Min; }
    const size_t GetMin(void) const { return m_Min; }
    void SetMax(const size_t Max) { m_Max = Max; }
    const size_t GetMax(void) const { return m_Max; }
    void SetLength(const size_t Length) { m_Length = Length; }
    const size_t GetLength(void) const { return m_Length; }
    void SetCharset(const std::string_view Charset) { m_Charset = ParseCharset(Charset); }
    const std::string& GetCharset(void) const { return m_Charset; }
    void SetCoverage(const double Coverage) { m_Coverage = std::clamp(Coverage, 0.01, 0.999); }
    const double GetCoverage(void) const { return m_Coverage; }
    void SetTableCount(const size_t Count) { m_TableCount = Count; }
    const size_t GetTableCount(void) const { return m_TableCount; }
    void SetChainCount(const size_t Count) { m_ChainCount = Count; }
    const size_t GetChainCount(void) const { return m_ChainCount; }
    void SetThreads(const size_t Threads) { m_Threads = Threads; }
    const size_t GetThreads(void) const { return m_Threads; }
    void SetBlocksize(const size_t Blocksize) { m_Blocksize = Blocksize; }
    void SetSeparator(const char Separator) { m_Separator = Separator; }
    const char GetSeparator(void) const { return m_Separator; }

    // Actions
    void Build(void);
    std::vector<std::tuple<std::string, std::string>> Crack(const std::string_view Target);
    void Info(void);
    void Test(const std::string_view Target);

    // Queries
    std::vector<TableInfo> ScanDirectory(void) const;
    std::vector<TableInfo> ScanCompatibleTables(void) const;
    std::string GenerateFilename(const uint32_t Seed) const;
    float GetCombinedCoverage(const std::vector<TableInfo>& Tables) const;
    bool LoadConfigFromDirectory(void);

private:
    void ConfigureTable(RainbowTable& Table, const uint32_t Seed) const;
    size_t ComputeChainsPerTable(void) const;
    size_t ComputeTableCount(void) const;

    std::filesystem::path m_Directory;
    HashAlgorithm m_Algorithm = HashAlgorithmUndefined;
    size_t m_Min = 0;
    size_t m_Max = 0;
    size_t m_Length = 0;
    size_t m_Blocksize = 0;
    size_t m_Threads = 0;
    std::string m_Charset;
    double m_Coverage = 0.99;
    size_t m_TableCount = 0;    // 0 = auto from coverage
    size_t m_ChainCount = 0;    // 0 = auto from coverage
    char m_Separator = ':';
};

#endif /* RainbowTableSet_hpp */
