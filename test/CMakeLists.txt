cmake_minimum_required(VERSION 3.28)

if(APPLE)
    execute_process(COMMAND brew --prefix OUTPUT_VARIABLE HOMEBREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    message("Homebrew prefix: ${HOMEBREW_PREFIX}")
    include_directories(${HOMEBREW_PREFIX}/include)
    link_directories(${HOMEBREW_PREFIX}/lib)
endif()

#
# Tests
#

include(FetchContent)
FetchContent_Declare(
  googletest
  # Specify the commit you depend on and update it regularly.
  URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# specify clang
SET(CMAKE_C_COMPILER clang)
SET(CMAKE_CXX_COMPILER clang++)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "-Wall -Werror -Wno-deprecated-declarations -Wno-ignored-optimization-argument -Wno-deprecated-declarations -Wno-vla-cxx-extension")
if(!APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunsafe-buffer-usage -Wunsafe-buffer-usage-in-container")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -funroll-loops")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ggdb -O0 -fno-omit-frame-pointer -fsanitize=address -DDEBUG -DDEBUGINFO")

# Cracktools unittest
add_executable(unittest EXCLUDE_FROM_ALL
    HashListUnittest.cpp
    ReduceUnittest.cpp
    LineReaderUnittest.cpp
    UnsafeBufferUnittest.cpp
    UtilUnittest.cpp
    WordGeneratorTest.cpp
    ../src/HashList.cpp
    ../src/Util.cpp
    ../src/WordGenerator.cpp)
target_include_directories(unittest
    PUBLIC
        ./
        ../src/
        ../SimdHash/src/
)
target_link_libraries(unittest gtest_main gmp gmpxx)
add_test(NAME unittest COMMAND unittest)
add_test(NAME util_unittest COMMAND unittest --gtest_filter=Util.*)

# Shared mmap reader tests, independent of the legacy relative-path line fixtures.
add_executable(mmaplinereader_unittest EXCLUDE_FROM_ALL MmapLineReaderUnittest.cpp)
target_include_directories(mmaplinereader_unittest PUBLIC ../src/)
target_link_libraries(mmaplinereader_unittest gtest_main)
add_test(NAME mmaplinereader_unittest COMMAND mmaplinereader_unittest)

# Hashcat rule engine unittest
add_executable(rules_unittest EXCLUDE_FROM_ALL
    CsvWriterUnittest.cpp
    RuleAnalysisUnittest.cpp
    RulesUnittest.cpp
    ../src/CsvWriter.cpp
    ../src/RuleAnalysis.cpp
    ../src/Rules.cpp
    ../src/Util.cpp
    ../src/WordGenerator.cpp)
target_include_directories(rules_unittest
    PUBLIC
        ../src/
)
target_link_libraries(rules_unittest gtest_main gmp gmpxx)
if(CRACKTOOLS_ENABLE_CUDA)
    target_sources(rules_unittest PRIVATE RuleAnalysisCudaUnittest.cpp)
    target_link_libraries(rules_unittest ruleanalysis_cuda)
endif()
add_test(NAME rules_unittest COMMAND rules_unittest)
if(CRACKTOOLS_ENABLE_CUDA)
    set_tests_properties(rules_unittest PROPERTIES ENVIRONMENT "ASAN_OPTIONS=protect_shadow_gap=0")
endif()

# Word-list sort unittest
add_executable(wordlistsort_unittest EXCLUDE_FROM_ALL
    WordlistSortUnittest.cpp
    ../src/Util.cpp
    ../src/WordlistSort.cpp)
target_include_directories(wordlistsort_unittest
    PUBLIC
        ../src/
)
target_link_libraries(wordlistsort_unittest gtest_main gmp gmpxx)
add_test(NAME wordlistsort_unittest COMMAND wordlistsort_unittest)

# ASCII word-list manipulation unittest
find_package(Threads REQUIRED)
add_executable(wordlist_unittest EXCLUDE_FROM_ALL
    WordlistUnittest.cpp
    ../src/Util.cpp
    ../src/Wordlist.cpp)
target_include_directories(wordlist_unittest PUBLIC ../src/)
target_link_libraries(wordlist_unittest gtest_main dispatchqueue Threads::Threads gmp gmpxx)
add_test(NAME wordlist_unittest COMMAND wordlist_unittest)
set_tests_properties(wordlist_unittest PROPERTIES TIMEOUT 60)
add_test(NAME wordlist_help COMMAND $<TARGET_FILE:wordlist> --help)
add_test(NAME wordlist_cli
    COMMAND ${CMAKE_COMMAND}
        -DWORDLIST=$<TARGET_FILE:wordlist>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/wordlist_integration
        -P ${CMAKE_CURRENT_SOURCE_DIR}/WordlistIntegration.cmake)

    add_test(NAME dispatch_application_smoke
        COMMAND ${CMAKE_COMMAND}
        -DCRACKLIST=$<TARGET_FILE:cracklist>
        -DSIMDCRACK=$<TARGET_FILE:simdcrack>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/dispatch_applications
        -P ${CMAKE_CURRENT_SOURCE_DIR}/DispatchApplicationsIntegration.cmake)
    set_tests_properties(dispatch_application_smoke PROPERTIES TIMEOUT 60)

# RainbowTable unittest (needs simdhash, dispatchqueue, crypto)
add_executable(rainbow_unittest EXCLUDE_FROM_ALL
    RainbowTableUnittest.cpp
    RainbowTableSetUnittest.cpp
    ReduceUnittest.cpp
    ../src/BloomFilter.cpp
    ../src/RainbowTable.cpp
    ../src/RainbowTableSet.cpp
    ../src/Util.cpp
    ../src/WordGenerator.cpp)
target_include_directories(rainbow_unittest
    PUBLIC
        ./
        ../src/
        ../SimdHash/src/
        ../libdispatchqueue/include/
)
target_link_libraries(rainbow_unittest gtest_main simdhash dispatchqueue crypto gmp gmpxx)
add_test(NAME rainbow_unittest COMMAND rainbow_unittest)