// tests/test_common.h
#pragma once

#include <gtest/gtest.h>
#include <string>
#include <memory>
#include <iostream>

// Include main headers
#include "system.h"
#include "common/common.h"
#include "common/constants.h"
#include "interface/cli.h"

/**
 * @brief Base fixture for all JusticeFlow tests
 */
class JusticeFlowTestFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Initialize system before each test
        system_layer::SystemInitConfig config;
        config.audit_db_conninfo = "host=/var/run/postgresql dbname=justiceflow_test";

        auto &sys = system_layer::SystemManager::getInstance();
        auto result = sys.init(config);

        if (!result.ok())
        {
            std::cerr << "Warning: System initialization failed in test setup\n";
        }
    }

    void TearDown() override
    {
        // Cleanup after each test
        auto &sys = system_layer::SystemManager::getInstance();
        sys.shutdown();
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