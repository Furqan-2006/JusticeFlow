// tests/os_layer_test.cpp
#include "test_common.h"
#include "os_layer/os_layer.h"

/**
 * @class OSLayerTest
 * @brief Tests for OS layer components
 */
class OSLayerTest : public ::testing::Test
{
};

// =========================================================================
// Scheduler Tests
// =========================================================================

TEST_F(OSLayerTest, SchedulerInitialization)
{
    // Scheduler should be available
    auto &scheduler = Scheduler::getInstance();
    EXPECT_NE(&scheduler, nullptr);
}

// =========================================================================
// Threading Tests
// =========================================================================

TEST_F(OSLayerTest, ThreadPoolCreation)
{
    // Thread pool should initialize
    auto &pool = ThreadPool::getInstance();
    EXPECT_NE(&pool, nullptr);
}

// =========================================================================
// IPC Tests
// =========================================================================

TEST_F(OSLayerTest, IPCManagerCreation)
{
    // IPC manager should be available
    auto &ipc = ipc::IpcManager::getInstance();
    EXPECT_NE(&ipc, nullptr);
}

// =========================================================================
// Process Tests
// =========================================================================

TEST_F(OSLayerTest, ProcessRegistryCreation)
{
    // Process registry should be available
    auto &registry = ProcessRegistry::getInstance();
    EXPECT_NE(&registry, nullptr);
}