// CUDA C++20, CUDA runtime only; host links the existing Rules/RuleAnalysis code.
// CUDA 12.4 can target compute_90 PTX for driver JIT on an RTX 5080 (sm_120).
// With that toolkit use g++-13, not the system g++-15 or clang++-20.
// For example CMake's CUDA_ARCHITECTURES may be "89-real;90-virtual";
// newer toolkits can additionally emit native code for 120.
#include "RuleAnalysisCuda.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <unordered_set>

namespace RuleAnalysis
{
namespace
{

constexpr uint32_t NoTarget = UINT32_MAX;
constexpr size_t InputChunk = 32768;
constexpr size_t InitialQualificationChunk = 4096;
constexpr size_t RuleBatch = 32;
constexpr unsigned BlockSize = 256;
constexpr unsigned BloomProbes = 8;
constexpr size_t BloomUploadChunk = 1048576;
static_assert(Rules::MAX_PASSWORD_SIZE == 256);

enum : uint32_t { Applied = 1, Changed = 2, Matched = 4, Rejected = 8, Syntax = 16 };

struct Instruction
{
    unsigned char operation;
    unsigned char a;
    unsigned char b;
};

struct DeviceRule
{
    uint64_t offset;
    uint32_t count;
};

struct DeviceInput
{
    uint64_t offset;
    uint64_t length;
};

struct DeviceHash
{
    uint64_t high;
    uint64_t low;
};

struct PairResult
{
    DeviceHash hash;
    uint32_t length;
    uint32_t flags;
};

struct Hit
{
    DeviceHash hash;
    uint32_t length;
    uint32_t inputIndex;
    uint32_t ruleSlot;
};

struct Counters
{
    // Applied, changed, potential Bloom hits, rejected, syntax errors.
    // True matches are counted only after exact CPU verification.
    unsigned long long values[5];
};

// Parse compiledRule.commands, which has already undergone Rules::Compile's
// hex decoding and comment handling. Whitespace is ignored ONLY between ops;
// a literal space/tab/CR used as an argument is preserved.
bool CompileDeviceRule(const Rules::CompiledRule& Rule, std::vector<Instruction>& Output)
{
    const size_t OriginalSize = Output.size();
    const std::string& Text = Rule.commands;
    size_t Count = 0;
    for (size_t i = 0; i < Text.size();)
    {
        const unsigned char Op = static_cast<unsigned char>(Text[i++]);
        if (Op == ' ' || Op == '\t' || Op == '\r') continue;
        if (++Count > Rules::MAX_OPERATIONS)
        {
            Output.resize(OriginalSize);
            return false;
        }
        // P = numeric position, C = literal byte. Saved-position 'p' is not
        // accepted as P: those entire rules must run through the CPU engine.
        const char* Arguments = nullptr;
        switch (Op)
        {
            case ':': case 'l': case 'u': case 'c': case 'C': case 't':
            case 'r': case 'd': case 'f': case '{': case '}': case '[':
            case ']': case 'q': case 'k': case 'K': case 'E':
                Arguments = ""; break;
            case 'T': case 'p': case 'D': case '\'': case 'z': case 'Z':
            case '<': case '>': case '_': case 'L': case 'R': case '+':
            case '-': case '.': case ',': case 'y': case 'Y':
                Arguments = "P"; break;
            case '$': case '^': case '@': case '!': case '/': case '(':
            case ')': case 'e':
                Arguments = "C"; break;
            case 'x': case 'O': case '*': Arguments = "PP"; break;
            case 'i': case 'o': case '=': case '%': case '3':
                Arguments = "PC"; break;
            case 's': Arguments = "CC"; break;
            default: break;
        }
        if (Arguments == nullptr)
        {
            Output.resize(OriginalSize);
            return false;
        }
        Instruction Result{Op, 0, 0};
        unsigned char* Values[] = {&Result.a, &Result.b};
        for (size_t j = 0; Arguments[j] != '\0'; ++j)
        {
            if (i == Text.size())
            {
                Output.resize(OriginalSize);
                return false;
            }
            unsigned char Value = static_cast<unsigned char>(Text[i++]);
            if (Arguments[j] == 'P')
            {
                if (Value >= '0' && Value <= '9') Value -= '0';
                else if (Value >= 'A' && Value <= 'Z') Value = Value - 'A' + 10;
                else
                {
                    Output.resize(OriginalSize);
                    return false;
                }
            }
            *Values[j] = Value;
        }
        Output.push_back(Result);
    }
    return true;
}

__device__ unsigned char Lower(unsigned char C)
{
    return C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C;
}

__device__ unsigned char Upper(unsigned char C)
{
    return C >= 'a' && C <= 'z' ? C - ('a' - 'A') : C;
}

__device__ unsigned char Toggle(unsigned char C)
{
    return C >= 'a' && C <= 'z' ? Upper(C) : Lower(C);
}

__device__ void Swap(unsigned char& A, unsigned char& B)
{
    const unsigned char T = A;
    A = B;
    B = T;
}

// All growing transformations deliberately use <256, not <=256, exactly
// matching Rules::Apply. Existing 256-byte inputs remain legal; >256 is syntax.
__device__ uint32_t Transform(
    unsigned char* W, unsigned& N, const Instruction* Instructions, uint32_t Count)
{
    for (uint32_t j = 0; j < Count; ++j)
    {
        const Instruction I = Instructions[j];
        const unsigned A = I.a;
        const unsigned B = I.b;
        switch (I.operation)
        {
            case ':': break;
            case 'l': case 'u': case 'c': case 'C': case 't':
                for (unsigned k = 0; k < N; ++k)
                {
                    W[k] = I.operation == 't' ? Toggle(W[k])
                        : (I.operation == 'u' || I.operation == 'C') ? Upper(W[k])
                        : Lower(W[k]);
                }
                if (N && I.operation == 'c') W[0] = Upper(W[0]);
                if (N && I.operation == 'C') W[0] = Lower(W[0]);
                break;
            case 'T': if (A < N) W[A] = Toggle(W[A]); break;
            case 'r':
                for (unsigned k = 0; k < N / 2; ++k) Swap(W[k], W[N - k - 1]);
                break;
            case 'd': case 'p':
            {
                const unsigned Original = N;
                const unsigned Copies = I.operation == 'd' ? 1 : A;
                if (Original * (Copies + 1) < 256)
                {
                    for (unsigned k = 0; k < Copies * Original; ++k)
                        W[Original + k] = W[k % Original];
                    N = Original * (Copies + 1);
                }
                break;
            }
            case 'f':
                if (N * 2 < 256)
                {
                    for (unsigned k = 0; k < N; ++k) W[N + k] = W[N - k - 1];
                    N *= 2;
                }
                break;
            case '{':
                if (N)
                {
                    const unsigned char First = W[0];
                    for (unsigned k = 1; k < N; ++k) W[k - 1] = W[k];
                    W[N - 1] = First;
                }
                break;
            case '}':
                if (N)
                {
                    const unsigned char Last = W[N - 1];
                    for (unsigned k = N - 1; k > 0; --k) W[k] = W[k - 1];
                    W[0] = Last;
                }
                break;
            case '$': if (N + 1 < 256) W[N++] = A; break;
            case '^':
                if (N + 1 < 256)
                {
                    for (unsigned k = N; k > 0; --k) W[k] = W[k - 1];
                    W[0] = A;
                    ++N;
                }
                break;
            case '[': case ']': case 'D':
            {
                const unsigned Position = I.operation == '[' ? 0
                    : I.operation == ']' ? (N ? N - 1 : 0) : A;
                if (Position < N)
                {
                    for (unsigned k = Position + 1; k < N; ++k) W[k - 1] = W[k];
                    --N;
                }
                break;
            }
            case 'x':
                if (A < N && A + B <= N)
                {
                    for (unsigned k = 0; k < B; ++k) W[k] = W[A + k];
                    N = B;
                }
                break;
            case 'O':
                if (A < N && A + B <= N)
                {
                    for (unsigned k = A + B; k < N; ++k) W[k - B] = W[k];
                    N -= B;
                }
                break;
            case 'i':
                if (A <= N && N + 1 < 256)
                {
                    for (unsigned k = N; k > A; --k) W[k] = W[k - 1];
                    W[A] = B;
                    ++N;
                }
                break;
            case 'o': if (A < N) W[A] = B; break;
            case '\'': if (A < N) N = A; break;
            case 's':
                for (unsigned k = 0; k < N; ++k) if (W[k] == A) W[k] = B;
                break;
            case '@':
            {
                unsigned Length = 0;
                for (unsigned k = 0; k < N; ++k) if (W[k] != A) W[Length++] = W[k];
                N = Length;
                break;
            }
            case 'z':
                if (N && N + A < 256)
                {
                    const unsigned char First = W[0];
                    for (unsigned k = N; k > 0; --k) W[k + A - 1] = W[k - 1];
                    for (unsigned k = 0; k < A; ++k) W[k] = First;
                    N += A;
                }
                break;
            case 'Z':
                if (N && N + A < 256)
                {
                    const unsigned char Last = W[N - 1];
                    for (unsigned k = 0; k < A; ++k) W[N + k] = Last;
                    N += A;
                }
                break;
            case 'q':
                if (N && N * 2 < 256)
                {
                    for (unsigned k = N; k > 0; --k)
                    {
                        const unsigned char Value = W[k - 1];
                        W[2 * k - 1] = Value;
                        W[2 * k - 2] = Value;
                    }
                    N *= 2;
                }
                break;
            case 'k': if (N >= 2) Swap(W[0], W[1]); break;
            case 'K': if (N >= 2) Swap(W[N - 1], W[N - 2]); break;
            case '*': if (A < N && B < N) Swap(W[A], W[B]); break;
            case 'L': if (A < N) W[A] = static_cast<unsigned char>(W[A] << 1); break;
            case 'R': if (A < N) W[A] >>= 1; break;
            case '+': if (A < N) ++W[A]; break;
            case '-': if (A < N) --W[A]; break;
            case '.': if (A + 1 < N) W[A] = W[A + 1]; break;
            case ',': if (A > 0 && A < N) W[A] = W[A - 1]; break;
            case 'y':
                if (A <= N && N + A < 256)
                {
                    for (unsigned k = N; k > 0; --k) W[k + A - 1] = W[k - 1];
                    for (unsigned k = 0; k < A; ++k) W[k] = W[A + k];
                    N += A;
                }
                break;
            case 'Y':
                if (A <= N && N + A < 256)
                {
                    for (unsigned k = 0; k < A; ++k) W[N + k] = W[N - A + k];
                    N += A;
                }
                break;
            case 'E': case 'e':
            {
                const unsigned Separator = I.operation == 'E' ? ' ' : A;
                bool UpperNext = true;
                for (unsigned k = 0; k < N; ++k)
                {
                    if (W[k] == Separator) { UpperNext = true; continue; }
                    W[k] = UpperNext ? Upper(W[k]) : Lower(W[k]);
                    UpperNext = false;
                }
                break;
            }
            case '3':
            {
                unsigned Seen = 0;
                for (unsigned k = 0; k < N; ++k)
                {
                    if (W[k] != B || Seen++ != A) continue;
                    if (k + 1 < N) W[k + 1] = Toggle(W[k + 1]);
                    break;
                }
                break;
            }
            case '<': if (N > A) return Rejected; break;
            case '>': if (N < A) return Rejected; break;
            case '_': if (N != A) return Rejected; break;
            case '!': case '/':
            {
                bool Found = false;
                for (unsigned k = 0; k < N; ++k) if (W[k] == A) { Found = true; break; }
                if ((I.operation == '!' && Found) || (I.operation == '/' && !Found))
                    return Rejected;
                break;
            }
            case '(': if (!N || W[0] != A) return Rejected; break;
            case ')': if (!N || W[N - 1] != A) return Rejected; break;
            case '=': if (A >= N || W[A] != B) return Rejected; break;
            case '%':
            {
                unsigned Found = 0;
                for (unsigned k = 0; k < N; ++k) if (W[k] == B) ++Found;
                if (Found < A) return Rejected;
                break;
            }
            default: return Syntax; // Defensive: host parser never emits this.
        }
    }
    return Applied;
}

__device__ DeviceHash Fingerprint(const unsigned char* Word, unsigned Length)
{
    // FNV-1a 128 modulo 2^128. prime = (0x1000000 << 64) + 0x13b.
    // __umul64hi provides the carry from the low-word multiplication; CUDA
    // device code does not need a compiler-specific 128-bit integer type.
    uint64_t High = 0x6c62272e07bb0142ULL;
    uint64_t Low = 0x62b821756295c58dULL;
    for (unsigned k = 0; k < Length; ++k)
    {
        Low ^= Word[k];
        High = High * 0x13bULL + __umul64hi(Low, 0x13bULL) + (Low << 24);
        Low *= 0x13bULL;
    }
    return {High, Low};
}

__device__ uint64_t BloomMix(uint64_t Value)
{
    Value ^= Value >> 30;
    Value *= 0xbf58476d1ce4e5b9ULL;
    Value ^= Value >> 27;
    Value *= 0x94d049bb133111ebULL;
    return Value ^ (Value >> 31);
}

__device__ uint64_t BloomStart(const DeviceHash Hash, const unsigned Length)
{
    return BloomMix(Hash.low ^ (static_cast<uint64_t>(Length) * 0x9e3779b97f4a7c15ULL));
}

__device__ uint64_t BloomStep(const DeviceHash Hash)
{
    return BloomMix(Hash.high ^ 0xd6e8feb86659fd93ULL) | 1ULL;
}

__global__ void InsertBloom(
    const DeviceHash* Hashes, const unsigned Count, const unsigned Length,
    unsigned long long* Bits, const uint64_t BitCount)
{
    const unsigned Index = blockIdx.x * blockDim.x + threadIdx.x;
    if (Index >= Count) return;
    const DeviceHash Hash = Hashes[Index];
    uint64_t Position = BloomStart(Hash, Length);
    const uint64_t Step = BloomStep(Hash);
    for (unsigned Probe = 0; Probe < BloomProbes; ++Probe)
    {
        const uint64_t Bit = Position % BitCount;
        atomicOr(&Bits[Bit / 64], 1ULL << (Bit % 64));
        Position += Step;
    }
}

__device__ bool BloomContains(
    const DeviceHash Hash, const unsigned Length,
    const unsigned long long* Bits, const uint64_t BitCount)
{
    uint64_t Position = BloomStart(Hash, Length);
    const uint64_t Step = BloomStep(Hash);
    for (unsigned Probe = 0; Probe < BloomProbes; ++Probe)
    {
        const uint64_t Bit = Position % BitCount;
        if ((Bits[Bit / 64] & (1ULL << (Bit % 64))) == 0) return false;
        Position += Step;
    }
    return true;
}

__device__ uint32_t TransformInput(
    const unsigned char* Plaintext, const DeviceInput Input,
    const Instruction* Instructions, const DeviceRule Rule,
    unsigned char* Word, unsigned& Length)
{
    if (Input.length > 256) return Syntax;
    Length = static_cast<unsigned>(Input.length);
    for (unsigned k = 0; k < Length; ++k) Word[k] = Plaintext[Input.offset + k];
    const uint32_t Status = Transform(Word, Length,
        Rule.count ? Instructions + Rule.offset : nullptr, Rule.count);
    if (Status != Applied) return Status;
    bool Different = Length != Input.length;
    if (!Different)
        for (unsigned k = 0; k < Length; ++k)
            if (Word[k] != Plaintext[Input.offset + k]) { Different = true; break; }
    return Different ? Applied | Changed : Applied;
}

__device__ PairResult EvaluatePair(
    const unsigned char* Plaintext, const DeviceInput Input,
    const Instruction* Instructions, const DeviceRule Rule,
    const unsigned long long* Bloom, const uint64_t BitCount,
    const uint64_t* LengthCounts)
{
    unsigned char Word[256];
    unsigned Length = 0;
    const uint32_t Status = TransformInput(Plaintext, Input, Instructions, Rule, Word, Length);
    if (!(Status & Changed)) return {{0, 0}, Length, Status};

    if (LengthCounts[Length] == 0) return {{0, 0}, Length, Applied | Changed};

    const DeviceHash Hash = Fingerprint(Word, Length);
    if (BloomContains(Hash, Length, Bloom, BitCount))
        return {Hash, Length, Applied | Changed | Matched}; // Potential hit only.
    return {{0, 0}, Length, Applied | Changed};
}

__global__ void Evaluate(
    const unsigned char* Plaintext, const DeviceInput* Inputs,
    const Instruction* Instructions, const DeviceRule* Rules,
    const unsigned long long* Bloom, const uint64_t BitCount,
    const uint64_t* LengthCounts,
    uint64_t InputStart, uint64_t RuleStart, unsigned InputCount,
    const unsigned* Active, Hit* Hits, unsigned* HitCount,
    unsigned HitCapacity, Counters* Totals)
{
    if (!Active[blockIdx.y]) return;
    __shared__ unsigned Counts[5];
    if (threadIdx.x < 5) Counts[threadIdx.x] = 0;
    __syncthreads();
    const unsigned Index = blockIdx.x * blockDim.x + threadIdx.x;
    if (Index < InputCount)
    {
        const PairResult Result = EvaluatePair(Plaintext, Inputs[InputStart + Index],
            Instructions, Rules[RuleStart + blockIdx.y], Bloom, BitCount, LengthCounts);
        if (Result.flags & Matched)
        {
            const unsigned Position = atomicAdd(HitCount, 1U);
            if (Position < HitCapacity)
            {
                Hits[Position] = {Result.hash, Result.length, Index, blockIdx.y};
            }
        }
        if (Result.flags & Applied) atomicAdd(&Counts[0], 1U);
        if (Result.flags & Changed) atomicAdd(&Counts[1], 1U);
        if (Result.flags & Matched) atomicAdd(&Counts[2], 1U);
        if (Result.flags & Rejected) atomicAdd(&Counts[3], 1U);
        if (Result.flags & Syntax) atomicAdd(&Counts[4], 1U);
    }
    __syncthreads();
    if (threadIdx.x < 5)
        atomicAdd(&Totals[blockIdx.y].values[threadIdx.x],
            static_cast<unsigned long long>(Counts[threadIdx.x]));
}

// Re-evaluate only the final qualifying prefix to recover exact applied,
// changed/rejected/error counts without transferring per-input status flags.
__global__ void CountPrefixes(
    const unsigned char* Plaintext, const DeviceInput* Inputs,
    const Instruction* Instructions, const DeviceRule* Rules,
    uint64_t InputStart, uint64_t RuleStart,
    const unsigned* PrefixLengths, Counters* Totals)
{
    const unsigned Length = PrefixLengths[blockIdx.y];
    if (Length == 0) return;
    __shared__ unsigned Counts[5];
    if (threadIdx.x < 5) Counts[threadIdx.x] = 0;
    __syncthreads();
    const unsigned Index = blockIdx.x * blockDim.x + threadIdx.x;
    if (Index < Length)
    {
        unsigned char Word[256];
        unsigned OutputLength = 0;
        const uint32_t Status = TransformInput(Plaintext, Inputs[InputStart + Index],
            Instructions, Rules[RuleStart + blockIdx.y], Word, OutputLength);
        if (Status & Applied) atomicAdd(&Counts[0], 1U);
        if (Status & Changed) atomicAdd(&Counts[1], 1U);
        if (Status & Rejected) atomicAdd(&Counts[3], 1U);
        if (Status & Syntax) atomicAdd(&Counts[4], 1U);
    }
    __syncthreads();
    if (threadIdx.x < 5)
        atomicAdd(&Totals[blockIdx.y].values[threadIdx.x],
            static_cast<unsigned long long>(Counts[threadIdx.x]));
}

bool Check(cudaError_t Status, const char* Operation, std::string& Error)
{
    if (Status == cudaSuccess) return true;
    Error = std::string(Operation) + ": " + cudaGetErrorString(Status);
    return false;
}

bool AddBytes(size_t Count, size_t Width, size_t& Total, std::string& Error)
{
    if (Count > (std::numeric_limits<size_t>::max() - Total) / Width)
    {
        Error = "CUDA resident data size exceeds addressable size_t range";
        return false;
    }
    Total += Count * Width;
    return true;
}

template <typename T> class DeviceBuffer
{
public:
    DeviceBuffer() = default;
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    ~DeviceBuffer() { if (data != nullptr) cudaFree(data); }

    bool Allocate(size_t Count, const char* Name, std::string& Error)
    {
        size_t Bytes = 0;
        if (!AddBytes(Count, sizeof(T), Bytes, Error)) return false;
        if (Bytes == 0) return true;
        if (!Check(cudaMalloc(reinterpret_cast<void**>(&data), Bytes), Name, Error))
        {
            Error += " (requested " + std::to_string(Bytes) + " bytes)";
            return false;
        }
        return true;
    }

    bool Upload(const T* Source, size_t Count, std::string& Error)
    {
        if (!Count) return true;
        return Check(cudaMemcpy(data, Source, Count * sizeof(T), cudaMemcpyHostToDevice),
            "Upload resident CUDA data", Error);
    }

    T* data = nullptr;
};

// Private streams allow overlap without synchronizing unrelated application
// work. Owning slots destroy their streams before their transfer buffers.
class Stream
{
public:
    Stream() = default;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    ~Stream()
    {
        if (value != nullptr)
        {
            cudaStreamSynchronize(value);
            cudaStreamDestroy(value);
        }
    }
    cudaStream_t value = nullptr;
};

template <typename T> class PinnedBuffer
{
public:
    PinnedBuffer() = default;
    PinnedBuffer(const PinnedBuffer&) = delete;
    PinnedBuffer& operator=(const PinnedBuffer&) = delete;
    ~PinnedBuffer() { if (data != nullptr) cudaFreeHost(data); }

    bool Allocate(size_t Count, std::string& Error)
    {
        size_t Bytes = 0;
        if (!AddBytes(Count, sizeof(T), Bytes, Error)) return false;
        return Check(cudaMallocHost(reinterpret_cast<void**>(&data), Bytes),
            "Allocate pinned CUDA transfer buffer", Error);
    }

    T* data = nullptr;
};

struct ExecutionSlot
{
    DeviceBuffer<Hit> hits;
    DeviceBuffer<Counters> counters;
    DeviceBuffer<unsigned> hitCount;
    DeviceBuffer<unsigned> active;
    PinnedBuffer<Hit> hostHits;
    PinnedBuffer<Counters> hostCounters;
    PinnedBuffer<unsigned> hostHitCount;
    PinnedBuffer<unsigned> hostActive;
    size_t inputStart = 0;
    size_t inputCount = 0;
    // Destroy the stream first, so queued operations finish before buffers free.
    Stream stream;

    bool Allocate(size_t Capacity, size_t BatchSize, std::string& Error)
    {
        return hits.Allocate(Capacity, "Allocate compact GPU hits", Error)
            && counters.Allocate(BatchSize, "Allocate GPU batch counters", Error)
            && hitCount.Allocate(1, "Allocate GPU hit count", Error)
            && active.Allocate(BatchSize, "Allocate GPU active-rule mask", Error)
            && hostHits.Allocate(Capacity, Error)
            && hostCounters.Allocate(BatchSize, Error)
            && hostHitCount.Allocate(1, Error)
            && hostActive.Allocate(BatchSize, Error)
            && Check(cudaStreamCreateWithFlags(&stream.value, cudaStreamNonBlocking),
                "Create pipelined CUDA stream", Error);
    }
};

void AddCounters(RuleStatistic& Statistic, size_t Evaluated, const Counters& Counts)
{
    Statistic.evaluated += Evaluated;
    Statistic.applied += static_cast<size_t>(Counts.values[0]);
    Statistic.changed += static_cast<size_t>(Counts.values[1]);
    Statistic.rejected += static_cast<size_t>(Counts.values[3]);
    Statistic.syntaxErrors += static_cast<size_t>(Counts.values[4]);
}

void Reset(RuleStatistic& S)
{
    S.matchedTargets.clear();
    S.evaluated = S.applied = S.changed = S.matches = S.rejected = S.syntaxErrors = 0;
    S.elapsedNanoseconds = 0;
    S.rank = S.marginalGain = S.cumulativeTargets = 0;
    S.qualified = false;
}

void Finish(RuleStatistic& S, AnalysisProgress* Progress,
    bool Qualification, uint64_t Elapsed)
{
    std::sort(S.matchedTargets.begin(), S.matchedTargets.end());
    S.elapsedNanoseconds = Elapsed;
    if (Progress == nullptr) return;
    if (Qualification)
    {
        auto& Count = S.qualified ? Progress->qualifiedRules : Progress->unqualifiedRules;
        Count.fetch_add(1, std::memory_order_relaxed);
    }
    Progress->completedEvaluations.fetch_add(S.evaluated, std::memory_order_relaxed);
    Progress->completedRules.fetch_add(1, std::memory_order_release);
}

} // namespace

bool CudaAvailable(std::string& Reason)
{
    Reason.clear();
    int Count = 0;
    if (!Check(cudaGetDeviceCount(&Count), "Query CUDA devices", Reason)) return false;
    if (Count == 0) { Reason = "No CUDA devices available"; return false; }
    int Device = 0;
    if (!Check(cudaGetDevice(&Device), "Query current CUDA device", Reason)) return false;
    cudaDeviceProp Properties{};
    if (!Check(cudaGetDeviceProperties(&Properties, Device), "Query CUDA device properties", Reason))
        return false;
    if (Properties.computeMode == cudaComputeModeProhibited)
    {
        Reason = "Current CUDA device prohibits compute access";
        return false;
    }
    cudaFuncAttributes Attributes{};
    if (!Check(cudaFuncGetAttributes(&Attributes, Evaluate), "Load CUDA rule kernel", Reason))
    {
        Reason += "; build native code for this GPU or include compatible virtual PTX";
        return false;
    }
    return true;
}

bool AnalyzeCuda(
    std::span<const std::string> Inputs, const TargetLookup& Targets,
    std::span<RuleStatistic> Statistics, size_t CpuThreads,
    AnalysisProgress* Progress, std::optional<size_t> QualificationBar,
    std::string& Error, const CudaOptions& Options)
{
    Error.clear();
    if (Options.bloomBitsPerTarget == 0 || Options.bloomBitsPerTarget > 64)
    {
        Error = "CUDA Bloom filter size must be between 1 and 64 bits per target";
        return false;
    }
    if (QualificationBar && *QualificationBar == 0)
    {
        Error = "CUDA qualification bar must be greater than zero";
        return false;
    }
    if (Statistics.empty()) return true;
    if (!CudaAvailable(Error)) return false;

    std::vector<Instruction> Instructions;
    std::vector<DeviceRule> DeviceRules;
    std::vector<size_t> GpuIndices;
    std::vector<size_t> CpuIndices;
    for (size_t i = 0; i < Statistics.size(); ++i)
    {
        const size_t Offset = Instructions.size();
        if (CompileDeviceRule(Statistics[i].compiledRule, Instructions))
        {
            DeviceRules.push_back({static_cast<uint64_t>(Offset),
                static_cast<uint32_t>(Instructions.size() - Offset)});
            GpuIndices.push_back(i);
        }
        else CpuIndices.push_back(i);
    }
    std::cerr << "CUDA rule analysis: " << GpuIndices.size() << " GPU rules, "
        << CpuIndices.size() << " CPU fallback rules (unsupported, saved-position, "
        "memory/class or malformed commands)\n";

    if (!GpuIndices.empty() && !Inputs.empty())
    {
        size_t PlaintextBytes = 0;
        for (const std::string& Input : Inputs)
            if (!AddBytes(Input.size(), 1, PlaintextBytes, Error)) return false;

        // Target IDs remain CPU index positions; packed byte addresses use 64 bits.
        if (Targets.Size() > static_cast<size_t>(NoTarget))
        {
            Error = "CUDA target count exceeds the uint32 target-ID space";
            return false;
        }
        std::vector<uint64_t> LengthCounts(TargetLookup::BUCKET_COUNT, 0);
        size_t TargetCount = 0;
        for (size_t Length = 0; Length < TargetLookup::BUCKET_COUNT; ++Length)
        {
            const auto& Bucket = Targets.Bucket(Length);
            if (Bucket.size() > static_cast<size_t>(NoTarget) - TargetCount)
            {
                Error = "CUDA target buckets exceed the uint32 target-ID space";
                return false;
            }
            LengthCounts[Length] = Bucket.size();
            TargetCount += Bucket.size();
        }
        if (TargetCount != Targets.Size())
        {
            Error = "CUDA analysis requires a finalized TargetLookup";
            return false;
        }

        size_t BloomBits = 0;
        if (!AddBytes(TargetCount, Options.bloomBitsPerTarget, BloomBits, Error)) return false;
        const size_t BloomWords = std::max<size_t>(1, BloomBits / 64 + (BloomBits % 64 != 0));
        const uint64_t BloomBitCount = static_cast<uint64_t>(BloomWords) * 64;
        const size_t UploadCapacity = std::min(BloomUploadChunk, TargetCount);

        const size_t BatchCapacity = std::min(RuleBatch, GpuIndices.size());
        const size_t ChunkCapacity = std::min(InputChunk, Inputs.size());
        const size_t PairCapacity = BatchCapacity * ChunkCapacity;
        size_t Required = 0;
        if (!AddBytes(PlaintextBytes, 1, Required, Error)
            || !AddBytes(Inputs.size(), sizeof(DeviceInput), Required, Error)
            || !AddBytes(BloomWords, sizeof(unsigned long long), Required, Error)
            || !AddBytes(UploadCapacity, sizeof(DeviceHash), Required, Error)
            || !AddBytes(LengthCounts.size(), sizeof(uint64_t), Required, Error)
            || !AddBytes(Instructions.size(), sizeof(Instruction), Required, Error)
            || !AddBytes(DeviceRules.size(), sizeof(DeviceRule), Required, Error)
            || !AddBytes(2 * PairCapacity, sizeof(Hit), Required, Error)
            || !AddBytes(2 * BatchCapacity, sizeof(Counters), Required, Error)
            || !AddBytes(2 * BatchCapacity + 2, sizeof(unsigned), Required, Error)) return false;
        size_t Free = 0, Total = 0;
        if (!Check(cudaMemGetInfo(&Free, &Total), "Query free CUDA memory", Error)) return false;
        if (Required > Free)
        {
            Error = "CUDA resident allocation requires " + std::to_string(Required)
                + " bytes, but only " + std::to_string(Free)
                + " are free (including the Bloom filter); no managed-memory oversubscription is used";
            return false;
        }
                std::cerr << "CUDA Bloom filter: " << BloomWords * sizeof(unsigned long long)
                                    << " bytes, " << Options.bloomBitsPerTarget << " bits/target, "
                                    << BloomProbes << " probes; exact 128-bit targets stay in host RAM.\n";

        std::vector<unsigned char> Plaintext;
        std::vector<DeviceInput> InputDescriptors;
        Plaintext.reserve(PlaintextBytes);
        InputDescriptors.reserve(Inputs.size());
        for (const std::string& Input : Inputs)
        {
            InputDescriptors.push_back({static_cast<uint64_t>(Plaintext.size()),
                static_cast<uint64_t>(Input.size())});
            Plaintext.insert(Plaintext.end(), Input.begin(), Input.end());
        }

        DeviceBuffer<unsigned char> GPlaintext;
        DeviceBuffer<DeviceInput> GInputs;
        DeviceBuffer<unsigned long long> GBloom;
        DeviceBuffer<DeviceHash> GUpload;
        DeviceBuffer<uint64_t> GLengthCounts;
        DeviceBuffer<Instruction> GInstructions;
        DeviceBuffer<DeviceRule> GRules;
        if (!GPlaintext.Allocate(Plaintext.size(), "Allocate resident plaintext", Error)
            || !GInputs.Allocate(InputDescriptors.size(), "Allocate resident input offsets", Error)
            || !GBloom.Allocate(BloomWords, "Allocate resident Bloom filter", Error)
            || !GUpload.Allocate(UploadCapacity, "Allocate bounded Bloom upload buffer", Error)
            || !GLengthCounts.Allocate(LengthCounts.size(), "Allocate target length counts", Error)
            || !GInstructions.Allocate(Instructions.size(), "Allocate resident rule instructions", Error)
            || !GRules.Allocate(DeviceRules.size(), "Allocate resident rule descriptors", Error)
            || !GPlaintext.Upload(Plaintext.data(), Plaintext.size(), Error)
            || !GInputs.Upload(InputDescriptors.data(), InputDescriptors.size(), Error)
            || !GLengthCounts.Upload(LengthCounts.data(), LengthCounts.size(), Error)
            || !GInstructions.Upload(Instructions.data(), Instructions.size(), Error)
            || !GRules.Upload(DeviceRules.data(), DeviceRules.size(), Error)
            || !Check(cudaStreamSynchronize(nullptr), "Finish resident data uploads", Error)) return false;

        Stream Work;
        if (!Check(cudaStreamCreateWithFlags(&Work.value, cudaStreamNonBlocking),
            "Create CUDA analysis stream", Error)) return false;
        if (!Check(cudaMemsetAsync(GBloom.data, 0,
                BloomWords * sizeof(unsigned long long), Work.value),
                "Clear CUDA Bloom filter", Error)
            || !Check(cudaStreamSynchronize(Work.value), "Initialize CUDA Bloom filter", Error))
            return false;

        std::cerr << "Building CUDA Bloom filter from " << TargetCount << " targets..." << std::flush;
        std::vector<DeviceHash> Upload;
        Upload.reserve(UploadCapacity);
        size_t Inserted = 0;
        auto LastBuildStatus = std::chrono::steady_clock::now();
        for (size_t Length = 0; Length < TargetLookup::BUCKET_COUNT; ++Length)
        {
            const auto& Bucket = Targets.Bucket(Length);
            for (size_t Start = 0; Start < Bucket.size();)
            {
                const size_t Count = std::min(UploadCapacity, Bucket.size() - Start);
                Upload.clear();
                for (size_t i = 0; i < Count; ++i)
                {
                    const Hash128 Hash = Bucket[Start + i];
                    Upload.push_back({Hash.high, Hash.low});
                }
                if (!Check(cudaMemcpyAsync(GUpload.data, Upload.data(),
                    Count * sizeof(DeviceHash), cudaMemcpyHostToDevice, Work.value),
                    "Upload Bloom construction chunk", Error)) return false;
                InsertBloom<<<static_cast<unsigned>((Count + BlockSize - 1) / BlockSize),
                    BlockSize, 0, Work.value>>>(GUpload.data, static_cast<unsigned>(Count),
                    static_cast<unsigned>(Length), GBloom.data, BloomBitCount);
                if (!Check(cudaGetLastError(), "Launch Bloom insertion", Error)
                    || !Check(cudaStreamSynchronize(Work.value), "Populate CUDA Bloom filter", Error))
                    return false;
                Start += Count;
                Inserted += Count;
                const auto Now = std::chrono::steady_clock::now();
                if (Now - LastBuildStatus >= std::chrono::milliseconds(500))
                {
                    std::cerr << '\r' << "Building CUDA Bloom filter: " << Inserted
                              << '/' << TargetCount << " targets..." << std::flush;
                    LastBuildStatus = Now;
                }
            }
        }
        std::cerr << " done\n";
        std::array<ExecutionSlot, 2> Slots;
        for (auto& Slot : Slots)
            if (!Slot.Allocate(PairCapacity, BatchCapacity, Error)) return false;
        std::cerr << "CUDA execution: " << BatchCapacity
                  << " rules/batch, compact hits, two pinned transfer buffers\n";
        uint64_t PotentialMatches = 0;
        uint64_t FalsePositives = 0;
        uint64_t TransferredBytes = 0;
        for (size_t RuleStart = 0; RuleStart < GpuIndices.size();)
        {
            const size_t BatchCount = std::min(BatchCapacity, GpuIndices.size() - RuleStart);
            std::vector<std::unordered_set<uint32_t>> Seen(BatchCount);
            std::vector<unsigned> Active(BatchCount, 1);
            for (size_t r = 0; r < BatchCount; ++r) Reset(Statistics[GpuIndices[RuleStart + r]]);
            const auto Start = std::chrono::steady_clock::now();

            auto LaunchChunk = [&](ExecutionSlot& Slot, size_t InputStart, size_t RequestedCount)
            {
                Slot.inputStart = InputStart;
                Slot.inputCount = std::min(RequestedCount, Inputs.size() - InputStart);
                std::copy(Active.begin(), Active.end(), Slot.hostActive.data);
                const cudaStream_t Stream = Slot.stream.value;
                if (!Check(cudaMemcpyAsync(Slot.active.data, Slot.hostActive.data,
                        BatchCount * sizeof(unsigned), cudaMemcpyHostToDevice, Stream),
                        "Upload active-rule mask", Error)
                    || !Check(cudaMemsetAsync(Slot.counters.data, 0,
                        BatchCount * sizeof(Counters), Stream), "Reset CUDA counters", Error)
                    || !Check(cudaMemsetAsync(Slot.hitCount.data, 0, sizeof(unsigned), Stream),
                        "Reset compact-hit count", Error)) return false;
                const dim3 Grid(static_cast<unsigned>((Slot.inputCount + BlockSize - 1) / BlockSize),
                    static_cast<unsigned>(BatchCount));
                Evaluate<<<Grid, BlockSize, 0, Stream>>>(GPlaintext.data, GInputs.data,
                    GInstructions.data, GRules.data, GBloom.data, BloomBitCount, GLengthCounts.data,
                    InputStart, RuleStart, static_cast<unsigned>(Slot.inputCount),
                    Slot.active.data, Slot.hits.data, Slot.hitCount.data,
                    static_cast<unsigned>(PairCapacity), Slot.counters.data);
                TransferredBytes += BatchCount * sizeof(Counters) + sizeof(unsigned);
                return Check(cudaGetLastError(), "Launch compact CUDA rule batch", Error)
                    && Check(cudaMemcpyAsync(Slot.hostHitCount.data, Slot.hitCount.data,
                        sizeof(unsigned), cudaMemcpyDeviceToHost, Stream),
                        "Download compact-hit count", Error)
                    && Check(cudaMemcpyAsync(Slot.hostCounters.data, Slot.counters.data,
                        BatchCount * sizeof(Counters), cudaMemcpyDeviceToHost, Stream),
                        "Download CUDA counters", Error);
            };

            const size_t InitialChunk = QualificationBar
                ? std::min(InitialQualificationChunk, ChunkCapacity)
                : ChunkCapacity;
            if (!LaunchChunk(Slots[0], 0, InitialChunk)) return false;
            size_t CurrentSlot = 0;
            while (true)
            {
                ExecutionSlot& Slot = Slots[CurrentSlot];
                if (!Check(cudaStreamSynchronize(Slot.stream.value),
                    "Wait for compact CUDA results", Error)) return false;
                const unsigned HitCount = *Slot.hostHitCount.data;
                if (HitCount > PairCapacity)
                {
                    Error = "CUDA compact-hit capacity exceeded; no results have been silently dropped";
                    return false;
                }
                if (HitCount != 0 && !Check(cudaMemcpyAsync(Slot.hostHits.data, Slot.hits.data,
                    static_cast<size_t>(HitCount) * sizeof(Hit), cudaMemcpyDeviceToHost,
                    Slot.stream.value), "Download compact Bloom hits", Error)) return false;
                TransferredBytes += static_cast<uint64_t>(HitCount) * sizeof(Hit);

                // One-chunk lookahead: the GPU works on the next chunk while
                // this chunk is downloaded, sorted and exactly verified by CPU.
                // Newly qualified rules may have one speculative extra chunk;
                // its results are ignored and never enter logical statistics.
                const size_t NextInput = Slot.inputStart + Slot.inputCount;
                const bool NextSubmitted = NextInput < Inputs.size();
                const size_t NextChunk = std::min(ChunkCapacity, Slot.inputCount * 2);
                if (NextSubmitted
                    && !LaunchChunk(Slots[1 - CurrentSlot], NextInput, NextChunk)) return false;
                if (!Check(cudaStreamSynchronize(Slot.stream.value),
                    "Wait for compact-hit transfer", Error)) return false;

                std::sort(Slot.hostHits.data, Slot.hostHits.data + HitCount,
                    [](const Hit& Left, const Hit& Right)
                    {
                        if (Left.ruleSlot != Right.ruleSlot) return Left.ruleSlot < Right.ruleSlot;
                        return Left.inputIndex < Right.inputIndex;
                    });
                std::vector<unsigned> PrefixLengths(BatchCount, 0);
                for (unsigned i = 0; i < HitCount; ++i)
                {
                    const Hit& Result = Slot.hostHits.data[i];
                    if (Result.ruleSlot >= BatchCount || Result.inputIndex >= Slot.inputCount)
                    {
                        Error = "CUDA returned an invalid compact-hit coordinate";
                        return false;
                    }
                    const size_t r = Result.ruleSlot;
                    if (!Active[r]) continue;
                    ++PotentialMatches;
                    const auto Target = Targets.FindFingerprint(Result.length,
                        {Result.hash.high, Result.hash.low});
                    if (!Target) { ++FalsePositives; continue; }
                    RuleStatistic& S = Statistics[GpuIndices[RuleStart + r]];
                    ++S.matches;
                    if (Seen[r].insert(*Target).second)
                    {
                        S.matchedTargets.push_back(*Target);
                        if (QualificationBar && S.matchedTargets.size() >= *QualificationBar)
                        {
                            S.qualified = true;
                            Active[r] = 0;
                            PrefixLengths[r] = Result.inputIndex + 1;
                        }
                    }
                }

                // Full-chunk counters are valid only for rules that did not
                // qualify in this chunk; qualified prefixes are recounted below.
                for (size_t r = 0; r < BatchCount; ++r)
                {
                    if (Active[r])
                        AddCounters(Statistics[GpuIndices[RuleStart + r]],
                            Slot.inputCount, Slot.hostCounters.data[r]);
                }
                const bool NeedsPrefixCounts = std::any_of(PrefixLengths.begin(), PrefixLengths.end(),
                    [](unsigned Length) { return Length != 0; });
                if (NeedsPrefixCounts)
                {
                    std::copy(PrefixLengths.begin(), PrefixLengths.end(), Slot.hostActive.data);
                    const cudaStream_t Stream = Slot.stream.value;
                    if (!Check(cudaMemcpyAsync(Slot.active.data, Slot.hostActive.data,
                            BatchCount * sizeof(unsigned), cudaMemcpyHostToDevice, Stream),
                            "Upload qualifying prefix lengths", Error)
                        || !Check(cudaMemsetAsync(Slot.counters.data, 0,
                            BatchCount * sizeof(Counters), Stream), "Reset prefix counters", Error))
                        return false;
                    const dim3 Grid(static_cast<unsigned>((Slot.inputCount + BlockSize - 1) / BlockSize),
                        static_cast<unsigned>(BatchCount));
                    CountPrefixes<<<Grid, BlockSize, 0, Stream>>>(GPlaintext.data, GInputs.data,
                        GInstructions.data, GRules.data, Slot.inputStart, RuleStart,
                        Slot.active.data, Slot.counters.data);
                    if (!Check(cudaGetLastError(), "Launch qualifying prefix recount", Error)
                        || !Check(cudaMemcpyAsync(Slot.hostCounters.data, Slot.counters.data,
                            BatchCount * sizeof(Counters), cudaMemcpyDeviceToHost, Stream),
                            "Download prefix counters", Error)
                        || !Check(cudaStreamSynchronize(Stream), "Finish prefix recount", Error))
                        return false;
                    TransferredBytes += BatchCount * sizeof(Counters);
                    for (size_t r = 0; r < BatchCount; ++r)
                    {
                        if (PrefixLengths[r] == 0) continue;
                        RuleStatistic& S = Statistics[GpuIndices[RuleStart + r]];
                        AddCounters(S, PrefixLengths[r], Slot.hostCounters.data[r]);
                        const uint64_t Elapsed = static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - Start).count()) / BatchCount;
                        Finish(S, Progress, true, Elapsed);
                    }
                }
                if (!NextSubmitted || std::none_of(Active.begin(), Active.end(),
                    [](unsigned Value) { return Value != 0; })) break;
                CurrentSlot = 1 - CurrentSlot;
            }

            // Drain the single speculative chunk before buffers and pinned masks
            // are reused for a different rule batch.
            for (auto& Slot : Slots)
                if (!Check(cudaStreamSynchronize(Slot.stream.value),
                    "Drain speculative CUDA work", Error)) return false;
            // GPU rules run concurrently: report amortized batch wall time,
            // including transfer and deduplication, not misleading CPU timings.
            const uint64_t Elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - Start).count()) / BatchCount;
            for (size_t r = 0; r < BatchCount; ++r)
                if (Active[r])
                    Finish(Statistics[GpuIndices[RuleStart + r]], Progress,
                        QualificationBar.has_value(), Elapsed);
            RuleStart += BatchCount;
        }
        std::cerr << "CUDA Bloom verification: " << PotentialMatches
                  << " potential hits, " << FalsePositives
                  << " false positives discarded; " << TransferredBytes
                  << " result bytes downloaded\n";
    }
    else
    {
        // Empty inputs still reset/finalize every supported rule, as CPU Analyze does.
        for (const size_t Index : GpuIndices)
        {
            Reset(Statistics[Index]);
            Finish(Statistics[Index], Progress, QualificationBar.has_value(), 0);
        }
    }

    // One CPU Analyze call preserves its multithreaded scheduling for all
    // unsupported rules. Keep the original rules/metadata and move back results.
    if (!CpuIndices.empty())
    {
        std::vector<RuleStatistic> Fallback;
        Fallback.reserve(CpuIndices.size());
        for (const size_t Index : CpuIndices) Fallback.push_back(Statistics[Index]);
        Analyze(Inputs, Targets, Fallback, CpuThreads, Progress, QualificationBar);
        for (size_t i = 0; i < CpuIndices.size(); ++i)
            Statistics[CpuIndices[i]] = std::move(Fallback[i]);
    }
    return true;
}

} // namespace RuleAnalysis