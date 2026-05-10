#include "test_common.h"

TEST(AuthUnitTest, ResultCodeValues)
{
    EXPECT_EQ(static_cast<int>(JusticeFlow::ResultCode::OK), 0);
    EXPECT_EQ(static_cast<int>(JusticeFlow::ResultCode::AUTH_FAILED), 1);
    EXPECT_EQ(static_cast<int>(JusticeFlow::ResultCode::DB_ERROR), 20);
}

TEST_F(JusticeFlowTestFixture, AuthRejectsEmptyAndInjectionCredentials)
{
    auto &sys = system_layer::SystemManager::getInstance();
    EXPECT_FALSE(sys.auth().login("", "").ok());
    EXPECT_FALSE(sys.auth().login("' OR 1=1 --", "x").ok());
}

TEST_F(JusticeFlowTestFixture, AuthValidLoginReturnsToken)
{
    auto &sys = system_layer::SystemManager::getInstance();
    auto login = sys.auth().login("42401-637951-0", "JusticeDemo@2026");
    ASSERT_TRUE(login.ok());
    ASSERT_FALSE(login.value.empty());

    auto validation = sys.auth().validateToken(login.value.c_str());
    EXPECT_TRUE(validation.ok());

    EXPECT_TRUE(sys.auth().logout(login.value.c_str()).ok());
    EXPECT_FALSE(sys.auth().validateToken(login.value.c_str()).ok());
}
