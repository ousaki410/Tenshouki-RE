# src/test/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# Fetch GTest if not found
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Add test executable - link only the resource parser files
add_executable(bin_parser_tests
    test_bin_parser.cpp
    ../resource/bin_parser.cpp
    ../core/logging.cpp
)

target_include_directories(bin_parser_tests PRIVATE
    ../core
    ../resource
)

target_link_libraries(bin_parser_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    fmt::fmt
)

# Add test
add_test(NAME bin_parser_tests COMMAND bin_parser_tests)

# DAT / sndata data-layer tests
add_executable(dat_parser_tests
    test_dat_parser.cpp
    ../resource/dat_parser.cpp
    ../core/logging.cpp
)

target_include_directories(dat_parser_tests PRIVATE
    ../core
    ../resource
)

target_link_libraries(dat_parser_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    fmt::fmt
)

add_test(NAME dat_parser_tests COMMAND dat_parser_tests)