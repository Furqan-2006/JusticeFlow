// tests/system_integration_test.cpp
#include "test_common.h"

/**
 * @class SystemIntegrationTest
 * @brief End-to-end integration tests
 */
class SystemIntegrationTest : public JusticeFlowTestFixture
{
};

// =========================================================================
// Full Workflow Tests
// =========================================================================

TEST_F(SystemIntegrationTest, LoginToLogoutFlow)
{
    auto &sys = system_layer::SystemManager::getInstance();

    // Step 1: Login
    auto login_result = sys.auth().login("12345-6789012-3", "password123");
    EXPECT_TRUE(login_result.ok());

    std::string token = login_result.value;

    // Step 2: Validate token
    auto validate_result = sys.auth().validateToken(token.c_str());
    EXPECT_TRUE(validate_result.ok());

    // Step 3: Logout
    auto logout_result = sys.auth().logout(token.c_str());
    EXPECT_TRUE(logout_result.ok());

    // Step 4: Token should be invalid
    auto revalidate_result = sys.auth().validateToken(token.c_str());
    EXPECT_FALSE(revalidate_result.ok());
}

TEST_F(SystemIntegrationTest, CaseLifecycle)
{
    MockDatabase db;
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow_test"))
    {
        GTEST_SKIP() << "Database not available";
    }

    auto &sys = system_layer::SystemManager::getInstance();
    auto session = test_utils::createTestSession(1);

    // TODO: Implement full case lifecycle once subsystem adapters are ready
    // Step 1: Create case
    // Step 2: Add officer
    // Step 3: Add victim
    // Step 4: Add evidence
    // Step 5: Close case
}

// =========================================================================
// Error Recovery Tests
// =========================================================================

TEST_F(SystemIntegrationTest, PartialInitRecovery)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    config.audit_db_conninfo = "host=invalid dbname=invalid";

    auto result = sys.init(config);

    // Should handle gracefully
    EXPECT_FALSE(result.ok());
}

// =========================================================================
// Data Consistency Tests
// =========================================================================

TEST_F(SystemIntegrationTest, CaseDataConsistency)
{
    MockDatabase db;
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow_test"))
    {
        GTEST_SKIP() << "Database not available";
    }

    auto &sys = system_layer::SystemManager::getInstance();

    // Create a case
    auto case1 = test_utils::createTestCase(db.getConnection(), 1);

    // Retrieve the same case
    auto case1_retrieval = sys.cases().getCaseById(db.getConnection(), case1.case_id);

    EXPECT_TRUE(case1_retrieval.ok());
    EXPECT_EQ(case1_retrieval.value.case_id, case1.case_id);
}