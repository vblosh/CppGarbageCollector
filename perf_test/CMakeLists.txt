# Define the path to the data file
set(DATA_FILE "${CMAKE_SOURCE_DIR}/data/data.bin")

add_executable(perfomace_test perfomace_test.cpp)
target_link_libraries(perfomace_test PRIVATE cppgc)

# Copy the data file to the build directory for the executable
add_custom_command(
    TARGET perfomace_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${DATA_FILE} ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)
