cmake_minimum_required(VERSION 4.2)
project(HFSM)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2 # Or whichever version hash/tag you need
        GIT_SHALLOW TRUE     # <--- This forces a shallow clone (--depth 1)
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/googletest-src
        BINARY_DIR ${CMAKE_BINARY_DIR}/googletest-build
        SUBBUILD_DIR ${CMAKE_BINARY_DIR}/googletest-subbuild
)

# For GoogleTest, it's common practice to disable its internal installation rules
# so it doesn't pollute your main project's install targets
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(hfsm
        main.cpp
        hfsm.h)

enable_testing()
include(GoogleTest)

add_executable(HierarchyTests HierarchyTests.cpp hfsm.h)
target_link_libraries(HierarchyTests PRIVATE GTest::gtest_main)
gtest_discover_tests(HierarchyTests)

add_executable(ControllerTests ControllerTests.cpp hfsm.h)
target_link_libraries(ControllerTests PRIVATE GTest::gtest_main)
gtest_discover_tests(ControllerTests)

add_executable(RadioControllerTests RadioControllerTests.cpp hfsm.h)
target_link_libraries(RadioControllerTests PRIVATE GTest::gtest_main)
gtest_discover_tests(RadioControllerTests)

add_executable(DeclarativeHfsmTests DeclarativeHfsmTests.cpp hfsm.h)
target_link_libraries(DeclarativeHfsmTests PRIVATE GTest::gtest_main)
gtest_discover_tests(DeclarativeHfsmTests)