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

#include "Util.hpp"

#define MAX_DIGEST_LENGTH SHA512_DIGEST_LENGTH

typedef enum _HashAlgorithm
{
    HashAlgorithmUndefined,
    HashAlgorithmMD5,
    HashAlgorithmSHA1,
    HashAlgorithmSHA256,
    HashAlgorithmSHA384,
    HashAlgorithmSHA512
} HashAlgorithm;

static const HashAlgorithm HashAlgorithms[] =
{
    HashAlgorithmMD5,
    HashAlgorithmSHA1,
    HashAlgorithmSHA256,
    HashAlgorithmSHA384,
    HashAlgorithmSHA512
};

const size_t HashAlgoritmCount = sizeof(HashAlgorithms) / sizeof(HashAlgorithm);

/*
 * A single record definition. This can be modified to
 * optimise the storage requirements.
 * The bit field of index and length gives you the original
 * word's length and the index within that file where it can
 * be found
 * The hash is the first N bytes of the hash
 */
#define INDEX_BITS 26
#define LENGTH_BITS 6
#define HASH_BYTES 6
typedef struct __attribute__((__packed__)) _Record
{
    uint32_t Index : INDEX_BITS;
    uint32_t Length : LENGTH_BITS;
    uint8_t  Hash[HASH_BYTES];
} DatabaseRecord;

/*
 * The index and length bits will likely wrap
 * For example, if LENGTH_BITS is only 6, then
 * the maximum length we can store is 2**6 - 1 = 63
 * 64 will wrap back to 0.
 * Calculate that wrap value given the following
 */
#define INDEX_WRAP(x) = ((x) + (1 << INDEX_BITS))
#define LENGTH_WRAP(x) = ((x) + (1 << LENGTH_BITS))

static_assert(sizeof(DatabaseRecord) == sizeof(uint32_t) + HASH_BYTES);

const std::string
HashAlgorithmToString(
    const HashAlgorithm Algorithm
);

const size_t
GetDigestLength(
    const HashAlgorithm Algorithm
);

const HashAlgorithm
DetectHashAlgorithm(
    const std::vector<uint8_t>& Hash
);

inline const HashAlgorithm
DetectHashAlgorithm(
    const std::string& Hash
)
{
    return DetectHashAlgorithm(
        Util::ParseHex(Hash)
    );
}

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
        case HashAlgorithmSHA384:
            SHA384(Data, Length, Digest);
            break;
        case HashAlgorithmSHA512:
            SHA512(Data, Length, Digest);
            break;
        default:
            break;
    }
}

const std::string
AsciiOrHex(
    const std::string& Value
);

#endif /* Common_hpp */