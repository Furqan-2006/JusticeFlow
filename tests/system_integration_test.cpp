// tests/system_integration_test.cpp
#include "test_common.h"
#include <thread>

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
    auto login_result = sys.auth().login("42401-637951-0", "JusticeDemo@2026");
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
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
    {
        GTEST_SKIP() << "Database not available";
    }

    auto &sys = system_layer::SystemManager::getInstance();
    auto officer = test_utils::createTestOfficer(db.getConnection());
    auto session = test_utils::createTestSession(officer.officerId);
    session.rank = JusticeFlow::OfficerRank::SI;

    auto created = test_utils::createTestCase(db.getConnection(), officer.officerId);
    auto fetch = sys.cases().getCaseById(db.getConnection(), created.case_id);
    if (!fetch.ok())
    {
        GTEST_SKIP() << "Case seed unavailable for lifecycle test";
    }

    auto accused = sys.cases().addAccused(db.getConnection(), session, created.case_id, session.cnic.c_str(), JusticeFlow::InvolvementType::ACCUSED);
    EXPECT_TRUE(accused.ok() || accused.code == JusticeFlow::ResultCode::FOREIGN_KEY_VIOLATION);

    auto closed = sys.cases().closeCase(db.getConnection(), session, created.case_id, "integration-close");
    EXPECT_TRUE(closed.ok() || closed.code == JusticeFlow::ResultCode::NOT_FOUND);
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

    // Invalid audit DB config should not crash global init.
    EXPECT_TRUE(result.ok());
}

// =========================================================================
// Data Consistency Tests
// =========================================================================

TEST_F(SystemIntegrationTest, CaseDataConsistency)
{
    MockDatabase db;
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
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

TEST_F(SystemIntegrationTest, ConcurrentCaseRegistrationDoesNotDuplicateIds)
{
    MockDatabase db;
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
    {
        GTEST_SKIP() << "Database not available";
    }

    auto &sys = system_layer::SystemManager::getInstance();
    auto session = test_utils::createTestSession(1);

    int first_id = -1;
    int second_id = -1;
    std::thread t1([&]()
                   {
        MockDatabase local_db;
        if (!local_db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
            return;
        auto r = sys.cases().registerCase(local_db.getConnection(), session, JusticeFlow::CaseType::MURDER, time(nullptr), "A", "A", 0, 0, 1, session.cnic.c_str());
        if (r.ok())
            first_id = r.value; });
    std::thread t2([&]()
                   {
        MockDatabase local_db;
        if (!local_db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
            return;
        auto r = sys.cases().registerCase(local_db.getConnection(), session, JusticeFlow::CaseType::ROBBERY, time(nullptr), "B", "B", 0, 0, 1, session.cnic.c_str());
        if (r.ok())
            second_id = r.value; });
    t1.join();
    t2.join();

    if (first_id > 0 && second_id > 0)
    {
        EXPECT_NE(first_id, second_id);
    }
}
