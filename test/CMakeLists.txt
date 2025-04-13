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
  URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
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
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunsafe-buffer-usage -Wunsafe-buffer-usage-in-container")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -funroll-loops")
set(CMAKE_CXX_FLAGS_DEBUG "-g -ggdb -O0 -fno-omit-frame-pointer -fsanitize=address -DDEBUG -DDEBUGINFO")

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(hashlist_unittest EXCLUDE_FROM_ALL
    HashListUnittest.cpp
    ../src/HashList.cpp
    ../src/Util.cpp)
target_include_directories(hashlist_unittest
    PUBLIC
        ./
        ../src/
        ../SimdHash/src/
)
target_link_libraries(hashlist_unittest gtest_main gmp gmpxx)
add_test(NAME hashlist_unittest COMMAND hashlist_unittest)

add_custom_target(
    unittests
    DEPENDS hashlist_unittest
)