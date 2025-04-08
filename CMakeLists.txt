cmake_minimum_required(VERSION 3.10)

# set the project name
project(cracktools VERSION 0.1)

# Set build type default Release
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

add_subdirectory(./SimdHash/)
add_subdirectory(./libdispatchqueue/)

# specify clang
SET(CMAKE_C_COMPILER clang)
SET(CMAKE_CXX_COMPILER clang++)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "-Wall -Werror -fno-exceptions -Wno-deprecated-declarations -Wno-ignored-optimization-argument -Wno-deprecated-declarations -Wno-vla-cxx-extension")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunsafe-buffer-usage")
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