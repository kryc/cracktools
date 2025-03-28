//
//  Common.cpp
//  CrackDB++
//
//  Created by Kryc on 11/08/2024.
//  Copyright © 2024 Kryc. All rights reserved.
//

#include <openssl/sha.h>

#include "CrackDBCommon.hpp"
#include "Util.hpp"

const std::string
HashAlgorithmToString(
    const HashAlgorithm Type
)
{
    switch (Type)
    {
    case HashAlgorithmMD5:
        return "MD5";    
    case HashAlgorithmSHA1:
        return "SHA1";
    case HashAlgorithmSHA256:
        return "SHA256";
    case HashAlgorithmSHA384:
        return "SHA384";
    case HashAlgorithmSHA512:
        return "SHA512";
    default:
        return "Undefined";
    }
}

const size_t
GetDigestLength(
    const HashAlgorithm Type
)
{
    switch (Type)
    {
    case HashAlgorithmMD5:
        return MD5_DIGEST_LENGTH;    
    case HashAlgorithmSHA1:
        return SHA_DIGEST_LENGTH;
    case HashAlgorithmSHA256:
        return SHA256_DIGEST_LENGTH;
    case HashAlgorithmSHA384:
        return SHA384_DIGEST_LENGTH;
    case HashAlgorithmSHA512:
        return SHA512_DIGEST_LENGTH;
    default:
        return 0;
    }
}

const HashAlgorithm
DetectHashAlgorithm(
    const std::vector<uint8_t>& Hash
)
{
    if (Hash.size() == MD5_DIGEST_LENGTH)
    {
        return HashAlgorithmMD5;
    }
    else if (Hash.size() == SHA_DIGEST_LENGTH)
    {
        return HashAlgorithmSHA1;
    }
    else if (Hash.size() == SHA256_DIGEST_LENGTH)
    {
        return HashAlgorithmSHA256;
    }
    else if (Hash.size() == SHA384_DIGEST_LENGTH)
    {
        return HashAlgorithmSHA384;
    }
    else if (Hash.size() == SHA512_DIGEST_LENGTH)
    {
        return HashAlgorithmSHA512;
    }
    return HashAlgorithmUndefined;
}

const std::string
AsciiOrHex(
    const std::string& Value
)
{
    bool isAscii = true;
    for (auto c : Value)
    {
        if (c < ' ' || c > '~')
        {
            isAscii = false;
            break;
        }
    }

    if (isAscii)
    {
        return Value;
    }

    return "$HEX[" + Util::ToHex((uint8_t*)&Value[0], Value.size()) + "]";
}