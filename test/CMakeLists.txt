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

# Hashcat rule engine unittest
add_executable(rules_unittest EXCLUDE_FROM_ALL
    RulesUnittest.cpp
    ../src/Rules.cpp
    ../src/Util.cpp)
target_include_directories(rules_unittest
    PUBLIC
        ../src/
)
target_link_libraries(rules_unittest gtest_main gmp gmpxx)
add_test(NAME rules_unittest COMMAND rules_unittest)

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