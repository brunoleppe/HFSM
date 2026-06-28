cmake_minimum_required(VERSION 3.28)
project(HFSM)

# Header-only library target — consumers get the include path automatically
add_library(hfsm INTERFACE)
target_include_directories(hfsm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(examples)

# Tests — C++20 required (StrLiteral NTTPs, constexpr comparisons, etc.)
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/googletest-src
        BINARY_DIR ${CMAKE_BINARY_DIR}/googletest-build
        SUBBUILD_DIR ${CMAKE_BINARY_DIR}/googletest-subbuild
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_subdirectory(tests)