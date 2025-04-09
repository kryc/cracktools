//
//  Check.hpp
//  CrackTools
//
//  Created by Kryc on 09/04/2025.
//  Copyright © 2025 Kryc. All rights reserved.
//

#ifndef Check_hpp
#define Check_hpp

#include <cstdlib>
#include <iostream>

namespace cracktools
{

inline static void
CheckImpl(
    const bool Condition,
    const char* Message,
    const char* File,
    const int Line
)
{
    if (!Condition)
    {
        std::cerr << "Check failed: " << Message << " at " << File << ":" << Line << std::endl;
        std::abort();
    }
}

} // namespace cracktools

#define CHECK(condition, message) \
    cracktools::CheckImpl(condition, message, __FILE__, __LINE__)

// DCHECK if debug build
#ifdef DEBUG
#define DCHECK(condition, message) \
    cracktools::CheckImpl(condition, message, __FILE__, __LINE__)
#else
#define DCHECK(condition, message)
#endif

#endif /* Check_hpp */