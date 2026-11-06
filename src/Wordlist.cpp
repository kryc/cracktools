#include "Wordlist.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "DispatchPool.hpp"
#include "LineReader.hpp"
#include "UnsafeBuffer.hpp"
#include "Util.hpp"

namespace Wordlist
{
namespace
{

struct Task
{
    std::string_view Bytes;
    size_t File;
};

struct Record
{
    std::string Word;
    size_t File;
};

struct Partition
{
    std::mutex Mutex;
    std::vector<Record> Records;
    std::vector<std::string> Output;
};

template <typename Function>
bool Parallel(dispatch::DispatchPool& Pool, const size_t Count, const size_t Threads, const Function& Work)
{
    std::atomic<size_t> Next = 0;
    const auto Worker = [&]
    {
        for (;;)
        {
            const size_t Index = Next.fetch_add(1, std::memory_order_relaxed);
            if (Index >= Count) break;
            Work(Index);
        }
    };
    const size_t WorkerCount = std::min(Count, Threads);
    // Queue worker loops, not individual words/chunks, to retain dynamic balancing.
    for (size_t i = 0; i < WorkerCount; ++i)
    {
        if (!Pool.TryPostTask(Worker))
        {
            Pool.Stop();
            Pool.Wait();
            return false;
        }
    }
    // Fence all captures before Next and the caller's Work temporary are destroyed.
    if (Pool.WaitIdle()) return true;
    Pool.Stop();
    Pool.Wait();
    return false;
}

std::vector<std::string> SplitVariants(std::vector<std::string> Words, const char Separator)
{
    std::vector<std::string> Result;
    for (auto& Word : Words)
    {
        if (Word.find(Separator) == std::string::npos)
        {
            Result.push_back(std::move(Word));
            continue;
        }
        Result.push_back(Util::RemoveCharacter(Word, Separator));
        // Split without copying tokens; retain owned results before Word is destroyed.
        for (const std::string_view Field : Util::Split(Word, Separator))
        {
            Result.emplace_back(Field);
        }
    }
    return Result;
}

std::vector<std::string> Transform(const std::string_view Input, const Operation Action)
{
    std::string Word;
    if (Action == Operation::Lower || Action == Operation::Clean) Word = Util::ToLower(Input);
    else if (Action == Operation::Upper) Word = Util::ToUpper(Input);
    else if (Action == Operation::Title) Word = Util::ToTitle(Input);
    else Word = Input;
    if (Action == Operation::Clean && Word.ends_with("'s"))
    {
        Word.resize(Word.size() - 2);
        Word.push_back('s');
    }
    std::vector<std::string> Words;
    Words.push_back(std::move(Word));
    if (Action == Operation::Clean)
    {
        // TODO(ICU): Add Unicode lowercase and transliterated variants here.
        // Non-ASCII bytes are currently preserved, not decoded or transliterated.
        Words = SplitVariants(std::move(Words), '-');
        Words = SplitVariants(std::move(Words), ' ');
        // Preserve the script's ^[^a-z]$ behavior: only single invalid characters.
        std::erase_if(Words, [](const auto& W)
        {
            return W.empty() || (W.size() == 1 && !Util::IsLower(W.front()));
        });
    }
    return Words;
}

void ReadTask(const Task& Input, const Options& Settings,
    std::span<const std::unique_ptr<Partition>> Partitions)
{
    // Worker-local batches bound staging memory. Only transfers take a partition lock;
    // no worker mutates another worker's words, and reduction begins after a barrier.
    std::vector<std::vector<Record>> Batches(Partitions.size());
    size_t Pending = 0;
    const auto Flush = [&]
    {
        for (size_t i = 0; i < Batches.size(); ++i)
        {
            auto& Batch = Batches[i];
            if (Batch.empty()) continue;
            auto& Destination = *Partitions[i];
            std::lock_guard Lock(Destination.Mutex);
            Destination.Records.insert(Destination.Records.end(),
                std::make_move_iterator(Batch.begin()), std::make_move_iterator(Batch.end()));
            Batch.clear();
        }
        Pending = 0;
    };
    size_t Start = 0;
    while (Start < Input.Bytes.size())
    {
        const size_t End = Input.Bytes.find_first_of("\r\n", Start);
        const auto Line = Input.Bytes.substr(Start,
            End == std::string_view::npos ? End : End - Start);
        if (Settings.IncludeComments || !Line.starts_with('#'))
        {
            // UnHexlify preserves malformed wrappers and literal empty $HEX[].
            auto Words = Transform(Util::UnHexlify(Line), Settings.Action);
            for (auto& Word : Words)
            {
                const size_t Bucket = std::hash<std::string_view>{}(Word) % Partitions.size();
                Batches[Bucket].push_back({std::move(Word), Input.File});
                if (++Pending >= 4096) Flush();
            }
        }
        if (End == std::string_view::npos) break;
        Start = End + 1;
        if (Input.Bytes[End] == '\r' && Start < Input.Bytes.size() && Input.Bytes[Start] == '\n')
            ++Start;
    }
    Flush();
}

void Reduce(Partition& Bucket, const Operation Action, const size_t FileCount)
{
    auto& Records = Bucket.Records;
    std::sort(Records.begin(), Records.end(), [](const auto& Left, const auto& Right)
    {
        return Left.Word < Right.Word || (Left.Word == Right.Word && Left.File < Right.File);
    });
    for (size_t Start = 0; Start < Records.size();)
    {
        size_t End = Start + 1;
        size_t Membership = 1;
        while (End < Records.size() && Records[End].Word == Records[Start].Word)
        {
            if (Records[End].File != Records[End - 1].File) ++Membership;
            ++End;
        }
        const bool Keep = Action == Operation::Difference
            ? Records[Start].File == 0 && Membership == 1
            : Action == Operation::Intersection ? Membership == FileCount
            : Action == Operation::SymmetricDifference ? Membership % 2 == 1
            : true;
        if (Keep) Bucket.Output.push_back(Util::Hexlify(Records[Start].Word));
        Start = End;
    }
    std::vector<Record>().swap(Records);
    // Sort the final, project-standard escaped output values.
    std::sort(Bucket.Output.begin(), Bucket.Output.end());
}

bool Write(std::span<const std::unique_ptr<Partition>> Partitions,
    std::ostream& Output, std::string& Error)
{
    struct Cursor { size_t Partition; size_t Index; };
    const auto Later = [&](const Cursor& Left, const Cursor& Right)
    {
        return Partitions[Left.Partition]->Output[Left.Index]
            > Partitions[Right.Partition]->Output[Right.Index];
    };
    std::priority_queue<Cursor, std::vector<Cursor>, decltype(Later)> Heap(Later);
    for (size_t i = 0; i < Partitions.size(); ++i)
        if (!Partitions[i]->Output.empty()) Heap.push({i, 0});
    std::string Buffer;
    const auto Flush = [&]
    {
        Output.write(Buffer.data(), static_cast<std::streamsize>(Buffer.size()));
        Buffer.clear();
        return Output.good();
    };
    while (!Heap.empty())
    {
        auto Position = Heap.top();
        Heap.pop();
        const auto& Words = Partitions[Position.Partition]->Output;
        Buffer += Words[Position.Index];
        Buffer.push_back('\n');
        if (++Position.Index < Words.size()) Heap.push(Position);
        if (Buffer.size() >= 65536 && !Flush())
        {
            Error = "Unable to write wordlist output.";
            return false;
        }
    }
    if (!Flush())
    {
        Error = "Unable to write wordlist output.";
        return false;
    }
    Output.flush();
    if (!Output.good())
    {
        Error = "Unable to flush wordlist output.";
        return false;
    }
    return true;
}

}

std::optional<Operation> ParseOperation(const std::string_view Name)
{
    if (Name == "union") return Operation::Union;
    if (Name == "difference") return Operation::Difference;
    if (Name == "intersection") return Operation::Intersection;
    if (Name == "symmetric-difference") return Operation::SymmetricDifference;
    if (Name == "lower") return Operation::Lower;
    if (Name == "upper") return Operation::Upper;
    if (Name == "title") return Operation::Title;
    if (Name == "clean") return Operation::Clean;
    return std::nullopt;
}

bool Process(const std::span<const std::filesystem::path> Files,
    const Options& Settings, std::ostream& Output, std::string& Error)
{
    Error.clear();
    if (Files.empty())
    {
        Error = "At least one input file is required.";
        return false;
    }
    const size_t Threads = Settings.Threads == 0
        ? std::max(1u, std::thread::hardware_concurrency()) : Settings.Threads;
    std::vector<std::unique_ptr<MmapLineReader>> Mappings;
    std::vector<Task> Tasks;
    for (size_t File = 0; File < Files.size(); ++File)
    {
        auto Mapping = std::make_unique<MmapLineReader>();
        if (!Mapping->SetInputFile(Files[File].string()))
        {
            Error = "Unable to map regular input file: " + Files[File].string();
            return false;
        }
        const auto Bytes = Mapping->View();
        const size_t ChunkSize = std::clamp(Bytes.size() / Threads, size_t{65536}, size_t{4 * 1024 * 1024});
        for (size_t Start = 0; Start < Bytes.size();)
        {
            size_t End = Start + std::min(ChunkSize, Bytes.size() - Start);
            if (End < Bytes.size())
            {
                // Align on either newline form, without splitting CRLF. Ranges are
                // disjoint, including very long lines and an unterminated last line.
                End = Bytes.find_first_of("\r\n", End);
                if (End == std::string_view::npos) End = Bytes.size();
                else
                {
                    const char Newline = Bytes[End++];
                    if (Newline == '\r' && End < Bytes.size() && Bytes[End] == '\n') ++End;
                }
            }
            Tasks.push_back({Bytes.substr(Start, End - Start), File});
            Start = End;
        }
        Mappings.push_back(std::move(Mapping));
    }
    const size_t PartitionCount = std::min(size_t{256}, std::min(Threads, size_t{64}) * 4);
    std::vector<std::unique_ptr<Partition>> Partitions;
    for (size_t i = 0; i < PartitionCount; ++i) Partitions.push_back(std::make_unique<Partition>());
    // One unregistered pool per invocation: avoid global names and reuse threads
    // across reading and reduction. Do not spawn more workers than either phase can use.
    const size_t WorkerCount = std::min(Threads, std::max(Tasks.size(), Partitions.size()));
    dispatch::DispatchPool Pool("wordlist", WorkerCount);
    if (!Parallel(Pool, Tasks.size(), WorkerCount,
        [&](size_t Index) { ReadTask(Tasks[Index], Settings, Partitions); }))
    {
        Error = "Unable to dispatch wordlist input processing.";
        return false;
    }
    // Views are no longer needed: all records own their decoded/transformed bytes.
    Mappings.clear();
    if (!Parallel(Pool, Partitions.size(), WorkerCount,
        [&](size_t Index) { Reduce(*Partitions[Index], Settings.Action, Files.size()); }))
    {
        Error = "Unable to dispatch wordlist reduction.";
        return false;
    }
    Pool.Drain();
    if (!Pool.Wait())
    {
        Error = "Unable to join wordlist workers.";
        return false;
    }
    return Write(Partitions, Output, Error);
}

}