#include "os_layer/os_layer.h"
#include <gtest/gtest.h>

#include <vector>
#include <string>

TEST(OSLayerDB, ConnectAndSelectOne)
{
    // Connect through the OS-layer facade (IpcManager -> UnixSocket).
    JusticeFlow::ResultCode rc = ipc::IpcManager::getInstance().connectDatabase();

    // If DB isn't configured/running, skip (so OS-layer tests can still pass on dev/CI machines).
    if (rc != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP()
            << "connectDatabase() failed (rc=" << static_cast<int>(rc) << "). "
            << "Ensure Postgres is running and env vars are set before launching the test: "
            << "JF_DB_HOST, JF_DB_PORT, JF_DB_NAME, JF_DB_USER(=justice_app), JF_DB_PASS.";
    }

    std::vector<std::vector<std::string>> results;
    rc = ipc::IpcManager::getInstance().executeQuery("SELECT 1;", results);

    if (rc != JusticeFlow::ResultCode::OK)
    {
        ipc::IpcManager::getInstance().disconnectDatabase();
        FAIL() << "executeQuery(\"SELECT 1;\") failed (rc=" << static_cast<int>(rc) << ")";
    }

    ASSERT_FALSE(results.empty());
    ASSERT_FALSE(results[0].empty());
    EXPECT_EQ(results[0][0], "1");

    ipc::IpcManager::getInstance().disconnectDatabase();
}