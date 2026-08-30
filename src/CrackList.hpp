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
#include <string_view>
#include <tuple>
#include <vector>

#include "DispatchQueue.hpp"
#include "SimdHashBuffer.hpp"

#include "HashList.hpp"
#include "LineReader.hpp"
#include "Rules.hpp"

typedef enum
{
    InputTypeUnknown,
    InputTypeText,
    InputTypeBinary,
    InputTypeSingle
} HashFileType;

constexpr size_t MAX_STRING_LENGTH = Rules::MAX_PASSWORD_SIZE;

class CrackList
{
public:
    CrackList() = default;
    void SetHashFile(const std::string_view HashFile) { m_HashFile = HashFile; }
    void SetOutFile(const std::filesystem::path OutFile) { m_OutFile = OutFile; }
    void SetWordlist(const std::string_view Wordlist) { m_Wordlist = Wordlist; }
    void SetRulesFile(const std::filesystem::path RulesFile) { m_RulesFile = RulesFile; }
    void SetAlgorithm(const HashAlgorithm Algorithm) { m_Algorithm = Algorithm; }
    void SetSeparator(const std::string_view Separator) { m_Separator = Separator; }
    void SetThreads(const size_t Threads) { m_Threads = Threads; }
    void SetBlockSize(const size_t BlockSize) { m_BlockSize = BlockSize; }
    void SetBinary(const bool Binary) { m_HashType = Binary ? InputTypeBinary : InputTypeText; }
    void SetTerminalWidth(const size_t Width) { m_TerminalWidth = Width; }
    void NoHexlify(void) { m_Hexlify = false; }
    void SetParseHexInput(const bool ParseHexInput) { m_ParseHexInput = ParseHexInput; }
    void SetHexlify(const bool Autohex) { m_Hexlify = Autohex; }
    void SetBitmaskSize(const size_t BitmaskSize) { m_BitmaskSize = BitmaskSize; }
    void SetQuickLookupEnabled(const bool Enable) { m_QuickLookupEnabled = Enable; }
    void SetLinkedIn(const bool LinkedIn) { m_LinkedIn = LinkedIn; }
    void SetPasswordOnly(const bool PasswordOnly) { m_PasswordOnly = PasswordOnly; }
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
    const bool GetQuickLookupEnabled(void) const { return m_QuickLookupEnabled; }
    const bool GetAutohex(void) const { return m_Hexlify; }
    const bool GetParseHexInput(void) const { return m_ParseHexInput; }
    const bool GetLinkedIn(void) const { return m_LinkedIn; }
    const bool GetPasswordOnly(void) const { return m_PasswordOnly; }
    const bool Crack(void);
    const bool CrackLinear(void);
private:
    void CrackWorker(const size_t Id);
    void ThreadPulse(const size_t ThreadId, const uint64_t BlockTime);
    void WorkerFinished(void);
    const size_t ReadBlock(SimdHashBufferFixed<MAX_STRING_LENGTH>& Words);
    const size_t ReadRuleBlock(SimdHashBufferFixed<MAX_STRING_LENGTH>& Words);
    const bool LoadRulesFile(void);
    void OutputResultInternal(const std::string_view Hash, const std::string_view Cracked, std::ostream& Output, const bool SetLastCracked = true);
    void OutputResultsInternal(std::vector<std::tuple<std::vector<uint8_t>,std::string,std::string>> Results);
    bool m_PasswordOnly = false;
    bool m_Hexlify = true;
    size_t m_BitmaskSize = 16;
    bool m_QuickLookupEnabled = false;
    std::vector<uint8_t> m_Hashes;
    std::string m_HashFile;
    HashFileType m_HashType = InputTypeUnknown;
    std::filesystem::path m_OutFile;
    std::string m_Wordlist;
    std::filesystem::path m_RulesFile;
    std::vector<Rules::CompiledRule> m_Rules;
    std::string m_RuleInput;
    size_t m_RuleIndex = 0;
    bool m_HaveRuleInput = false;
    HashAlgorithm m_Algorithm = HashAlgorithmUndefined;
    size_t m_DigestLength = 0;
    HashList m_HashList;
    std::ifstream m_WordlistFileStream;
    LineReader<> m_LineReader;
    std::ofstream m_OutputFileStream;
    std::string m_Separator = ":";
    std::string m_LastCracked;
    size_t m_Count;
    std::atomic<size_t> m_WordsProcessed = 0;
    std::atomic<size_t> m_BlocksProcessed = 0;
    size_t m_Cracked = 0;
    bool m_ParseHexInput = true;
    size_t m_TerminalWidth = 80;
    bool m_LinkedIn = false;
    // Threading
    std::mutex m_InputMutex;
    std::mutex m_ResultsMutex;
    bool m_Exhausted = false;
    bool m_Finished = false;
    size_t m_Threads = 1;
    dispatch::DispatcherBasePtr m_MainThread;
    dispatch::DispatcherPoolPtr m_DispatchPool;
    size_t m_ActiveWorkers;
    size_t m_BlockSize = 8192;
    std::map<size_t, uint64_t> m_LastBlockMs;
};

#endif //CrackList_hpp