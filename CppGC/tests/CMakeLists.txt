#GTEST
# Enable testing
enable_testing()

# Find GoogleTest package
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

set(SOURCE_FILES tests.cpp)
set(UNIT_TESTS unittests)

add_executable(${UNIT_TESTS} ${SOURCE_FILES})
target_include_directories(${UNIT_TESTS} PRIVATE )
target_link_libraries(${UNIT_TESTS} GTest::GTest)

add_test(NAME ${UNIT_TESTS} COMMAND ${UNIT_TESTS} )