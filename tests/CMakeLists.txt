find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(unittests tests.cpp)
target_link_libraries(unittests PRIVATE cppgc GTest::GTest)
target_compile_definitions(unittests PRIVATE CPPGC_TESTING=1)

gtest_discover_tests(unittests)
