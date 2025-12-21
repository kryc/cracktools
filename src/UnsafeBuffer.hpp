//
//  UnsafeBuffer.hpp
//  CrackTools
//
//  Created by Kryc on 01/04/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#ifndef UnsafeBuffer_hpp
#define UnsafeBuffer_hpp

#include <bit>
#include <span>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <optional>
#include <optional>
#include <string_view>
#include <sys/mman.h>
#include <vector>

#include "Check.hpp"

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

template <
    typename T,
    typename T2,
    std::size_t Extent = std::dynamic_extent>
inline static
std::span<T, Extent> UnsafeSpanEx(
    T2* Base,
    std::size_t Size
)
{
    CHECKA(sizeof(T) == sizeof(T2), "Types must be the same size for UnsafeSpanEx");
    return std::span<T, Extent>((T*)Base, Size);
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
    CHECKA(Span.size_bytes() % sizeof(T) == 0, "Span size not a multiple of T");
    DCHECKA(Span.data() != nullptr, "Span data is null");
    return std::span<T>((T*)Span.data(), Span.size_bytes() / sizeof(T));
}

template <typename T>
inline static
std::span<T> SpanCast(
    std::string_view String
)
{
    CHECKA(String.size() % sizeof(T) == 0, "String size not a multiple of T");
    DCHECKA(String.data() != nullptr, "String data is null");
    return std::span<T>((T*)String.data(), String.size() / sizeof(T));
}

template <typename T, typename T2=const uint8_t>
inline static
std::span<T2>
AsBytes(
    std::span<T> Span
)
{
    return std::span<T2>((T2*)Span.data(), Span.size_bytes());
}

template <typename T=const uint8_t>
inline static
std::span<T>
AsBytes(
    std::string_view String
)
{
    return std::span<T>((T*)String.data(), String.size() * sizeof(std::string_view::value_type));
}

template <typename T>
inline static
std::span<uint8_t>
AsWritableBytes(
    std::span<T> Span
)
{
    return AsBytes<uint8_t>(Span);
}

template <typename T, typename T2=const char>
inline static
std::span<T2>
AsChars(
    std::span<T> Span
)
{
    return std::span<T2>((T2*)Span.data(), Span.size_bytes());
}

template <typename T>
inline static
std::span<char>
AsWritableChars(
    std::span<T> Span
)
{
    return AsChars<char>(Span);
}

inline static
std::string_view
AsStringView(
    std::span<const char> Span
)
{
    return std::string_view(Span.data(), Span.size());
}

inline static
std::string_view
AsStringView(
    std::span<const uint8_t> Span
)
{
    return std::string_view((const char*)Span.data(), Span.size());
}

template <typename T>
inline static
std::string
AsString(
    std::span<T> Span
)
{
    return std::string((char*)Span.data(), Span.size_bytes());
}

inline static uint32_t
LoadUint32Native(
    std::span<const uint8_t> Span
)
{
    CHECKA(Span.size() >= sizeof(uint32_t), "Span size is less than uint32_t");
    return *(uint32_t*)Span.data();
}

inline static uint32_t
LoadUint32Native(
    const std::string_view String
)
{
    CHECKA(String.size() >= sizeof(uint32_t), "String size is less than uint32_t");
    return *(uint32_t*)String.data();
}

template <typename T>
inline static uint32_t
LoadUint32LittleEndian(
    T Value
)
{
    if (std::endian::native == std::endian::little)
    {
        return LoadUint32Native(Value);
    }
    else
    {
        return std::byteswap(LoadUint32Native(Value));
    }
}

template <typename T>
inline static uint32_t
LoadUint32BigEndian(
    T Value
)
{
    if (std::endian::native == std::endian::little)
    {
        return std::byteswap(LoadUint32Native(Value));
    }
    else
    {
        return LoadUint32Native(Value);
    }
}

inline static uint64_t
LoadUint64Native(
    std::span<const uint8_t> Span
)
{
    CHECKA(Span.size() >= sizeof(uint64_t), "Span size is less than uint64_t");
    return *(uint64_t*)Span.data();
}

inline static uint64_t
LoadUint64Native(
    const std::string_view String
)
{
    CHECKA(String.size() >= sizeof(uint64_t), "String size is less than uint64_t");
    return *(uint64_t*)String.data();
}

template <typename T>
inline static uint64_t
LoadUint64LittleEndian(
    T Value
)
{
    if (std::endian::native == std::endian::little)
    {
        return LoadUint64Native(Value);
    }
    else
    {
        return std::byteswap(LoadUint64Native(Value));
    }
}

template <typename T>
inline static uint64_t
LoadUint64BigEndian(
    T Value
)
{
    if (std::endian::native == std::endian::little)
    {
        return std::byteswap(LoadUint64Native(Value));
    }
    else
    {
        return LoadUint64Native(Value);
    }
}

inline static __uint128_t
LoadUint128Native(
    std::span<const uint8_t> Span
)
{
    CHECKA(Span.size() >= sizeof(__uint128_t), "Span size is less than uint128_t");
    return *(__uint128_t*)Span.data();
}

inline static __uint128_t
LoadUint128Native(
    const std::string_view String
)
{
    CHECKA(String.size() >= sizeof(__uint128_t), "String size is less than uint128_t");
    return *(__uint128_t*)String.data();
}

template <typename T>
inline static __uint128_t
LoadUint128LittleEndian(
    T Value
)
{
    if (std::endian::native == std::endian::little)
    {
        return LoadUint128Native(Value);
    }
    else
    {
        return std::byteswap(LoadUint128Native(Value));
    }
}

template <typename T>
inline static __uint128_t
LoadUint128BigEndian(
    T Value
)
{
    if (std::endian::native == std::endian::little)
    {
        return std::byteswap(LoadUint128Native(Value));
    }
    else
    {
        return LoadUint128Native(Value);
    }
}

template <typename T>
inline static void
SpanCopy(
    std::span<T> Destination,
    std::span<const T> Source
)
{
    CHECKA(Destination.size() == Source.size(), "Destination and source size mismatch");
    std::memcpy(Destination.data(), Source.data(), Source.size_bytes());
}

inline static void
SpanCopy(
    std::span<char> Destination,
    std::string_view Source,
    const size_t Length
)
{
    CHECKA(Destination.size() >= Length, "Destination size is less than length");
    CHECKA(Source.size() >= Length, "Source size is less than length");
    std::memcpy(Destination.data(), Source.data(), Length);
}

inline static void
SpanCopy(
    std::span<char> Destination,
    std::string_view Source
)
{
    return SpanCopy(Destination, Source, Source.size());
}

// A function to parse argv into a vector of strings
inline static
const std::vector<std::string_view> ParseArgv(
    const char* Argv[],
    const int Argc
)
{
    std::vector<std::string_view> Args;
    Args.reserve(Argc);
    for (int i = 0; i < Argc; ++i) {
        Args.emplace_back(Argv[i]);
    }
    return Args;
}

inline static
const int Memcmp(
    std::span<const uint8_t> Span1,
    std::span<const uint8_t> Span2
)
{
    CHECKA(Span1.size() == Span2.size(), "Span sizes do not match for Memcmp");
    return std::memcmp(Span1.data(), Span2.data(), Span1.size());
}

inline static
const int Memcmp(
    const uint8_t* const Ptr1,
    const uint8_t* const Ptr2,
    const size_t Size
)
{
    CHECKA(Size > 0, "Size is zero for Memcmp");
    return std::memcmp(Ptr1, Ptr2, Size);
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

// A wrapper around std::istream::read
inline static
size_t ReadStream(
    std::istream* Stream,
    std::span<char> Buffer,
    const size_t Length = 0,
    const size_t Offset = 0
)
{
    CHECKA(Offset + Length <= Buffer.size(), "Buffer size is less than length + offset");
    Stream->read(Buffer.data() + Offset, Length);
    return Stream->gcount();
}

#pragma clang unsafe_buffer_usage end

} // namespace cracktools

#endif /* UnsafeBuffer_hpp */