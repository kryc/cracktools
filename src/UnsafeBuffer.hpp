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

namespace cracktools
{

#pragma clang unsafe_buffer_usage begin

// A wimple wrapper around the two argument span constructor
// to allow for unsafe buffer usage
template <
    typename T,
    std::size_t Extent = std::dynamic_extent
>
std::span<T, Extent> UnsafeSpan(
    T* Base,
    std::size_t Size
) {
    return std::span<T, Extent>(Base, Size);
}

#pragma clang unsafe_buffer_usage end

} // namespace cracktools

#endif /* UnsafeBuffer_hpp */