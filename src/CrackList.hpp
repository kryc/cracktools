//
//  CrackList.hpp
//  CrackList
//
//  Created by Kryc on 25/10/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef CrackList_hpp
#define CrackList_hpp

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <tuple>

#include "DispatchQueue.hpp"
#include "simdhash.h"

#include "HashList.hpp"

typedef enum
{
    InputTypeUnknown,
    InputTypeText,
    InputTypeBinary,
    InputTypeSingle
} HashFileType;

class CrackList
{
public:
    CrackList() = default;
    void SetHashFile(const std::string HashFile) { m_HashFile = HashFile; }
    void SetOutFile(const std::filesystem::path OutFile) { m_OutFile = OutFile; }
    void SetWordlist(const std::string Wordlist) { m_Wordlist = Wordlist; }
    void SetAlgorithm(const HashAlgorithm Algorithm) { m_Algorithm = Algorithm; }
    void SetSeparator(const std::string Separator) { m_Separator = Separator; }
    void SetThreads(const size_t Threads) { m_Threads = Threads; }
    void SetBlockSize(const size_t BlockSize) { m_BlockSize = BlockSize; }
    void SetBinary(const bool Binary) { m_HashType = Binary ? InputTypeBinary : InputTypeText; }
    void SetTerminalWidth(const size_t Width) { m_TerminalWidth = Width; }
    void DisableAutohex(void) { m_Hexlify = false; }
    void SetParseHexInput(const bool ParseHexInput) { m_ParseHexInput = ParseHexInput; }
    void SetAutohex(const bool Autohex) { m_Hexlify = Autohex; }
    void SetBitmaskSize(const size_t BitmaskSize) { m_BitmaskSize = BitmaskSize; }
    void SetLinkedIn(const bool LinkedIn) { m_LinkedIn = LinkedIn; }
    const std::string GetHashFile(void) const { return m_HashFile; }
    const std::filesystem::path GetOutFile(void) const { return m_OutFile; }
    const std::string GetWordlist(void) const { return m_Wordlist; }
    const HashAlgorithm GetAlgorithm(void) const { return m_Algorithm; }
    const std::string GetSeparator(void) const { return m_Separator; }
    const size_t GetThreads(void) const { return m_Threads; }
    const size_t GetBlockSize(void) const { return m_BlockSize; }
    const bool GetBinary(void) const { return m_HashType == InputTypeBinary; }
    const size_t GetTerminalWidth(void) const { return m_TerminalWidth; }
    const size_t GetBitmaskSize(void) const { return m_BitmaskSize; }
    const bool GetAutohex(void) const { return m_Hexlify; }
    const bool GetParseHexInput(void) const { return m_ParseHexInput; }
    const bool GetLinkedIn(void) const { return m_LinkedIn; }
    const bool Crack(void);
    const bool CrackLinear(void);
private:
    void CrackWorker(const size_t Id);
    void ThreadPulse(const size_t ThreadId, const uint64_t BlockTime, const std::string LastCracked, const std::string LastTry);
    void WorkerFinished(void);
    void ReadInput(void);
    std::vector<std::string> ReadBlock(void);
    void OutputResults(void);
    void OutputResultsInternal(std::vector<std::tuple<std::vector<uint8_t>,std::string,std::string>>& Results);
    bool m_Hexlify = true;
    size_t m_BitmaskSize = 16;
    std::vector<uint8_t> m_Hashes;
    std::string m_HashFile;
    HashFileType m_HashType = InputTypeUnknown;
    std::filesystem::path m_OutFile;
    std::string m_Wordlist;
    HashAlgorithm m_Algorithm = HashAlgorithmUndefined;
    size_t m_DigestLength;
    HashList m_HashList;
    std::ifstream m_WordlistFileStream;
    std::ofstream m_OutputFileStream;
    std::string m_Separator = ":";
    std::string m_LastLine;
    std::string m_LastCracked;
    size_t m_Count;
    std::atomic<size_t> m_WordsProcessed = 0;
    std::atomic<size_t> m_BlocksProcessed = 0;
    size_t m_Cracked = 0;
    bool m_ParseHexInput = false;
    size_t m_TerminalWidth = 80;
    bool m_LinkedIn = false;
    // Threading
    std::mutex m_InputMutex;
    std::mutex m_ResultsMutex;
    std::vector<std::tuple<std::vector<uint8_t>,std::string,std::string>> m_Results;
    std::queue<std::vector<std::string>> m_InputCache;
    size_t m_CacheSizeBlocks = 4096;
    bool m_Exhausted = false;
    bool m_Finished = false;
    size_t m_Threads = 1;
    dispatch::DispatcherBasePtr m_MainThread;
    dispatch::DispatcherBasePtr m_IoThread;
    dispatch::DispatcherPoolPtr m_DispatchPool;
    size_t m_ActiveWorkers;
    size_t m_BlockSize = 8192;
    std::map<size_t, uint64_t> m_LastBlockMs;
};

#endif //CrackList_hpp