cmake_minimum_required(VERSION 3.28)

# specify clang
SET(CMAKE_C_COMPILER clang)
SET(CMAKE_CXX_COMPILER clang++)

# set the project name
project(cracktools VERSION 0.1)

enable_testing()

# Set build type default Release
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

add_subdirectory(./SimdHash/)
add_subdirectory(./libdispatchqueue/)
add_subdirectory(./test/)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "-Wall -Werror -Wno-deprecated-declarations -Wno-ignored-optimization-argument -Wno-deprecated-declarations -Wno-vla-cxx-extension")
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -Wunsafe-buffer-usage -Wunsafe-buffer-usage-in-container")
endif()

# Backward compat: map old AVX variable to new SIMD variable
if (NOT SIMD OR SIMD STREQUAL "")
    if (AVX STREQUAL "512")
        set(SIMD "avx512")
    elseif(AVX STREQUAL "256")
        set(SIMD "avx256")
    endif()
endif()

# Set SIMD flags for X86
message("Detected platform: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
if (NOT CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" AND NOT CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
    if (SIMD STREQUAL "avx512")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx512bw")
    elseif(SIMD STREQUAL "avx256")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
    elseif(SIMD STREQUAL "sse42")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2")
    elseif(SIMD STREQUAL "ssse3")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mssse3")
    elseif(SIMD STREQUAL "sse2")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
    else()
        # Default: auto-detect from host CPU
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
    endif()
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -funroll-loops")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ggdb -O0 -fno-omit-frame-pointer -fsanitize=address -DDEBUG -DDEBUGINFO")

if(APPLE)
    execute_process(COMMAND brew --prefix OUTPUT_VARIABLE HOMEBREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    message("Homebrew prefix: ${HOMEBREW_PREFIX}")
    include_directories(${HOMEBREW_PREFIX}/include)
    link_directories(${HOMEBREW_PREFIX}/lib)
endif()

# CrackList
set(CRACKLIST_SOURCES
    src/CrackList.cpp
    src/CrackListMain.cpp
    src/HashList.cpp
    src/Util.cpp
)
add_executable(cracklist ${CRACKLIST_SOURCES})
target_include_directories(cracklist
                            PUBLIC
                                ./src/
                                ./SimdHash/src/
                        )
target_link_libraries(cracklist simdhash dispatchqueue crypto gmp gmpxx)

# CrackDB
set(CRACKDB_SOURCES
    src/CrackDatabase.cpp
    src/CrackDBMain.cpp
    src/HashList.cpp
    src/Util.cpp
    src/Wordfile.cpp
)
add_executable(crackdb++ ${CRACKDB_SOURCES})
set_property(TARGET crackdb++ PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
target_include_directories(crackdb++
                            PUBLIC
                                ./src/
                                ./SimdHash/src/
                        )
target_link_libraries(crackdb++ dispatchqueue simdhash crypto gmp gmpxx)

# SimdRainbowCrack
set(SIMDRAINBOWCRACK_SOURCES
    src/SimdRainbowCrackMain.cpp
    src/RainbowTable.cpp
    src/Util.cpp
    src/WordGenerator.cpp
)
add_executable(simdrainbowcrack ${SIMDRAINBOWCRACK_SOURCES})
target_include_directories(simdrainbowcrack
                            PUBLIC
                                ./src/
                                ./SimdHash/src/
                        )
target_link_libraries(simdrainbowcrack simdhash dispatchqueue crypto gmp gmpxx)

# SimdCrack
set(SIMDCRACK_SOURCES
    src/HashList.cpp
    src/SimdCrack.cpp
    src/SimdCrackMain.cpp
    src/Util.cpp
    src/WordGenerator.cpp
)
add_executable(simdcrack ${SIMDCRACK_SOURCES})
target_include_directories(simdcrack
                            PUBLIC
                                ./src/
                                ./SimdHash/src/
                                ./libdispatchqueue/include/
                        )
target_link_libraries(simdcrack gmp gmpxx simdhash dispatchqueue)

# Wordgen
set(WORDGEN_SOURCES
    src/WordGenerator.cpp
    src/WordGenMain.cpp
)
add_executable(wordgen ${WORDGEN_SOURCES})
target_include_directories(wordgen
                            PUBLIC
                                ./src/
                        )
target_link_libraries(wordgen gmp gmpxx)

# HashPipe
set(HASHPIPE_SOURCES
    src/HashPipeMain.cpp
    src/Util.cpp
)
add_executable(hashpipe ${HASHPIPE_SOURCES})
target_include_directories(hashpipe
                            PUBLIC
                                ./src/
                                ./SimdHash/src/
                        )
target_link_libraries(hashpipe simdhash gmp gmpxx)

# Hexlify
set(HEXLIFY_SOURCES
    src/HexlifyMain.cpp
    src/Util.cpp
)
add_executable(hexlify ${HEXLIFY_SOURCES})
target_include_directories(hexlify
                            PUBLIC
                                ./src/
                        )
target_link_libraries(hexlify gmp gmpxx)

# Hexlify
set(UNHEXLIFY_SOURCES
    src/UnhexlifyMain.cpp
    src/Util.cpp
)
add_executable(unhexlify ${UNHEXLIFY_SOURCES})
target_include_directories(unhexlify
                            PUBLIC
                                ./src/
                        )
target_link_libraries(unhexlify gmp gmpxx)

# RemoveCracked
set(REMOVECRACKED_SOURCES
    src/RemoveCrackedMain.cpp
    src/Util.cpp
)
add_executable(removecracked ${REMOVECRACKED_SOURCES})
target_include_directories(removecracked
                            PUBLIC
                                ./src/
                        )
target_link_libraries(removecracked gmp gmpxx)

# CheckCracked
set(CHECKCRACKED_SOURCES
    src/CheckCrackedMain.cpp
    src/Util.cpp
)
add_executable(checkcracked ${CHECKCRACKED_SOURCES})
target_include_directories(checkcracked
                            PUBLIC
                                ./src/
                                ./SimdHash/src/
                        )
target_link_libraries(checkcracked simdhash gmp gmpxx)

# HCMaskGen
set(HCMASKGEN_SOURCES
    src/HCMaskGenMain.cpp
    src/Util.cpp
)
add_executable(hcmaskgen ${HCMASKGEN_SOURCES})
target_include_directories(hcmaskgen
                            PUBLIC
                                ./src/
                                ./SimdHash/src/
                        )
target_link_libraries(hcmaskgen gmp gmpxx)

# HashlistConvert
set(HASHLISTCONVERT_SOURCES
    src/HashlistConvertMain.cpp
    src/Util.cpp
)
add_executable(hashlistconvert ${HASHLISTCONVERT_SOURCES})
target_include_directories(hashlistconvert
                            PUBLIC
                                ./src/
                        )
target_link_libraries(hashlistconvert gmp gmpxx)

# WordListFilter
set(WORDLISTFILTER_SOURCES
    src/WordListFilterMain.cpp
    src/Util.cpp
)
add_executable(wordlistfilter ${WORDLISTFILTER_SOURCES})
target_include_directories(wordlistfilter
                            PUBLIC
                                ./src/
                        )
target_link_libraries(wordlistfilter gmp gmpxx)

# WordlistLengthSplit
set(WORDLISTLENGTHSPLIT_SOURCES
    src/WordlistLengthSplit.cpp
    src/Util.cpp
)
add_executable(wordlistlengthsplit ${WORDLISTLENGTHSPLIT_SOURCES})
target_include_directories(wordlistlengthsplit
                            PUBLIC
                                ./src/
                        )
target_link_libraries(wordlistlengthsplit gmp gmpxx)

# SortPwned
set(SORTPWNED_SOURCES
    src/SortPwnedMain.cpp
    src/Util.cpp
)
add_executable(sortpwned ${SORTPWNED_SOURCES})
target_include_directories(sortpwned
                            PUBLIC
                                ./src/
                        )
target_link_libraries(sortpwned gmp gmpxx)

# HashListLookup
set(HASHLISTLOOKUP_SOURCES
    src/HashListLookupMain.cpp
    src/Util.cpp
)
add_executable(hashlistlookup ${HASHLISTLOOKUP_SOURCES})
target_include_directories(hashlistlookup
                            PUBLIC
                                ./src/
                        )
target_link_libraries(hashlistlookup gmp gmpxx)

# CredParse
set(CREDPARSE_SOURCES
    src/CredParseMain.cpp
    src/CrackDatabase.cpp
    src/Wordfile.cpp
    src/Util.cpp
)
add_executable(credparse ${CREDPARSE_SOURCES})
set_property(TARGET credparse PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
target_include_directories(credparse
                            PUBLIC
                                ./src/
                                ./SimdHash/src/
                                ./libdispatchqueue/include/
                        )
target_link_libraries(credparse dispatchqueue simdhash crypto gmp gmpxx)

# RandString
set(RANDSTRING_SOURCES
    src/RandStringMain.cpp
    src/WordGenerator.cpp
)
add_executable(randstring ${RANDSTRING_SOURCES})
target_include_directories(randstring
                            PUBLIC
                                ./src/
                        )
target_link_libraries(randstring gmp gmpxx)

# Create a set of all targets
set(ALL_TARGETS
    cracklist
    crackdb++
    simdcrack
    simdrainbowcrack
    wordgen
    hashpipe
    hexlify
    unhexlify
    removecracked
    checkcracked
    hcmaskgen
    hashlistconvert
    wordlistfilter
    wordlistlengthsplit
    sortpwned
    hashlistlookup
    credparse
    randstring
)

# Create install targets
install(TARGETS ${ALL_TARGETS}
    RUNTIME DESTINATION bin
)