//
//  SimdCrack.hpp
//  SimdCrack
//
//  Created by Kryc on 14/09/2020.
//  Copyright © 2020 Kryc. All rights reserved.
//

#ifndef SimdCrack_hpp
#define SimdCrack_hpp

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

#include <gmpxx.h>

#include "HashList.hpp"
#include "Util.hpp"
#include "WordGenerator.hpp"
#include "DispatchQueue.hpp"
#include "SharedRefptr.hpp"
#include "simdhash.h"

class SimdCrack
{
public:
    SimdCrack() = default;
    ~SimdCrack(void);
    void InitAndRun(void);
    void SetBlocksize(const size_t BlockSize) { m_Blocksize = BlockSize; }
    void SetAlgorithm(const HashAlgorithm Algo) { m_Algorithm = Algo; m_HashWidth = GetHashWidth(m_Algorithm); }
    void SetThreads(const size_t Threads) { m_Threads = Threads; }
    void SetOutFile(const std::filesystem::path Outfile) { m_Outfile = Outfile; }
    void SetResume(const std::string& Resume) { m_ResumeString = Resume; }
    void SetPrefix(const std::string& Prefix) { m_Prefix = Prefix; }
    void SetPostfix(const std::string& Postfix) { m_Postfix = Postfix; }
    void SetCharset(const std::string& Charset) { m_Charset = ParseCharset(Charset); }
    void SetExtra(const std::string& Charset) { m_Charset += ParseCharset(Charset); }
    void AddTarget(const std::string& Target) { m_Target.push_back(Target); }
    void SetMin(const size_t Min) { m_Min = Min; }
    void SetMax(const size_t Max) { m_Max = Max; }
    void SetSeparator(const char Separator) { m_Separator = Separator; }
    void SetBitmaskSize(const size_t BitmaskSize) { m_BitmaskSize = BitmaskSize; }
    void SetHexlify(const bool Hexlify) { m_Hexlify = Hexlify; }
    const size_t GetBitmaskSize(void) const { return m_BitmaskSize; }
    const char GetSeparator(void) const { return m_Separator; }
    const bool GetHexlify(void) const { return m_Hexlify; }
private:
    void GenerateBlocks(const size_t ThreadId, const mpz_class Start, const size_t Step);
    void FoundResults(std::vector<std::tuple<std::string, std::string>> Results);
    bool ProcessHashList(void);
    bool AddHashToList(const std::string Hash);
    bool AddHashToList(const uint8_t* Hash);
    void ThreadPulse(const size_t ThreadId, const uint64_t BlockTime, const mpz_class Last);
    void ThreadCompleted(const size_t ThreadId);

    dispatch::DispatcherPoolPtr m_DispatchPool;
    std::vector<std::string> m_Target;
    bool m_Hexlify = true;
    HashList m_HashList;
    WordGenerator m_Generator;
    size_t m_Found = 0;
    size_t m_Threads = 0;
    size_t m_Blocksize = 512;
    HashAlgorithm m_Algorithm = HashAlgorithmUndefined;
    size_t m_HashWidth = SHA256_SIZE;
    FILE* m_BinaryFd = nullptr;
    uint8_t* m_Targets = nullptr;
    size_t   m_TargetsSize;
    size_t   m_TargetsAllocated = 0;
    size_t   m_TargetsCount = 0;
    std::vector<uint8_t*> m_TargetOffsets;
	std::vector<size_t> m_TargetCounts;
    size_t   m_BlocksCompleted;
    std::map<size_t, uint64_t> m_LastBlockMs;
    std::string m_LastWord;
    std::string m_Outfile;
    std::ofstream m_OutfileStream;
    mpz_class m_Resume;
    std::string m_Prefix;
    std::string m_Postfix;
    std::string m_Charset = ASCII;
    std::string m_ResumeString;
    size_t m_Min = 1;
    size_t m_Max = MAX_OPTIMIZED_BUFFER_SIZE;
    mpz_class m_Limit;
    size_t m_ThreadsCompleted = 0;
    char m_Separator = ':';
    size_t m_BitmaskSize = 16;
};

#endif // SimdCrack_hpp