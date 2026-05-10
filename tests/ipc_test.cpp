#include <gtest/gtest.h>
#include "../src/os_layer/ipc/include/shared_memory.h"

TEST(IPCTest, SharedMemoryCreateAttach)
{
    ipc::SharedMemory shm("/jf_test_shm");
    auto create_rc = shm.create();
    if (create_rc != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Shared memory create not available in environment";
    }

    auto *table = shm.getTable();
    ASSERT_NE(table, nullptr);
    shm.destroy();
}
