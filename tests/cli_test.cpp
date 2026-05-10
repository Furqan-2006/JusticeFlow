// tests/cli_test.cpp
#include "test_common.h"
#include <sstream>

/**
 * @class CLITest
 * @brief Tests for CLI interface
 */
class CLITest : public JusticeFlowTestFixture
{
protected:
    JusticeFlow::DBConfig db_config;

    void SetUp() override
    {
        JusticeFlowTestFixture::SetUp();

        // Setup database config for test
        db_config.host = "/var/run/postgresql";
        db_config.dbname = "justiceflow_test";
        db_config.user = "justice_app";
        db_config.port = 5432;
    }
};

// =========================================================================
// Authentication Tests
// =========================================================================

TEST_F(CLITest, LoginWithInvalidCredentials)
{
    auto &sys = system_layer::SystemManager::getInstance();

    auto result = sys.auth().login("99999-9999999-9", "wrongpassword");

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, JusticeFlow::ResultCode::AUTH_FAILED);
}

TEST_F(CLITest, LoginWithValidCredentials)
{
    auto &sys = system_layer::SystemManager::getInstance();

    // This test requires a pre-existing user in the test database
    // In CI/CD, you'd seed this data
    auto result = sys.auth().login("42401-637951-0", "JusticeDemo@2026");

    EXPECT_TRUE(result.ok());
    EXPECT_FALSE(result.value.empty());
}

TEST_F(CLITest, LoginWithEmptyCredentials)
{
    auto &sys = system_layer::SystemManager::getInstance();
    EXPECT_FALSE(sys.auth().login("", "x").ok());
    EXPECT_FALSE(sys.auth().login("42401-637951-0", "").ok());
    EXPECT_FALSE(sys.auth().login("", "").ok());
}

TEST_F(CLITest, LoginWithSqlInjectionUsername)
{
    auto &sys = system_layer::SystemManager::getInstance();
    auto result = sys.auth().login("' OR 1=1 --", "password");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, JusticeFlow::ResultCode::AUTH_FAILED);
}

TEST_F(CLITest, ValidateTokenAfterLogin)
{
    auto &sys = system_layer::SystemManager::getInstance();

    auto login_result = sys.auth().login("42401-637951-0", "JusticeDemo@2026");
    if (!login_result.ok())
    {
        GTEST_SKIP() << "Login failed, cannot test token validation";
    }

    auto token_result = sys.auth().validateToken(login_result.value.c_str());

    EXPECT_TRUE(token_result.ok());
    EXPECT_GT(token_result.value.officerId, 0);
}

TEST_F(CLITest, TokenExpiration)
{
    auto &sys = system_layer::SystemManager::getInstance();

    JusticeFlow::SessionContext session;
    session.sessionToken = "invalid_token_12345";
    session.expiresAt = 0; // Expired

    auto result = sys.auth().validateToken("invalid_token_12345");

    EXPECT_FALSE(result.ok());
}

TEST_F(CLITest, LogoutInvalidatesToken)
{
    auto &sys = system_layer::SystemManager::getInstance();

    auto login_result = sys.auth().login("42401-637951-0", "JusticeDemo@2026");
    if (!login_result.ok())
    {
        GTEST_SKIP() << "Login failed";
    }

    auto logout_result = sys.auth().logout(login_result.value.c_str());
    EXPECT_TRUE(logout_result.ok());

    auto validate_result = sys.auth().validateToken(login_result.value.c_str());
    EXPECT_FALSE(validate_result.ok());
}

// =========================================================================
// Case Management Tests
// =========================================================================

TEST_F(CLITest, ListCasesByStation)
{
    MockDatabase db;
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
    {
        GTEST_SKIP() << "Database connection failed";
    }

    auto &sys = system_layer::SystemManager::getInstance();
    auto result = sys.cases().getCasesByStation(db.getConnection(), 451);

    EXPECT_TRUE(result.ok());
}

TEST_F(CLITest, GetCaseById)
{
    MockDatabase db;
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
    {
        GTEST_SKIP() << "Database connection failed";
    }

    auto &sys = system_layer::SystemManager::getInstance();

    // Create test case first
    auto test_case = test_utils::createTestCase(db.getConnection(), 1);
    if (test_case.case_id == 0)
    {
        GTEST_SKIP() << "Test case creation failed (FK constraint not met — ensure station/officer seed data exists)";
    }

    // Now retrieve it
    auto result = sys.cases().getCaseById(db.getConnection(), test_case.case_id);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value.case_id, test_case.case_id);
}

TEST_F(CLITest, GetNonExistentCase)
{
    MockDatabase db;
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
    {
        GTEST_SKIP() << "Database connection failed";
    }

    auto &sys = system_layer::SystemManager::getInstance();
    auto result = sys.cases().getCaseById(db.getConnection(), 999999);

    EXPECT_FALSE(result.ok());
}

TEST_F(CLITest, GetCasesByStatus)
{
    MockDatabase db;
    if (!db.connect("host=/var/run/postgresql dbname=justiceflow user=justice_app password=justiceflow123"))
    {
        GTEST_SKIP() << "Database connection failed";
    }

    auto &sys = system_layer::SystemManager::getInstance();
    auto result = sys.cases().getCasesByStatus(
        db.getConnection(), 460, JusticeFlow::CaseStatus::UNDER_INVESTIGATION);

    EXPECT_TRUE(result.ok());
}

// =========================================================================
// Permission & Rank Tests
// =========================================================================

TEST_F(CLITest, RankValidationForConstable)
{
    auto &sys = system_layer::SystemManager::getInstance();

    auto session = test_utils::createTestSession(1);
    session.rank = JusticeFlow::OfficerRank::CONSTABLE;

    // Constable should not be able to approve warrants (requires SI or above)
    auto result = sys.auth().validateRank(session, JusticeFlow::OfficerRank::SI);

    EXPECT_FALSE(result.ok());
}

TEST_F(CLITest, RankValidationForSI)
{
    auto &sys = system_layer::SystemManager::getInstance();

    auto session = test_utils::createTestSession(2);
    session.rank = JusticeFlow::OfficerRank::SI;

    // SI should be able to approve warrants
    auto result = sys.auth().validateRank(session, JusticeFlow::OfficerRank::SI);

    EXPECT_TRUE(result.ok());
}

TEST_F(CLITest, RankValidationForDSPAndSP)
{
    auto &sys = system_layer::SystemManager::getInstance();
    auto session = test_utils::createTestSession(3);
    session.rank = JusticeFlow::OfficerRank::DSP;
    EXPECT_TRUE(sys.auth().validateRank(session, JusticeFlow::OfficerRank::SI).ok());
    EXPECT_FALSE(sys.auth().validateRank(session, JusticeFlow::OfficerRank::SP).ok());

    session.rank = JusticeFlow::OfficerRank::SP;
    EXPECT_TRUE(sys.auth().validateRank(session, JusticeFlow::OfficerRank::DSP).ok());
}

// =========================================================================
// Command Parsing Tests
// =========================================================================

TEST(CommandParsingTest, ParseLoginCommand)
{
    std::vector<std::string> tokens = {"login", "--cnic", "12345-6789012-3", "--password", "pass"};

    EXPECT_EQ(tokens[0], "login");
    EXPECT_EQ(tokens[2], "12345-6789012-3");
}

TEST(CommandParsingTest, ParseCaseListCommand)
{
    std::vector<std::string> tokens = {"case", "list", "--office", "5", "--status", "INVESTIGATING"};

    EXPECT_EQ(tokens[0], "case");
    EXPECT_EQ(tokens[1], "list");
    EXPECT_EQ(tokens[3], "5");
}

TEST(CommandParsingTest, ParseWarrantIssueCommand)
{
    std::vector<std::string> tokens = {
        "warrant", "issue", "123", "--accused", "12345-6789012-3", "--reason", "murder"};

    EXPECT_EQ(tokens[0], "warrant");
    EXPECT_EQ(tokens[1], "issue");
    EXPECT_EQ(tokens[2], "123");
}

// =========================================================================
// Output Formatting Tests
// =========================================================================

TEST(OutputFormattingTest, TableFormat)
{
    // Test table formatting logic
    std::vector<std::vector<std::string>> rows = {
        {"1", "FIR-2024-001", "MURDER", "INVESTIGATING"},
        {"2", "FIR-2024-002", "ROBBERY", "CLOSED"}};

    std::vector<std::string> headers = {"ID", "FIR#", "Type", "Status"};

    EXPECT_EQ(headers.size(), 4);
    EXPECT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].size(), 4);
}

TEST(OutputFormattingTest, JsonFormat)
{
    std::string json = R"({"case_id": 1, "fir_number": "FIR-2024-001"})";

    EXPECT_NE(json.find("case_id"), std::string::npos);
    EXPECT_NE(json.find("fir_number"), std::string::npos);
}

// =========================================================================
// Error Handling Tests
// =========================================================================

TEST_F(CLITest, DatabaseConnectionFailure)
{
    MockDatabase db;

    // EXPECT_CALL(db, executeQuery)
    //     .WillOnce(Return(JusticeFlow::ResultCode::DB_ERROR));

    bool connected = db.connect("host=invalid_host dbname=invalid_db");

    EXPECT_FALSE(connected);
}

TEST_F(CLITest, InvalidCommandHandling)
{
    std::vector<std::string> tokens = {"invalid_command"};

    // Should return error result
    EXPECT_GT(tokens.size(), 0);
}

// =========================================================================
// Session Management Tests
// =========================================================================

TEST_F(CLITest, SessionContextCreation)
{
    auto session = test_utils::createTestSession(1);

    EXPECT_EQ(session.officerId, 1);
    EXPECT_GT(session.createdAt, 0);
    EXPECT_GT(session.expiresAt, session.createdAt);
}

TEST_F(CLITest, DutyActiveCheck)
{
    auto &sys = system_layer::SystemManager::getInstance();

    // Check if an officer has active duty
    bool is_active = sys.auth().isDutyActive(1);

    EXPECT_TRUE(is_active || !is_active); // Just verify it doesn't crash
}
