cmake_minimum_required(VERSION 3.10)
project(JusticeFlow_Tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(PostgreSQL REQUIRED)
find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${PostgreSQL_INCLUDE_DIRS})

# OS Layer sources
set(OSLAYER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/os_layer/memory/src/mlock_guard.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/memory/src/mmap_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/threading/src/sync.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/threading/src/thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/threading/src/session_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/threading/src/worker.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/threading/src/connection_gate.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/ipc/src/unix_socket.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/ipc/src/fifo.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/ipc/src/shared_memory.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/ipc/src/ipc_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/process/src/process_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/process/src/process_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/scheduler/src/scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/scheduler/src/timer.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/scheduler/src/signal_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/scheduler/src/daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/scheduler/src/process_spawner.cpp
    ${CMAKE_SOURCE_DIR}/src/os_layer/scheduler/src/job_executor.cpp
)

# Auth module sources
set(AUTH_SOURCES
    ${CMAKE_SOURCE_DIR}/src/shr_infra/auth/src/token_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/shr_infra/auth/src/session_store.cpp
    ${CMAKE_SOURCE_DIR}/src/shr_infra/auth/src/duty_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/shr_infra/auth/src/auth_manager.cpp
)

# Common sources (logger, etc.)
set(COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/common/logger.cpp
)

# OS Layer Integration Test
add_executable(os_layer_integration_test
    ${CMAKE_CURRENT_SOURCE_DIR}/os_layer_integration_test.cpp
    ${OSLAYER_SOURCES}
    ${AUTH_SOURCES}
    ${COMMON_SOURCES}
)

target_link_libraries(os_layer_integration_test
    GTest::GTest GTest::Main
    Threads::Threads
    ${PostgreSQL_LIBRARIES}
    m
)

target_compile_options(os_layer_integration_test PRIVATE -Wall -Wextra -pthread)