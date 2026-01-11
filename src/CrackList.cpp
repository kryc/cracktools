//
//  CrackList.cpp
//  CrackList
//
//  Created by Kryc on 25/10/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <assert.h>

#include <algorithm>
#include <format>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string.h>
#include <thread>
#include <tuple>
#include <vector>

#include "SimdHashBuffer.hpp"

#include "CrackList.hpp"
#include "HashList.hpp"
#include "LineReader.hpp"
#include "Util.hpp"

const size_t
CrackList::ReadBlock(
    SimdHashBufferFixed<MAX_STRING_LENGTH>& Words
)
{
    static const size_t lanes = SimdLanes();
    std::string_view line;
    size_t count = 0;
    std::string temp;

    // Loop until the block is full or the input is exhausted
    for (size_t i = 0; i < lanes; i++)
    {
        if (m_LineReader.IsEof())
        {
            m_Exhausted = true;
            break;
        }

        m_LineReader.ReadLine(line);

        // Handle hashcat output wordlists, take the part after the separator
        const size_t sepPos = line.find(m_Separator);
        if (sepPos != std::string_view::npos &&
            Util::IsHex(line.substr(0, sepPos)))
        {
            line = line.substr(sepPos + m_Separator.size());
        }

        if (Util::IsHexlified(line) && m_ParseHexInput)
        {
            temp = Util::UnHexlify(line);
            line = temp;
        }

        Words.Set(i, line);
        count++;
    }

    m_WordsProcessed += count;

    return count;
}

const bool
CrackList::CrackLinear(
    void
)
{
    std::ostream& output = m_OutputFileStream.is_open() ? m_OutputFileStream : std::cout;
    std::string last_cracked;

    std::cerr << "Performing linear crack" << std::endl;

    auto start = std::chrono::system_clock::now();

    const size_t hashWidth = GetHashWidth(m_Algorithm);
    SimdHashBufferFixed<MAX_STRING_LENGTH> words;
    std::array<uint8_t, MAX_HASH_SIZE * MAX_LANES> hashes;
    std::span<uint8_t, MAX_HASH_SIZE * MAX_LANES> hashspan(hashes);

    while (!m_Exhausted)
    {
        auto count = ReadBlock(words);

        // Can be empty if the input is blocksize aligned
        if (count == 0)
        {
            continue;
        }

        SimdHash(
            m_Algorithm,
            words.GetLengths(),
            words.ConstBuffers(),
            &hashes[0]
        );

        for (size_t h = 0; h < count; h++)
        {
            auto hash = hashspan.subspan(h * hashWidth, hashWidth);

            if (m_HashList.Lookup(hash))
            {
                auto hex = Util::ToHex(hash);
                OutputResultInternal(hex, words.GetStringView(h), output);
                last_cracked = words.GetStringView(h);
            }
            else if (m_LinkedIn && hashWidth == SHA1_SIZE)
            {
                // Try again with the first bytes set to zero
                std::array<uint8_t, SHA1_SIZE> modifiedHash;
                std::copy(hash.begin(), hash.end(), modifiedHash.begin());
                modifiedHash[0] = 0;
                modifiedHash[1] = 0;
                modifiedHash[2] &= 0x0f;
                if (m_HashList.Lookup(modifiedHash))
                {
                    auto hex = Util::ToHex(hash);
                    m_Cracked++;
                    OutputResultInternal(hex, words.GetStringView(h), output);
                    last_cracked = words.GetStringView(h);
                }
            }
        }

        m_BlocksProcessed++;

        auto end = std::chrono::system_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        ThreadPulse(0, elapsed_ms.count());
        start = std::chrono::system_clock::now();

        if (m_Cracked == m_Count)
        {
            break;
        }
    }
    return true;
}

void
CrackList::OutputResultInternal(
    const std::string_view Hash,
    const std::string_view Cracked,
    std::ostream& Output,
    const bool SetLastCracked
)
{
    std::string_view out = Cracked;
    std::string temp;

    m_Cracked++;

    if (m_Hexlify)
    {
        temp = Util::Hexlify(Cracked);
        out = temp;
    }

    if (m_PasswordOnly)
    {
        Output << out << std::endl;
    }
    else
    {
        Output << Hash << m_Separator << out << std::endl;
    }

    if (SetLastCracked)
    {
        m_LastCracked = Cracked;
    }

    // Check if we have found all the tarets
    if (m_Cracked == m_Count)
    {
        m_Finished = true;
    }
}

void
CrackList::OutputResultsInternal(
    std::vector<std::tuple<std::vector<uint8_t>,std::string,std::string>> Results
)
{
    std::ostream& output = m_OutputFileStream.is_open() ? m_OutputFileStream : std::cout;

    for (auto& [h,x,v] : Results)
    {
        OutputResultInternal(x, v, output, false);
    }

    if (!Results.empty())
    {
        m_LastCracked = std::get<2>(Results.back());
    }
}

void
CrackList::ThreadPulse(
    const size_t ThreadId,
    const uint64_t BlockTime
)
{
    m_LastBlockMs[ThreadId] = BlockTime;

    std::string printable_cracked = Util::Hexlify(m_LastCracked);
    std::transform(printable_cracked.begin(), printable_cracked.end(), printable_cracked.begin(),
        [](unsigned char c){ return c > ' ' && c < '~' ? c : ' ' ; });

    // Output the status if we are not printing to stdout
    if (!m_OutFile.string().empty())
    {
        uint64_t averageMs = 0;
        for (auto const& [thread, val] : m_LastBlockMs)
        {
            averageMs += val;
        }
        // The average time a thread takes to do one block in ms
        averageMs /= m_Threads;

        // The number of hashes per second
        std::string hps_ch;
        double hashesPerSec = (double)(m_BlockSize * 1000 * m_Threads) / averageMs;
        hashesPerSec = Util::NumFactor(hashesPerSec, hps_ch);
        // double hashesPerSec = (double)(m_BlockSize * 1000) / BlockTime;

        const size_t hashcount = m_HashList.GetCount();
        double percent = ((double)m_Cracked / hashcount) * 100.f;

        std::string status = std::format(
            "H/s:{:.1f}{} C:{}/{} ({:.1f}%) T:{} C:\"{}\"",
            hashesPerSec,
            hps_ch,
            m_Cracked,
            hashcount,
            percent,
            m_WordsProcessed.load(),
            printable_cracked
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
}

void
CrackList::WorkerFinished(
    void
)
{
    if (--m_ActiveWorkers == 0)
    {
        // Stop the pool
        m_DispatchPool->Stop();
        m_DispatchPool->Wait();
        // Stop the current main thread
        dispatch::CurrentDispatcher()->Stop();
    }
}

void
CrackList::CrackWorker(
    const size_t Id
)
{
    static const size_t lanes = SimdLanes();
    srand(Id);

    // Check if all input is done
    if (m_Finished || m_Exhausted)
    {
        // Track the completion of this worker
        dispatch::PostTaskToDispatcher(
            "main",
            std::bind(
                &CrackList::WorkerFinished,
                this
            )
        );
        // Terminate our current queue
        dispatch::CurrentQueue()->Stop();
        return;
    }

    std::vector<std::tuple<std::vector<uint8_t>,std::string,std::string>> cracked;

    auto start = std::chrono::system_clock::now();

    const size_t hashWidth = GetHashWidth(m_Algorithm);
    SimdHashBufferFixed<MAX_STRING_LENGTH> words;
    std::array<uint8_t, MAX_HASH_SIZE * MAX_LANES> hashes;
    std::span<uint8_t, MAX_HASH_SIZE * MAX_LANES> hashspan(hashes);

    for (size_t block = 0; block < m_BlockSize && !m_Exhausted; block += lanes)
    {
        // Read a block of words
        size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(m_InputMutex);
            count = ReadBlock(words);
        }

        if (count == 0)
        {
            continue;
        }

        SimdHash(
            m_Algorithm,
            words.GetLengths(),
            words.ConstBuffers(),
            &hashes[0]
        );

        for (size_t h = 0; h < count; h++)
        {
            std::span<uint8_t> hash = hashspan.subspan(h * hashWidth, hashWidth);

            if (m_HashList.Lookup(hash))
            {
                auto hex = Util::ToHex(hash);
                hex = Util::ToLower(hex);
                cracked.push_back({
                    {hash.begin(), hash.end()},
                    hex,
                    std::string(words.GetStringView(h))
                });
            }
            else if (m_LinkedIn && hashWidth == SHA1_SIZE)
            {
                // Try again with the first bytes set to zero
                std::array<uint8_t, SHA1_SIZE> modifiedHash;
                std::copy(hash.begin(), hash.end(), modifiedHash.begin());
                modifiedHash[0] = 0;
                modifiedHash[1] = 0;
                modifiedHash[2] &= 0x0f;
                if (m_HashList.Lookup(modifiedHash))
                {
                    auto hex = Util::ToHex(hash);
                    hex = Util::ToLower(hex);
                    cracked.push_back({
                        {hash.begin(), hash.end()},
                        hex,
                        std::string(words.GetStringView(h))
                    });
                }
            }
        }
    }

    auto end = std::chrono::system_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (cracked.size() > 0)
    {
        std::lock_guard<std::mutex> lock(m_ResultsMutex);
        OutputResultsInternal(std::move(cracked));
    }

    m_BlocksProcessed++;

    dispatch::PostTaskToDispatcher(
        "main",
        std::bind(
            &CrackList::ThreadPulse,
            this,
            Id,
            elapsed_ms.count()
        )
    );

    dispatch::PostTaskFast(
        std::bind(
            &CrackList::CrackWorker,
            this,
            Id
        )
    );
}

const bool
CrackList::Crack(
    void
)
{
    bool result = false;

    // Check parameters
    if (m_HashFile == "")
    {
        std::cerr << "Error: no hash file specified" << m_HashFile << std::endl;
        return false;
    }
    else if (!std::filesystem::exists(m_HashFile))
    {
        std::cerr << "Error: hash file does not exist" << std::endl;
        return false;
    }

    if (m_BlockSize % SimdLanes() != 0)
    {
        std::cerr << "Error: Block Size must be a multiple of Simd Lanes (" << SimdLanes() << ")" << std::endl;
        return false;
    }

    // Open the input file
    if (m_Wordlist != "-" && m_Wordlist != "")
    {
        if (!std::filesystem::exists(m_Wordlist))
        {
            std::cerr << "Error: Wordlist file does not exist" << std::endl;
            return false;
        }
        if (!m_LineReader.SetInputFile(m_Wordlist))
        {
            std::cerr << "Error: Unable to open wordlist file" << std::endl;
            return false;
        }
    }
    else
    {
        if (!m_LineReader.SetFileStream(&std::cin))
        {
            std::cerr << "Error: Unable to read from stdin" << std::endl;
            return false;
        }
    }

    if (m_OutFile != "")
    {
        m_OutputFileStream.open(m_OutFile, std::ios::out | std::ios::app);
    }

    // Detect the input type
    if (m_HashType == InputTypeUnknown)
    {
        if (m_HashFile.ends_with(".txt") || m_HashFile.ends_with(".lst"))
        {
            m_HashType = InputTypeText;
        }
        else if (m_HashFile.ends_with(".bin") || m_HashFile.ends_with(".dat"))
        {
            m_HashType = InputTypeBinary;
        }
        else if (Util::IsHex(m_HashFile))
        {
            m_HashType = InputTypeSingle;
        }
        else
        {
            std::cerr << "Error: Unable to determine input hash file type" << std::endl;
            return false;
        }
    }

    m_HashList.SetBitmaskSize(m_BitmaskSize);
    m_HashList.EnableQuickLookup(m_QuickLookupEnabled);

    if (m_LinkedIn)
    {
        std::cerr << "LinkedIn mode enabled" << std::endl;
        m_Algorithm = HashAlgorithmSHA1;
        m_DigestLength = SHA1_SIZE;
    }

    // Open the hash file
    if (m_HashType == InputTypeBinary)
    {
        if (m_Algorithm == HashAlgorithmUndefined)
        {
            std::cerr << "Error: binary hash list with no algorithm" << std::endl;
            return false;
        }

        m_DigestLength = GetHashWidth(m_Algorithm);
        if (!m_HashList.Initialize(m_HashFile, m_DigestLength))
        {
            std::cerr << "Error: unable to initialize hash list" << std::endl;
            return false;
        }
    }
    else if (m_HashType == InputTypeText)
    {
        std::cerr << "Parsing hash list" << std::endl;

        LineReader<> infile(m_HashFile);
        if (infile.Open() == false)
        {
            std::cerr << "Error: unable to open hash list file" << std::endl;
            return false;
        }
        std::string_view line;
        while (infile.ReadLine(line))
        {
            // If there is a colon in the line, take only the part before it
            auto colon_pos = line.find(':');
            if (colon_pos != std::string_view::npos)
            {
                line = line.substr(0, colon_pos);
            }
            if (m_Algorithm == HashAlgorithmUndefined)
            {
                m_Algorithm = DetectHashAlgorithmHex(line.size());
                if (m_Algorithm == HashAlgorithmUndefined)
                {
                    std::cerr << "Unable to detect hash algorithm" << std::endl;
                    return false;
                }
                std::cerr << HashAlgorithmToString(m_Algorithm) << " detected" << std::endl;
                m_DigestLength = GetHashWidth(m_Algorithm);
            }
            else if (m_DigestLength == 0)
            {
                m_DigestLength = GetHashWidth(m_Algorithm);
            }
            
            if (line.size() != m_DigestLength * 2)
            {
                std::cerr << "Invalid hash found, ignoring " << line.size() << "!=" << m_DigestLength * 2 << ": \"" << line << "\"" << std::endl;
                continue;
            }

            // Add the new hash to the list
            auto bytes = Util::ParseHex(line);
            m_Hashes.insert(m_Hashes.end(), bytes.begin(), bytes.end());
        }

        m_HashList.Initialize(m_Hashes, m_DigestLength, true);
    }
    else if (m_HashType == InputTypeSingle)
    {
        m_Algorithm = DetectHashAlgorithmHex(m_HashFile.size());
        if (m_Algorithm == HashAlgorithmUndefined)
        {
            std::cerr << "Unable to detect hash algorithm" << std::endl;
            return false;
        }
        m_DigestLength = GetHashWidth(m_Algorithm);
        std::cerr << HashAlgorithmToString(m_Algorithm) << " detected" << std::endl;
        // Add the new hash to the list
        auto bytes = Util::ParseHex(m_HashFile);
        m_Hashes.insert(m_Hashes.end(), bytes.begin(), bytes.end());
        m_HashList.Initialize(m_Hashes, m_DigestLength, false);
    }

    m_Count = m_HashList.GetCount();

    std::cerr << "Beginning cracking" << std::endl;
    
    if (m_Threads == 1)
    {
        result = CrackLinear();
    }
    else 
    {
        if (m_Threads == 0)
        {
            m_Threads = std::thread::hardware_concurrency();
        }

        m_DispatchPool = dispatch::CreateDispatchPool("worker", m_Threads);
        m_ActiveWorkers = m_Threads;

        for (size_t i = 0; i < m_Threads; i++)
        {
            m_DispatchPool->PostTask(
                dispatch::bind(
                    &CrackList::CrackWorker,
                    this,
                    i
                )
            );
        }

        dispatch::CreateAndEnterDispatcher(
            "main",
            std::bind(
                dispatch::DoNothing
            )
        );

        result = true;
    }

    // Terminate the status line
    if (!m_OutFile.string().empty())
    {
        std::cerr << std::endl;
    }

    std::cerr << "Processed " << m_WordsProcessed << " inputs" << std::endl;
    std::cerr << "Processed " << m_BlocksProcessed << " blocks" << std::endl;
    std::cerr << "Cracked   " << m_Cracked << " hashes" << std::endl;

    return result;
}
