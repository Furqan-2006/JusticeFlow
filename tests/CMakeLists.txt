# tests/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

# Find Google Test
find_package(GTest REQUIRED)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${GTEST_INCLUDE_DIRS}
)

# Test executable
add_executable(justiceflow_test
    test_common.h
    test_common.cpp
    cli_test.cpp
    orchestrator_test.cpp
    system_integration_test.cpp
    os_layer_test.cpp
)

# Link libraries
target_link_libraries(justiceflow_test
    ${GTEST_LIBRARIES}
    ${GTEST_MAIN_LIBRARIES}
    justiceflow_lib
    pthread
)

# Add test
enable_testing()
add_test(NAME JusticeFlowTests COMMAND justiceflow_test)