//
//  Database.hpp
//  CrackDB++
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef Database_hpp
#define Database_hpp

#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdio.h>
#include <vector>
#include <sys/mman.h>

#include <DispatchQueue.hpp>

#include "MappedDatabase.hpp"
#include "Util.hpp"
#include "Wordfile.hpp"

/*
 * The Database class represents the hash database
 * It is actually just a path to a directory which
 * contains files in a specific layout.
 * {db}/{words}/{length}.txt
 * {db}/{hash}.db
 */
class CrackDatabase
{
public:
    CrackDatabase(const std::filesystem::path Path);
    ~CrackDatabase(void);
    bool Exists(void) { return std::filesystem::exists(m_Path) && std::filesystem::is_directory(m_Path); };
    std::filesystem::path GetPath(void) { return m_Path; };
    const bool Build(const std::vector<HashAlgorithm> Types, const std::filesystem::path InputWords);
    const std::optional<std::string> Lookup(const HashAlgorithm Algorithm, const std::vector<uint8_t>& Hash) const;
    const std::optional<std::string> Lookup(const std::vector<uint8_t>& Hash) const;
    const std::optional<std::string> Lookup(const std::string& Hash) const { return Lookup(Util::ParseHex(Hash)); };
    const bool CrackFile(const std::string& HashfileInput);
    const std::optional<std::string> Test(const HashAlgorithm Algorithm, const std::string& Value);
    const bool HasAlgorithm(const HashAlgorithm Algorithm) const;
    void DisableFileHandleCache(void) { m_CacheWordFiles = false; };
    void SetMin(const size_t Min) { m_Min = Min; };
    void SetMax(const size_t Max) { m_Max = std::min<size_t>(std::numeric_limits<uint32_t>::max(), Max); };
    void SetOutput(const std::string& Output) { m_Output = Output; };
    void SetUncrackable(const std::string& Uncrackable) { m_Uncrackable = Uncrackable; };
    void SetSeparator(const std::string& Separator) { m_Separator = Separator; };
    void SetPasswordsOnly(const bool PasswordsOnly) { m_PasswordsOnly = PasswordsOnly; };
    void SetHex(const bool Hex) { m_Hex = Hex; };
    void SetThreads(const size_t Threads) { m_Threads = Threads; };
    void SetBlockSize(const size_t BlockSize) { m_BlockSize = BlockSize; };
    const size_t GetMin(void) { return m_Min; };
    const size_t GetMax(void) { return m_Max; };
    const std::string& GetOutput(void) { return m_Output; };
    const std::filesystem::path& GetUncrackable(void) { return m_Uncrackable; };
    const std::string& GetSeparator(void) { return m_Separator; };
    const bool GetPasswordsOnly(void) { return m_PasswordsOnly; };
    bool GetHex(void) { return m_Hex; };
    const size_t GetThreads(void) { return m_Threads; };
    const size_t GetBlockSize(void) { return m_BlockSize; };
    const std::filesystem::path GetWordsPath(void) const { return m_Path / "words"; };
    const bool HasWordSize(const size_t Size) const;
private:
    const size_t OpenWordfilesForLookup(void);
    const size_t OpenDatabaseFilesForLookup(void);
    void Sort(const HashAlgorithm Algorithm) const;
    void CrackFileInternal(void);
    const bool CrackFileLinear(void);
    void OutputResult(const std::string& Hash, const std::string& Value, std::ostream& Stream) const;
    const std::optional<std::string> Lookup(const HashAlgorithm Algorithm, const FileMapping& Mapping, const uint8_t* const Hash, const size_t Length) const;
    const std::optional<std::string> Lookup(const HashAlgorithm Algorithm, const uint8_t* const Hash, const size_t Length) const;
    const std::filesystem::path DatabaseFile(const HashAlgorithm Algorithm) const;
    std::vector<WordfilePtr> GetAllWordFiles(const size_t Length, const bool Write) const;
    void AddWordSize(const size_t Size);
    WordfilePtr GetWordfile(const size_t Length, const bool Write) const;
    const std::optional<std::string> CheckResult(const uint8_t* const Target, const size_t TargetLength, const DatabaseRecord* const Value, const DatabaseRecord* const Base, const DatabaseRecord* const Top, const HashAlgorithm Algorithm) const;
    std::optional<std::shared_ptr<const MappedDatabase>> GetDatabase(const HashAlgorithm Algorithm) const;
    std::optional<const FileMapping> GetCachedDatabaseMapping(const HashAlgorithm Algorithm) const;
    std::optional<const MappedDatabase> OpenDatabaseNoCache(const HashAlgorithm Algorithm) const;
    size_t m_Min = 1;
    size_t m_Max = std::numeric_limits<uint32_t>::max();
    size_t m_Threads = 1;
    std::filesystem::path m_Path;
    std::string m_Separator = ":";
    bool m_PasswordsOnly = false;
    bool m_Hex = true;
    // For file cracking
    std::mutex m_InputMutex;
    std::mutex m_OutputMutex;
    std::string m_Output;
    std::filesystem::path m_Uncrackable;
    std::ofstream m_OutputFileStream;
    std::ofstream m_UncrackableStream;
    std::ifstream m_InputFileStream;
    size_t m_BlockSize = 1024;
    // Internals
    std::map<HashAlgorithm, std::filesystem::path> m_HashDatabases;
    bool m_CacheWordFiles = true;
    std::map<size_t, std::shared_ptr<Wordfile>> m_Wordfiles;
    std::map<HashAlgorithm, std::shared_ptr<const MappedDatabase>> m_DatabaseCache;
    std::vector<size_t> m_Wordsizes;
    size_t m_MaxWordSize = 0;
    dispatch::DispatcherPoolPtr m_DispatchPool;
};

#endif /* Database_hpp */