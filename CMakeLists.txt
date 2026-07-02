cmake_minimum_required(VERSION 3.17)

project("qunzip" CXX)

include(FetchContent)

# Pull in the rapidgzip library from the rapidgzip submodule (header-only C++17).
# librapidarchive/ holds the project's root CMakeLists.txt.
FetchContent_Declare(
    librapidarchive
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rapidgzip/librapidarchive"
)

set(LIBRAPIDARCHIVE_BUILD_TESTS OFF)
set(LIBRAPIDARCHIVE_BUILD_TOOLS OFF)
set(LIBRAPIDARCHIVE_BUILD_BENCHMARKS OFF)

FetchContent_MakeAvailable(librapidarchive)

# The q shared object. q's 2: loads it by exact name (qunzip.so / qunzip.dll),
# so suppress the default "lib" prefix.
add_library(qunzip SHARED qunzip.cpp)
target_include_directories(qunzip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # for k.h
target_link_libraries(qunzip PRIVATE rapidgzip::rapidgzip)
set_target_properties(qunzip PROPERTIES
    PREFIX ""
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
