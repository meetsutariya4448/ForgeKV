cmake_minimum_required(VERSION 3.24)

project(
    ForgeKV
    VERSION 0.1.0
    DESCRIPTION "A persistent key-value store engineering project"
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/ForgeKVOptions.cmake)

add_library(forgekv_warnings INTERFACE)
target_compile_options(
    forgekv_warnings
    INTERFACE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
)

add_library(forgekv_sanitizers INTERFACE)
forgekv_configure_sanitizers(forgekv_sanitizers)

add_library(
    forgekv
    src/benchmark/output.cpp
    src/cluster/hash_ring.cpp
    src/cluster/replication.cpp
    src/concurrency/worker_pool.cpp
    src/index/sharded_index.cpp
    src/network/tcp.cpp
    src/protocol/frame.cpp
    src/storage/crc32c.cpp
    src/storage/engine.cpp
    src/storage/record.cpp
    src/version.cpp
)
add_library(ForgeKV::forgekv ALIAS forgekv)
target_compile_features(forgekv PUBLIC cxx_std_20)
target_include_directories(
    forgekv
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(forgekv PRIVATE forgekv_warnings forgekv_sanitizers)

function(forgekv_add_app target source)
    add_executable(${target} ${source})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE ForgeKV::forgekv forgekv_warnings forgekv_sanitizers)
endfunction()

forgekv_add_app(forgekv-server apps/forgekv-server/main.cpp)
forgekv_add_app(forgekv-cli apps/forgekv-cli/main.cpp)
forgekv_add_app(forgekv-bench apps/forgekv-bench/main.cpp)

set(FORGEKV_BUILD_METADATA_HEADER
    ${CMAKE_CURRENT_BINARY_DIR}/generated/forgekv/build_metadata.hpp)
add_custom_target(
    forgekv_build_metadata ALL
    COMMAND
        ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${FORGEKV_BUILD_METADATA_HEADER}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildMetadata.cmake
    BYPRODUCTS ${FORGEKV_BUILD_METADATA_HEADER}
    COMMENT "Refreshing ForgeKV build metadata"
    VERBATIM
)
add_dependencies(forgekv-bench forgekv_build_metadata)
target_include_directories(forgekv-bench PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(CMAKE_BUILD_TYPE)
    set(FORGEKV_BUILD_DESCRIPTION_VALUE ${CMAKE_BUILD_TYPE})
else()
    set(FORGEKV_BUILD_DESCRIPTION_VALUE unspecified)
endif()
target_compile_definitions(
    forgekv-bench
    PRIVATE
        FORGEKV_COMPILER_DESCRIPTION="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
        FORGEKV_BUILD_DESCRIPTION="${FORGEKV_BUILD_DESCRIPTION_VALUE}"
)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
