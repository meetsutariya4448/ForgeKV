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

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
