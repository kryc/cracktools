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

#define CHECKA(condition, message) \
    cracktools::CheckImpl(condition, message, __FILE__, __LINE__)
#define CHECK(condition) \
    cracktools::CheckImpl(condition, #condition, __FILE__, __LINE__)

// DCHECK if debug build
#ifdef DEBUG
#define DCHECKA(condition, message) \
    cracktools::CheckImpl(condition, message, __FILE__, __LINE__)
#define DCHECK(condition) \
    cracktools::CheckImpl(condition, #condition, __FILE__, __LINE__)
#else
#define DCHECKA(condition, message)
#define DCHECK(condition)
#endif

#endif /* Check_hpp */