find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(unittests tests.cpp)
target_link_libraries(unittests PRIVATE cppgc GTest::GTest)

gtest_discover_tests(unittests)
