cmake_minimum_required(VERSION 3.10)
project(JusticeFlow_Tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Define the Project Root (one level up from the 'tests' directory)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# 2. Find dependencies
find_package(PostgreSQL REQUIRED)
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# 3. Include directories
# We include the main 'src' folder and the module sub-folders for easy header access
include_directories(${PROJECT_ROOT}/src)
include_directories(${PROJECT_ROOT}/src/common)
include_directories(${PROJECT_ROOT}/src/os_layer)
include_directories(${PostgreSQL_INCLUDE_DIRS})
include_directories(${/usr/include/postgresql})

# 4. OS Layer sources (Pathing updated to match your tree)
set(OSLAYER_SOURCES
    ${PROJECT_ROOT}/src/os_layer/memory/src/mlock_guard.cpp
    ${PROJECT_ROOT}/src/os_layer/memory/src/mmap_handler.cpp
    ${PROJECT_ROOT}/src/os_layer/threading/src/sync.cpp
    ${PROJECT_ROOT}/src/os_layer/threading/src/thread_pool.cpp
    ${PROJECT_ROOT}/src/os_layer/threading/src/session_manager.cpp
    ${PROJECT_ROOT}/src/os_layer/threading/src/worker.cpp
    ${PROJECT_ROOT}/src/os_layer/threading/src/connection_gate.cpp
    ${PROJECT_ROOT}/src/os_layer/ipc/src/unix_socket.cpp
    ${PROJECT_ROOT}/src/os_layer/ipc/src/fifo.cpp
    ${PROJECT_ROOT}/src/os_layer/ipc/src/shared_memory.cpp
    ${PROJECT_ROOT}/src/os_layer/ipc/src/ipc_manager.cpp
    ${PROJECT_ROOT}/src/os_layer/process/src/process_manager.cpp
    ${PROJECT_ROOT}/src/os_layer/process/src/process_registry.cpp
    ${PROJECT_ROOT}/src/os_layer/scheduler/src/scheduler.cpp
    ${PROJECT_ROOT}/src/os_layer/scheduler/src/timer.cpp
    ${PROJECT_ROOT}/src/os_layer/scheduler/src/signal_handler.cpp
    ${PROJECT_ROOT}/src/os_layer/scheduler/src/daemon.cpp
    ${PROJECT_ROOT}/src/os_layer/scheduler/src/process_spawner.cpp
    ${PROJECT_ROOT}/src/os_layer/scheduler/src/job_executor.cpp
)

# 5. Auth module sources (Names updated to match your tree: auth.cpp, session.cpp, token.cpp)
set(AUTH_SOURCES
    ${PROJECT_ROOT}/src/shr_infra/auth/src/token_generator.cpp
    ${PROJECT_ROOT}/src/shr_infra/auth/src/session_store.cpp
    ${PROJECT_ROOT}/src/shr_infra/auth/src/auth_manager.cpp
    ${PROJECT_ROOT}/src/shr_infra/auth/src/duty_cache.cpp
)

# 6. Common sources
# Note: Ensure logger.cpp exists; if not, you can remove this variable
# set(COMMON_SOURCES
#     ${PROJECT_ROOT}/src/common/logger.cpp
# )

# 7. Define the Test Executable
# This uses the db_test.cpp you have in your tests folder
add_executable(os_layer_integration_test
    ${CMAKE_CURRENT_SOURCE_DIR}/db_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/os_smoke_test.cpp
    ${OSLAYER_SOURCES}
    ${AUTH_SOURCES}
    ${COMMON_SOURCES}
)

# 8. Linking
target_link_libraries(os_layer_integration_test
    GTest::GTest
    GTest::Main
    Threads::Threads
    ${PostgreSQL_LIBRARIES}
    m  # Math library
)

# 9. Compiler Flags
target_compile_options(os_layer_integration_test PRIVATE -Wall -Wextra -pthread)