cmake_minimum_required(VERSION 3.10)
project(CppGC VERSION 1.0)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add include directories
include_directories(include)

# Add unit tests
add_subdirectory(tests)

# Add perfomance test
add_subdirectory(perf_test)
