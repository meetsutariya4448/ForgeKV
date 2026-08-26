include(FetchContent)

option(FORGEKV_USE_SYSTEM_GTEST "Use a preinstalled GoogleTest package" OFF)

if(FORGEKV_USE_SYSTEM_GTEST)
    find_package(GTest CONFIG REQUIRED)
else()
    message(STATUS "Fetching pinned GoogleTest v1.15.2 (b514bdc)")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59
        GIT_SHALLOW TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(
    forgekv_unit_tests
    integration/storage_engine_test.cpp
    unit/storage/record_test.cpp
    unit/version_test.cpp
)
target_compile_features(forgekv_unit_tests PRIVATE cxx_std_20)
target_link_libraries(
    forgekv_unit_tests
    PRIVATE
        ForgeKV::forgekv
        GTest::gtest_main
        forgekv_warnings
        forgekv_sanitizers
)

include(GoogleTest)
gtest_discover_tests(
    forgekv_unit_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 15
)

if(FORGEKV_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
        message(FATAL_ERROR "ForgeKV fuzz targets currently require Clang and libFuzzer")
    endif()

    add_executable(fuzz_record_decoder fuzz/record_decoder_fuzz.cpp)
    target_compile_features(fuzz_record_decoder PRIVATE cxx_std_20)
    target_compile_options(fuzz_record_decoder PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_record_decoder PRIVATE -fsanitize=fuzzer)
    target_link_libraries(
        fuzz_record_decoder
        PRIVATE ForgeKV::forgekv forgekv_warnings forgekv_sanitizers
    )
endif()
