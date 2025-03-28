#include <algorithm>
#include <chrono>
#include <fstream>
#include <inttypes.h>
#include <gmp.h>
#include <mutex>
#include <sys/mman.h>

#include "SimdCrack.hpp"
#include "SimdHashBuffer.hpp"
#include "SharedRefptr.hpp"
#include "Util.hpp"
#include "WordGenerator.hpp"

#include "simdhash.h"

SimdCrack::~SimdCrack(
    void
)
{
    if (m_BinaryFd == nullptr)
    {
        free(m_Targets);
    }
    else
    {
        munmap(m_Targets, m_TargetsCount * m_HashWidth);
        fclose(m_BinaryFd);
    }
}

void
SimdCrack::InitAndRun(
    void
)
{
    if (m_Threads == 0)
    {
        m_Threads = std::thread::hardware_concurrency();
    }

    m_Generator = WordGenerator(m_Charset, m_Prefix, m_Postfix);

    m_Resume = m_Generator.WordLengthIndex(m_Min);

    m_Limit = m_Generator.WordLengthIndex(m_Max + 1);

    if (!m_ResumeString.empty())
	{
		m_Resume = m_Generator.Parse(m_ResumeString);
		std::cout << "Resuming from '" << m_ResumeString << "' (Index " << m_Resume.get_str() << ") " << std::endl;
	}

    if(!ProcessHashList())
    {
        return;
    }

    std::cerr << "Using character set: " << m_Charset << std::endl;

    //
    // Open the output file handle
    //
    if(!m_Outfile.empty())
    {
        m_OutfileStream.open(m_Outfile, std::ofstream::out | std::ofstream::app);
    }

    m_DispatchPool = dispatch::CreateDispatchPool("pool", m_Threads);

    std::cerr << "Starting cracking using " << m_Threads << " threads" << std::endl;

    for (size_t i = 0; i < m_Threads; i++)
    {
        mpz_class start(m_Resume + i + 1);

        m_DispatchPool->PostTask(
            dispatch::bind(
                &SimdCrack::GenerateBlocks,
                this,
                i,
                std::move(start),
                m_Threads
            )
        );
    }
}

int comparator(
    const void* x,
    const void* y,
    void* Width
)
{
    return memcmp(x, y, (size_t)Width);
}

inline bool
SimdCrack::AddHashToList(
    const uint8_t* Hash
)
{
    if (m_TargetsCount == m_TargetsAllocated)
    {
        m_TargetsAllocated += 1024;
        m_Targets = (uint8_t*)realloc(m_Targets, m_TargetsAllocated * m_HashWidth);
        if (m_Targets == NULL)
        {
            std::cerr << "Not enough memory to allocate hash targets!" << std::endl;
            return false;
        }
    }

    uint8_t* next = m_Targets + (m_TargetsCount * m_HashWidth);
    memcpy(next, Hash, m_HashWidth);
    m_TargetsCount++;
    return true;
}

bool
SimdCrack::AddHashToList(
    const std::string Hash
)
{
    auto nexthash = Util::ParseHex(Hash);
    return AddHashToList(&nexthash[0]);
}

bool
ValidHexHashes(
    std::vector<std::string> Hashes
)
{
    for (auto & hex : Hashes)
    {
        if (!Util::IsHex(hex))
        {
            return false;
        }
    }
    return true;
}

bool
SimdCrack::ProcessHashList(
    void
)
{
    if (m_Target.empty())
    {
        std::cerr << "No target specified" << std::endl;
        return false;
    }

    std::filesystem::path targetPath = m_Target.at(0);

    m_HashList.SetBitmaskSize(m_BitmaskSize);

    if (m_Target.size() == 1 && targetPath.extension() == ".txt")
    {
        std::cerr << "Processing hash list" << std::endl;
        std::ifstream infile(targetPath);
        std::string line;
        while (std::getline(infile, line))
        {
            if (line.size() != m_HashWidth * 2)
            {
                std::cerr << "Invalid hash found, ignoring " << line.size() << "!=" << m_HashWidth*2 << ": \"" << line << "\"" << std::endl;
                continue;
            }

            if(!AddHashToList(std::move(line)))
            {
                return false;
            }
        }

        m_HashList.Initialize(
            m_Targets,
            m_TargetsSize,
            m_HashWidth,
            true
        );
    }
    else if (m_Target.size() == 1 && targetPath.extension() == ".bin")
    {
        m_TargetsSize = std::filesystem::file_size(targetPath);
        m_TargetsCount = m_TargetsSize / m_HashWidth;
        m_BinaryFd = fopen(targetPath.c_str(), "rb");
        m_Targets = (uint8_t*)mmap(nullptr, m_TargetsSize, PROT_READ, MAP_SHARED, fileno(m_BinaryFd), 0);
        if (m_Targets == MAP_FAILED)
        {
            std::cerr << "Error mapping hash list file" << std::endl;
            return false;
        }
        // Inform the kernel that we will be performing random accesses at all offsets
        auto ret = madvise(m_Targets, m_TargetsSize, MADV_RANDOM | MADV_WILLNEED);
        if (ret != 0)
        {
            std::cerr << "Madvise not happy" << std::endl;
        }

        m_HashList.Initialize(
            m_Targets,
            m_TargetsSize,
            m_HashWidth,
            false
        );
    }
    else if (ValidHexHashes(m_Target))
    {
        for (auto& target : m_Target)
        {
            if (!AddHashToList(&target[0]))
            {
                return false;
            }
        }

        m_HashList.Initialize(
            m_Targets,
            m_TargetsSize,
            m_HashWidth,
            true
        );
    }
    else
    {
        std::cerr << "Invalid target specified" << std::endl;
        return false;
    }

    return true;
}

void
SimdCrack::FoundResults(
    std::vector<std::tuple<std::string, std::string>> Results
)
{
    assert(dispatch::CurrentDispatcher() == dispatch::GetDispatcher("main").get());

    m_Found += Results.size();

    // Output them
    for (auto& [h, w] : Results){
        if (m_OutfileStream.is_open())
        {
            m_OutfileStream << h << m_Separator << (m_Hexlify ? Util::Hexlify(w) : w) << std::endl;
        }
        else
        {
            std::cout << h << m_Separator << (m_Hexlify ? Util::Hexlify(w) : w) << std::endl;
        }
    }

    m_LastWord = std::get<1>(Results.back());

    // Check if we have found all targets
    if (m_Found == m_TargetsCount)
    {
        // Stop the pool
        m_DispatchPool->Stop();
        m_DispatchPool->Wait();

        // Stop the current (main) dispatcher
        dispatch::CurrentDispatcher()->Stop();
    }
}

void
SimdCrack::ThreadCompleted(
    const size_t ThreadId
)
{
    m_ThreadsCompleted++;
    if (m_ThreadsCompleted == m_Threads)
    {
        std::cerr << std::endl << "Input space exhausted" << std::endl;
        // Stop the pool
        if (m_DispatchPool != nullptr)
        {
            m_DispatchPool->Stop();
            m_DispatchPool->Wait();
        }

        // Stop the current (main) dispatcher
        dispatch::CurrentDispatcher()->Stop();
    }
}

void
SimdCrack::GenerateBlocks(
    const size_t ThreadId,
    const mpz_class Start,
    const size_t Step
)
{
    mpz_class index(Start);
    SimdHashBufferFixed<MAX_OPTIMIZED_BUFFER_SIZE> words;
    std::array<uint8_t, MAX_HASH_SIZE * MAX_LANES> hashes;
    std::vector<std::tuple<std::string, std::string>> results;

    // Check if we should end
    if (index >= m_Limit)
    {
        dispatch::PostTaskToDispatcher(
            "main",
            dispatch::bind(
                &SimdCrack::ThreadCompleted,
                this,
                ThreadId
            )
        );
        return;
    }

    auto start = std::chrono::system_clock::now();

    for (
        size_t counter = 0;
        counter < m_Blocksize && index < m_Limit;
        counter++
    )
    {
        for (size_t i = 0; i < SimdLanes(); i++)
        {
            const std::string word = m_Generator.Generate(index);
            words.Set(i, word);
            index += Step;
        }

        SimdHashOptimized(
            m_Algorithm,
            words.GetLengths(),
            words.ConstBuffers(),
            &hashes[0]
        );
        
        for (size_t i = 0; i < SimdLanes(); i++)
        {
            const uint8_t* hash = &hashes[i * m_HashWidth];
            if (m_HashList.Lookup(hash))
            {
                results.push_back({
                    Util::ToHex(hash, m_HashWidth),
                    words.GetString(i)
                });
            }
        }
    }

    auto end = std::chrono::system_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (!results.empty())
    {
        dispatch::PostTaskToDispatcher(
            "main",
            dispatch::bind(
                &SimdCrack::FoundResults,
                this,
                std::move(results)
            )
        );
    }

    dispatch::PostTaskToDispatcher(
        "main",
        dispatch::bind(
            &SimdCrack::ThreadPulse,
            this,
            ThreadId,
            elapsed_ms.count(),
            index
        )
    );

    dispatch::PostTaskFast(
        dispatch::bind(
            &SimdCrack::GenerateBlocks,
            this,
            ThreadId,
            index,
            Step
        )
    );
}

void
SimdCrack::ThreadPulse(
    const size_t ThreadId,
    const uint64_t BlockTime,
    const mpz_class Last
)
{
    char statusbuf[96];

    assert(dispatch::CurrentDispatcher() == dispatch::GetDispatcher("main").get());

    m_BlocksCompleted++;
    m_LastBlockMs[ThreadId] = BlockTime;

    if (!m_Outfile.empty() && ThreadId == 0)
    {
        // Get the upper and lower bound for this current length
        std::string diffch, ooch;
        auto lower = m_Generator.WordLengthIndex(m_Min);
        auto upper = m_Generator.WordLengthIndex(m_Max + 1);

        mpz_class diff = Last - lower;
        mpz_class outof = upper - lower;
        mpf_class percent = (mpf_class(diff) * 100)/ outof;
        diff = Util::NumFactor(diff, diffch);
        outof = Util::NumFactor(outof, ooch);

        uint64_t averageMs = 0;
        for (auto const& [thread, val] : m_LastBlockMs)
        {
            averageMs += val;
        }
        averageMs /= m_Threads;
        
        double hashesPerSec = (double)(m_Blocksize * 1000 * m_Threads) / averageMs;
        std::string multiplechar;
        hashesPerSec = Util::NumFactor(hashesPerSec, multiplechar);

        statusbuf[sizeof(statusbuf) - 1] = '\0';
        memset(statusbuf, '\b', sizeof(statusbuf) - 1);
        fprintf(stderr, "%s", statusbuf);
        memset(statusbuf, ' ', sizeof(statusbuf) - 1);
        int count = snprintf(
            statusbuf, sizeof(statusbuf),
            "H/s:%.1lf%s C:%zu/%zu L:\"%s\" C:\"%s\" #:%s%s/%s%s (%.1lf%%)",
                hashesPerSec,
                multiplechar.c_str(),
                m_Found,
                m_TargetsCount,
                m_LastWord.c_str(),
                m_Generator.Generate(Last).c_str(),
                diff.get_str().c_str(),
                diffch.c_str(),
                outof.get_str().c_str(),
                ooch.c_str(),
                percent.get_d()
        );
        if (count < sizeof(statusbuf) - 1)
        {
            statusbuf[count] = ' ';
        }
        fprintf(stderr, "%s", statusbuf);
    }
}