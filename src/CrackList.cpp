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

constexpr size_t kLinkedInIgnoreBytes = 6;

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

#if 0
        // Strip carriage return if present at the end
        if (!line.empty() && line.back() == '\r')
        {
            line = line.substr(0, line.size() - 1);
        }
#endif

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
            std::span<uint8_t> lookupHash = m_LinkedIn ? hash.subspan(kLinkedInIgnoreBytes) : hash;

            if (m_HashList.Lookup(lookupHash))
            {
                auto hex = Util::ToHex(hash);
                m_Cracked++;
                output << hex << m_Separator << Util::Hexlify(words.GetStringView(h)) << std::endl;
                last_cracked = words.GetStringView(h);
            }
        }

        std::string back(words.GetStringView(count - 1));
        m_BlocksProcessed++;

        auto end = std::chrono::system_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        ThreadPulse(0, elapsed_ms.count(), std::move(last_cracked), std::move(back));
        start = std::chrono::system_clock::now();

        if (m_Cracked == m_Count)
        {
            break;
        }
    }
    return true;
}

void
CrackList::OutputResultsInternal(
    std::vector<std::tuple<std::vector<uint8_t>,std::string,std::string>>& Results
)
{
    std::ostream& output = m_OutputFileStream.is_open() ? m_OutputFileStream : std::cout;

    for (auto& [h,x,v] : Results)
    {
        m_Cracked++;
        output << x << m_Separator << v << std::endl;
        m_LastCracked = v;
    }

    // Check if we have found all the tarets
    if (m_Cracked == m_Count)
    {
        m_Finished = true;
    }
}

void
CrackList::OutputResults(
    void
)
{
    // Take a copy of the results
    std::vector<std::tuple<std::vector<uint8_t>,std::string,std::string>> copy;
    {
        std::lock_guard<std::mutex> lock(m_ResultsMutex);
        copy = std::move(m_Results);
    }

    OutputResultsInternal(copy);
}

void
CrackList::ThreadPulse(
    const size_t ThreadId,
    const uint64_t BlockTime,
    const std::string LastCracked,
    const std::string LastTry
)
{
    m_LastBlockMs[ThreadId] = BlockTime;

    if (!LastCracked.empty())
    {
        m_LastCracked = LastCracked;
    }

    std::string printable_cracked = Util::Hexlify(m_LastCracked);
    std::transform(printable_cracked.begin(), printable_cracked.end(), printable_cracked.begin(),
        [](unsigned char c){ return c > ' ' && c < '~' ? c : ' ' ; });

    std::string printable_last = Util::Hexlify(LastTry);
    std::transform(printable_last.begin(), printable_last.end(), printable_last.begin(),
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
            "H/s:{:.1f}{} C:{}/{} ({:.1f}%) T:{} C:\"{}\" L:\"{}\"",
            hashesPerSec,
            hps_ch,
            m_Cracked,
            hashcount,
            percent,
            m_WordsProcessed.load(),
            printable_cracked,
            printable_last
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
    std::string last_cracked, back;

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
            std::span<uint8_t> lookupHash = m_LinkedIn ? hash.subspan(kLinkedInIgnoreBytes) : hash;

            if (m_HashList.Lookup(lookupHash))
            {
                auto hex = Util::ToHex(hash);
                hex = Util::ToLower(hex);
                cracked.push_back({
                    {hash.begin(), hash.end()},
                    hex,
                    Util::Hexlify(words.GetStringView(h))
                });
            }
        }

        if (count > 0)
        {
            back = words.GetStringView(count - 1);
        }
    }

    auto end = std::chrono::system_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (cracked.size() > 0)
    {
        std::lock_guard<std::mutex> lock(m_ResultsMutex);
        last_cracked = std::get<2>(cracked.back());
        OutputResultsInternal(cracked);
    }

    m_BlocksProcessed++;

    dispatch::PostTaskToDispatcher(
        "main",
        std::bind(
            &CrackList::ThreadPulse,
            this,
            Id,
            elapsed_ms.count(),
            std::move(last_cracked),
            std::move(back)
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
        m_LineReader.SetInputFile(m_Wordlist);
    }
    else
    {
        m_LineReader.SetFileStream(&std::cin);
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

    // Set some values for linkedin mode
    if (m_LinkedIn)
    {
        m_Algorithm = HashAlgorithmSHA1;
        m_DigestLength = GetHashWidth(m_Algorithm) - kLinkedInIgnoreBytes;
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
        std::string_view line;
        while (infile.ReadLine(line))
        {
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

            if (m_LinkedIn)
            {
                // In linkedin mode we strip the first kLinkedInIgnoreBytes bytes
                line = line.substr(kLinkedInIgnoreBytes * 2);
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
