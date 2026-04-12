//
//  RainbowTableSet.cpp
//  SimdRainbowCrack
//
//  Created on 11/04/2026.
//

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/sysinfo.h>
#include <vector>

#include "RainbowTableSet.hpp"
#include "Util.hpp"
#include "WordGenerator.hpp"

bool
RainbowTableSet::LoadConfigFromDirectory(
    void
)
{
    if (!std::filesystem::exists(m_Directory))
    {
        return false;
    }

    // Find the first valid .tbl file and load config from it
    for (const auto& entry : std::filesystem::directory_iterator(m_Directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".tbl")
        {
            continue;
        }

        TableHeader hdr;
        if (!RainbowTable::GetTableHeader(entry.path(), &hdr))
        {
            continue;
        }

        m_Algorithm = static_cast<HashAlgorithm>(hdr.algorithm);
        m_Min = hdr.min;
        m_Max = hdr.max;
        m_Length = hdr.length;
        m_Charset = std::string(hdr.charset, hdr.charsetlen);
        return true;
    }

    return false;
}

std::string
RainbowTableSet::GenerateFilename(
    const uint32_t Seed
) const
{
    std::string algo = HashAlgorithmToString(m_Algorithm);
    std::transform(algo.begin(), algo.end(), algo.begin(), ::tolower);
    // Find a short charset name, or use the length 
    auto maybeCharsetName = CharsetName(m_Charset);
    std::string charsetName = maybeCharsetName
        ? std::string(*maybeCharsetName)
        : "c" + std::to_string(m_Charset.size());

    return algo + "_" + charsetName + "_" +
           std::to_string(m_Min) + "-" + std::to_string(m_Max) + "_" +
           std::to_string(m_Length) + "_" +
           std::to_string(Seed) + ".tbl";
}

std::vector<TableInfo>
RainbowTableSet::ScanDirectory(
    void
) const
{
    std::vector<TableInfo> tables;

    if (!std::filesystem::exists(m_Directory))
    {
        return tables;
    }

    for (const auto& entry : std::filesystem::directory_iterator(m_Directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".tbl")
        {
            continue;
        }

        TableHeader hdr;
        if (!RainbowTable::GetTableHeader(entry.path(), &hdr))
        {
            continue;
        }

        RainbowTable table;
        table.SetPath(entry.path());
        if (!table.LoadTable())
        {
            continue;
        }

        TableInfo info;
        info.path = entry.path();
        info.algorithm = static_cast<HashAlgorithm>(hdr.algorithm);
        info.min = hdr.min;
        info.max = hdr.max;
        info.length = hdr.length;
        info.charset = std::string(hdr.charset, hdr.charsetlen);
        info.seed = hdr.seed;
        info.count = table.GetCount();
        info.coverage = table.GetCoverageEstimate();
        tables.push_back(std::move(info));
    }

    // Sort by seed for consistent ordering
    std::sort(tables.begin(), tables.end(),
        [](const TableInfo& a, const TableInfo& b) { return a.seed < b.seed; });

    return tables;
}

std::vector<TableInfo>
RainbowTableSet::ScanCompatibleTables(
    void
) const
{
    auto all = ScanDirectory();
    std::erase_if(all, [this](const TableInfo& t)
    {
        return t.algorithm != m_Algorithm
            || t.min != m_Min
            || t.max != m_Max
            || t.length != m_Length
            || t.charset != m_Charset;
    });
    return all;
}

float
RainbowTableSet::GetCombinedCoverage(
    const std::vector<TableInfo>& Tables
) const
{
    // Combined coverage = 1 - product(1 - c_i)
    double miss = 1.0;
    for (const auto& t : Tables)
    {
        miss *= 1.0 - (t.coverage / 100.0);
    }
    return static_cast<float>((1.0 - miss) * 100.0);
}

size_t
RainbowTableSet::ComputeTableCount(
    void
) const
{
    if (m_TableCount > 0)
    {
        return m_TableCount;
    }

    // If the user specified chain count manually, default to 1 table
    if (m_ChainCount > 0)
    {
        return 1;
    }

    // Compute how many tables at individual max coverage we
    // need to achieve the target combined coverage.
    // First estimate the per-table coverage ceiling from the endpoint space.
    // After t iterations, the effective endpoint fraction is given by the
    // beta recurrence: beta_{k+1} = 1 - e^{-beta_k}, starting at beta_0 = 1.
    __uint128_t lowerbound = WordGenerator::WordLengthIndex128(m_Min, m_Charset);
    __uint128_t upperbound = WordGenerator::WordLengthIndex128(m_Max + 1, m_Charset);
    double N = static_cast<double>(upperbound - lowerbound);

    double beta = 1.0;
    for (size_t i = 0; i < m_Length; i++)
    {
        beta = 1.0 - std::exp(-beta);
    }
    // At ~95% fill of endpoint space, per-table coverage is:
    double maxUnique = 0.95 * beta * N;
    double perTable = 1.0 - std::exp(-maxUnique * m_Length / N);
    perTable = std::min(perTable, 0.999);  // Clamp to avoid log(0)

    // Combined coverage: 1 - (1-c)^k >= target
    // k = ceil(ln(1-target) / ln(1-c))
    double k = std::log(1.0 - m_Coverage) / std::log(1.0 - perTable);
    return std::max(size_t(1), static_cast<size_t>(std::ceil(k)));
}

size_t
RainbowTableSet::ComputeChainsPerTable(
    void
) const
{
    if (m_ChainCount > 0)
    {
        return m_ChainCount;
    }

    size_t tableCount = ComputeTableCount();

    // Per-table coverage needed: 1 - (1-total)^(1/k)
    double perTableCoverage = 1.0 - std::pow(1.0 - m_Coverage, 1.0 / tableCount);

    // m = -ln(1 - c) * N / t
    __uint128_t lowerbound = WordGenerator::WordLengthIndex128(m_Min, m_Charset);
    __uint128_t upperbound = WordGenerator::WordLengthIndex128(m_Max + 1, m_Charset);
    double N = static_cast<double>(upperbound - lowerbound);
    double factor = -std::log(1.0 - perTableCoverage);
    double chains = factor * N / m_Length;

    return static_cast<size_t>(chains);
}

void
RainbowTableSet::ConfigureTable(
    RainbowTable& Table,
    const uint32_t Seed
) const
{
    Table.SetPath(m_Directory / GenerateFilename(Seed));
    Table.SetAlgorithm(GetAlgorithmString());
    Table.SetMin(m_Min);
    Table.SetMax(m_Max);
    Table.SetLength(m_Length);
    Table.SetSeed(Seed);
    Table.SetCharsetRaw(m_Charset);
    if (m_Threads > 0) Table.SetThreads(m_Threads);
    if (m_Blocksize > 0) Table.SetBlocksize(m_Blocksize);
}

void
RainbowTableSet::Build(
    void
)
{
    size_t tableCount = ComputeTableCount();
    size_t chainsPerTable = ComputeChainsPerTable();

    // Estimate memory needed for Bloom filter dedup (~10 bits per element)
    size_t estimatedSetMemory = (chainsPerTable * 10 + 7) / 8;
    uint8_t indexWidth = ComputeIndexWidth(m_Min, m_Max, m_Charset);
    size_t estimatedTableSize = chainsPerTable * indexWidth * 2;

    std::string memChar, tblChar;
    double memSize = Util::SizeFactor(static_cast<double>(estimatedSetMemory), memChar);
    double tblSize = Util::SizeFactor(static_cast<double>(estimatedTableSize), tblChar);

    std::cerr << "Building " << tableCount << " table(s), "
              << chainsPerTable << " chains each" << std::endl;
    std::cerr << "Per-table size: " << std::fixed << std::setprecision(1)
              << tblSize << " " << tblChar
              << ", dedup memory: ~" << memSize << " " << memChar << std::endl;

    // Check against available system memory
    struct sysinfo si;
    if (sysinfo(&si) == 0)
    {
        size_t availableMemory = static_cast<size_t>(si.freeram + si.bufferram) * si.mem_unit;
        if (estimatedSetMemory > availableMemory)
        {
            std::string availChar;
            double availSize = Util::SizeFactor(static_cast<double>(availableMemory), availChar);
            std::cerr << "Error: Bloom filter requires ~"
                      << memSize << " " << memChar
                      << " but only " << std::fixed << std::setprecision(1)
                      << availSize << " " << availChar << " available." << std::endl;
            std::cerr << "Use -n to specify a smaller chain count, e.g.:" << std::endl;
            std::cerr << "  simdrainbowcrack build -n 10000000 ..." << std::endl;
            return;
        }
    }

    // Create directory if needed
    std::filesystem::create_directories(m_Directory);

    // Scan for existing compatible tables
    auto existing = ScanCompatibleTables();

    // Determine which seeds already exist and what their chain counts are
    std::map<uint32_t, TableInfo> existingBySeed;
    for (const auto& t : existing)
    {
        existingBySeed[t.seed] = t;
    }

    for (uint32_t seed = 0; seed < tableCount; seed++)
    {
        auto it = existingBySeed.find(seed);
        if (it != existingBySeed.end() && it->second.count >= chainsPerTable)
        {
            std::cerr << "Table " << seed << " already has "
                      << it->second.count << " chains, skipping" << std::endl;
            continue;
        }

        std::cerr << std::endl << "=== Table " << (seed + 1) << "/" << tableCount
                  << " ===" << std::endl;

        RainbowTable table;
        ConfigureTable(table, seed);
        table.SetCount(chainsPerTable);
        table.InitAndRunBuild();
    }

    // Print combined summary
    auto tables = ScanCompatibleTables();
    float combined = GetCombinedCoverage(tables);
    std::cerr << std::endl << "Combined coverage: " << std::fixed
              << std::setprecision(1) << combined << "% across "
              << tables.size() << " table(s)" << std::endl;
}

std::vector<std::tuple<std::string, std::string>>
RainbowTableSet::Crack(
    const std::string_view Target
)
{
    auto tables = ScanDirectory();
    if (tables.empty())
    {
        std::cerr << "No tables found in " << m_Directory << std::endl;
        return {};
    }

    std::cerr << "Cracking with " << tables.size() << " table(s)" << std::endl;

    std::vector<std::tuple<std::string, std::string>> allResults;
    bool isFile = !Util::IsHex(Target) && std::filesystem::exists(Target);

    if (Util::IsHex(Target))
    {
        // Single hash: try each table until found
        for (const auto& ti : tables)
        {
            RainbowTable table;
            table.SetPath(ti.path);
            table.LoadTable();
            if (m_Threads > 0) table.SetThreads(m_Threads);

            if (!table.PrepareCrack()) continue;

            auto result = table.CrackOne(Target);
            table.FinishCrack();

            if (result.has_value())
            {
                std::cout << Target << m_Separator << result.value() << std::endl;
                allResults.emplace_back(std::string(Target), result.value());
                break;
            }
        }
    }
    else if (isFile)
    {
        // File of hashes: for each hash, try tables until cracked
        std::string targetPath{Target};
        std::ifstream hashFile{targetPath};
        std::string line;
        std::vector<std::string> hashes;

        while (std::getline(hashFile, line))
        {
            if (!line.empty())
            {
                hashes.push_back(line);
            }
        }

        // Track which hashes are still uncracked
        std::vector<bool> cracked(hashes.size(), false);

        for (const auto& ti : tables)
        {
            // Check if all cracked
            bool allDone = true;
            for (size_t i = 0; i < cracked.size(); i++)
            {
                if (!cracked[i]) { allDone = false; break; }
            }
            if (allDone) break;

            RainbowTable table;
            table.SetPath(ti.path);
            table.LoadTable();
            if (m_Threads > 0) table.SetThreads(m_Threads);

            if (!table.PrepareCrack()) continue;

            for (size_t i = 0; i < hashes.size(); i++)
            {
                if (cracked[i]) continue;

                auto result = table.CrackOne(hashes[i]);
                if (result.has_value())
                {
                    std::cout << hashes[i] << m_Separator << result.value() << std::endl;
                    allResults.emplace_back(hashes[i], result.value());
                    cracked[i] = true;
                }
            }

            table.FinishCrack();
        }
    }
    else
    {
        std::cerr << "Invalid target: not a hex hash or file" << std::endl;
    }

    return allResults;
}

void
RainbowTableSet::Info(
    void
)
{
    auto tables = ScanDirectory();
    if (tables.empty())
    {
        std::cerr << "No tables found in " << m_Directory << std::endl;
        return;
    }

    std::cout << "Directory: " << m_Directory << std::endl;
    std::cout << "Tables:    " << tables.size() << std::endl;
    std::cout << std::endl;

    // Group tables by config (algorithm, min, max, length, charset)
    struct ConfigKey
    {
        HashAlgorithm algorithm;
        uint8_t min, max;
        uint64_t length;
        std::string charset;
        bool operator==(const ConfigKey&) const = default;
    };

    std::vector<std::pair<ConfigKey, std::vector<const TableInfo*>>> groups;
    for (const auto& ti : tables)
    {
        ConfigKey key{ti.algorithm, ti.min, ti.max, ti.length, ti.charset};
        auto it = std::find_if(groups.begin(), groups.end(),
            [&key](const auto& g) { return g.first == key; });
        if (it != groups.end())
            it->second.push_back(&ti);
        else
            groups.push_back({key, {&ti}});
    }

    for (const auto& [key, group] : groups)
    {
        std::cout << HashAlgorithmToString(key.algorithm) << " "
                  << "\"" << key.charset << "\" "
                  << (int)key.min << "-" << (int)key.max
                  << " length=" << key.length << std::endl;

        for (const auto* ti : group)
        {
            std::cout << "  [" << ti->seed << "] " << ti->path.filename().string()
                      << " — " << ti->count << " chains, "
                      << std::fixed << std::setprecision(1) << ti->coverage << "% coverage"
                      << std::endl;
        }

        // Combined coverage within this group
        std::vector<TableInfo> groupVec;
        for (const auto* ti : group)
            groupVec.push_back(*ti);
        float combined = GetCombinedCoverage(groupVec);
        std::cout << "  Combined: " << std::fixed << std::setprecision(1)
                  << combined << "%" << std::endl;
        std::cout << std::endl;
    }
}

void
RainbowTableSet::Test(
    const std::string_view Target
)
{
    auto tables = ScanDirectory();
    if (tables.empty())
    {
        std::cerr << "No tables found in " << m_Directory << std::endl;
        return;
    }

    // Get the hash algorithm from the first table
    RainbowTable refTable;
    refTable.SetPath(tables[0].path);
    refTable.LoadTable();

    if (std::filesystem::exists(Target))
    {
        // Read passwords, hash them, then crack
        std::string targetPath{Target};
        std::ifstream infile{targetPath};
        std::string line;
        std::vector<std::string> passwords;
        std::vector<std::string> hashes;

        while (std::getline(infile, line))
        {
            if (line.empty()) continue;
            passwords.push_back(line);
            hashes.push_back(refTable.DoHashHex(
                reinterpret_cast<const uint8_t*>(line.data()), line.size()));
        }

        // Track which are cracked
        std::vector<bool> cracked(passwords.size(), false);
        size_t found = 0;

        for (const auto& ti : tables)
        {
            if (found == passwords.size()) break;

            RainbowTable table;
            table.SetPath(ti.path);
            table.LoadTable();
            if (m_Threads > 0) table.SetThreads(m_Threads);

            if (!table.PrepareCrack()) continue;

            for (size_t i = 0; i < hashes.size(); i++)
            {
                if (cracked[i]) continue;

                auto result = table.CrackOne(hashes[i]);
                if (result.has_value())
                {
                    cracked[i] = true;
                    found++;
                }
            }

            table.FinishCrack();
        }

        std::cout << "Found " << found << "/" << passwords.size()
                  << " (" << std::fixed << std::setprecision(1)
                  << (100.0 * found / passwords.size()) << "%)" << std::endl;
    }
    else
    {
        // Single password test
        auto hash = refTable.DoHashHex(
            reinterpret_cast<const uint8_t*>(Target.data()), Target.size());
        std::cout << "Testing for password \"" << Target << "\": " << hash << std::endl;

        for (const auto& ti : tables)
        {
            RainbowTable table;
            table.SetPath(ti.path);
            table.LoadTable();
            if (m_Threads > 0) table.SetThreads(m_Threads);

            if (!table.PrepareCrack()) continue;

            auto result = table.CrackOne(hash);
            table.FinishCrack();

            if (result.has_value())
            {
                std::cout << hash << m_Separator << result.value() << std::endl;
                return;
            }
        }

        std::cout << "Not found" << std::endl;
    }
}
