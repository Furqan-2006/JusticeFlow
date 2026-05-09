// tests/test_common.h
#pragma once

#include <gtest/gtest.h>
#include <string>
#include <memory>

#include <iostream>

// Include main headers
#include "../src/system/system.h"
#include "common/common.h"
#include "common/constants.h"
#include "common/dbconfig.h"
#include "../src/interface/cli.h"

/**
 * @brief Base fixture for all JusticeFlow tests
 */
class JusticeFlowTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto &sys = system_layer::SystemManager::getInstance();

        // ✅ Skip if already initialized
        if (sys.isInitialized())
        {
            sys.shutdown();
        }

        system_layer::SystemInitConfig config;
        config.audit_db_conninfo = "host=/var/run/postgresql dbname=justiceflow_test";

        auto result = sys.init(config);

        // ✅ Graceful skip instead of assertion
        if (!result.ok())
        {
            GTEST_SKIP() << "System init failed (rc=" << static_cast<int>(result.code)
                         << "). Ensure PostgreSQL is running.";
        }
    }

    void TearDown() override
    {
        auto &sys = system_layer::SystemManager::getInstance();

        // ✅ Safe shutdown even if init failed
        if (sys.isInitialized())
        {
            sys.shutdown();
        }
    }
};
/**
 * @brief Mock database for testing
 */
class MockDatabase
{
public:
    MockDatabase() : m_conn(nullptr) {}

    ~MockDatabase()
    {
        if (m_conn)
        {
            PQfinish(m_conn);
        }
    }

    bool connect(const std::string &conninfo)
    {
        m_conn = PQconnectdb(conninfo.c_str());
        return PQstatus(m_conn) == CONNECTION_OK;
    }

    PGconn *getConnection() { return m_conn; }

private:
    PGconn *m_conn;
};

/**
 * @brief Test utility functions
 */
namespace test_utils
{
    /**
     * Create a test case in the database
     */
    JusticeFlow::Case createTestCase(PGconn *conn, int officer_id);

    /**
     * Create a test officer
     */
    JusticeFlow::Officer createTestOfficer(PGconn *conn);

    /**
     * Create a test session
     */
    JusticeFlow::SessionContext createTestSession(int officer_id);

    /**
     * Clean up test data
     */
    void cleanupTestData(PGconn *conn);
}