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
#include <string>
#include <vector>

namespace cracktools
{

#pragma clang unsafe_buffer_usage begin

// A wimple wrapper around the two argument span constructor
// to allow for unsafe buffer usage
template <
    typename T,
    std::size_t Extent = std::dynamic_extent>
inline static
std::span<T, Extent> UnsafeSpan(
    T* Base,
    std::size_t Size
) {
    return std::span<T, Extent>(Base, Size);
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

#pragma clang unsafe_buffer_usage end

} // namespace cracktools

#endif /* UnsafeBuffer_hpp */