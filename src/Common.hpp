//
//  Common.hpp
//  CrackDB++
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#ifndef Common_hpp
#define Common_hpp

#include <openssl/md5.h>
#include <openssl/sha.h>
#include <string>
#include <vector>

#include "simdhash.h"

inline void
DoHash(
    const HashAlgorithm Algorithm,
    const uint8_t* Data,
    const size_t Length,
    uint8_t* Digest
)
{
    switch (Algorithm)
    {
        case HashAlgorithmMD5:
            MD5(Data, Length, Digest);
            break;
        case HashAlgorithmSHA1:
            SHA1(Data, Length, Digest);
            break;
        case HashAlgorithmSHA256:
            SHA256(Data, Length, Digest);
            break;
        default:
            break;
    }
}

#endif /* Common_hpp */