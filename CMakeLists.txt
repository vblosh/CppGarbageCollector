cmake_minimum_required(VERSION 3.14)
project(CppGC VERSION 1.0 LANGUAGES CXX)

# Specify C++ standard
add_library(cppgc INTERFACE)
target_compile_features(cppgc INTERFACE cxx_std_17)
target_include_directories(cppgc INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(CPPGC_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(CPPGC_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cppgc INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cppgc INTERFACE -fsanitize=address,undefined)
endif()

enable_testing()

# Add unit tests
add_subdirectory(tests)

# Add performance test
option(CPPGC_BUILD_PERF_TESTS "Build the performance test executable" ON)
if(CPPGC_BUILD_PERF_TESTS)
    add_subdirectory(perf_test)
