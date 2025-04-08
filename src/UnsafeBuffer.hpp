//
//  UnsafeBuffer.hpp
//  CrackTools
//
//  Created by Kryc on 01/04/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#ifndef UnsafeBuffer_hpp
#define UnsafeBuffer_hpp

#include <span>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <optional>
#include <optional>
#include <string_view>
#include <sys/mman.h>
#include <vector>

namespace cracktools
{

#pragma clang unsafe_buffer_usage begin

// A simple wrapper around the two argument span constructor
// to allow for unsafe buffer usage
template <
    typename T,
    std::size_t Extent = std::dynamic_extent>
inline static
std::span<T, Extent> UnsafeSpan(
    T* Base,
    std::size_t Size
)
{
    return std::span<T, Extent>(Base, Size);
}

// A simple wrapper to make a span from a string view
template <typename T>
inline static
std::span<T> UnsafeSpan(
    std::string_view StringView
)
{
    return std::span<T>(StringView.data(), StringView.size());
}

template <typename T, typename T2>
inline static
std::span<T> SpanCast(
    std::span<T2> Span
)
{
    assert(Span.size_bytes() % sizeof(T) == 0);
    return std::span<T>((T*)Span.data(), Span.size_bytes() / sizeof(T));
}

// A function to parse argv into a vector of strings
inline static
const std::vector<std::string> ParseArgv(
    const char* Argv[],
    const int Argc
)
{
    std::vector<std::string> Args;
    Args.reserve(Argc);
    for (int i = 0; i < Argc; ++i) {
        Args.emplace_back(Argv[i]);
    }
    return Args;
}

// A wrapper around mmap given a type returning an optional
// tuple of span and file descriptor.
// Will check that the filesize is a multiple of the type size
template <typename T>
inline static
std::optional<std::tuple<std::span<T>, FILE*>>
MmapFileSpan(
    const std::filesystem::path Path,
    const int Prot = PROT_READ,
    const int Flags = MAP_SHARED,
    const bool Madvise = false
)
{
    constexpr char kFileRead[] = "r";
    constexpr char kFileWrite[] = "r+";
    const size_t size = std::filesystem::file_size(Path);
    const size_t count = size / sizeof(T);
    const char* mode = (Prot == PROT_READ) ? kFileRead : kFileWrite;

    if (size % sizeof(T) != 0)
    {
        return std::nullopt;
    }

    // Check if opening read or write
    FILE* fd = fopen(Path.c_str(), mode);
    if (fd == nullptr)
    {
        return std::nullopt;
    }

    // Mmap the file
    T* base = (T*)mmap(nullptr, size, Prot, Flags, fileno(fd), 0);
    if (base == MAP_FAILED)
    {
        fclose(fd);
        return std::nullopt;
    }

    // Inform the kernel that we will be performing random accesses at all offsets
    if (Madvise)
    {
        // We don't warn or fail if madvise fails
        (void) madvise((void*)base, size, MADV_RANDOM | MADV_WILLNEED);
    }

    // Create a span from the base pointer and size
    std::span<T> span(base, count);
    return std::make_tuple(span, fd);
}

// A wrapper around munmap given the span.
// Takes a reference so it can invalidate it afterwards
template <typename T>
inline static
bool UnmapFileSpan(
    std::span<T>& Span,
    FILE*& Fd
)
{
    int result = 0;
    if (Span.data() != nullptr)
    {
        result = munmap((void*)Span.data(), Span.size_bytes());
    }
    if (Fd != nullptr)
    {
        fclose(Fd);
        Fd = nullptr;
    }
    Span = std::span<T>();
    return result == 0;
}

#pragma clang unsafe_buffer_usage end

} // namespace cracktools

#endif /* UnsafeBuffer_hpp */