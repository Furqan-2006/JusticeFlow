// tests/orchestrator_test.cpp
#include "test_common.h"
#include <thread>

/**
 * @class OrchestratorTest
 * @brief Tests for system orchestration and initialization
 */
class OrchestratorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Do not auto-initialize
    }

    void TearDown() override
    {
        auto &sys = system_layer::SystemManager::getInstance();
        sys.shutdown();
    }
};

// =========================================================================
// Initialization Tests
// =========================================================================

TEST_F(OrchestratorTest, SystemManagerSingleton)
{
    auto &sys1 = system_layer::SystemManager::getInstance();
    auto &sys2 = system_layer::SystemManager::getInstance();

    EXPECT_EQ(&sys1, &sys2);
}

TEST_F(OrchestratorTest, InitializesSystemLayer)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    config.audit_db_conninfo = "host=/var/run/postgresql dbname=justiceflow_test";

    auto result = sys.init(config);

    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(sys.isInitialized());
}

TEST_F(OrchestratorTest, ShutdownAfterInit)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    config.audit_db_conninfo = "host=/var/run/postgresql dbname=justiceflow_test";

    auto init_result = sys.init(config);
    EXPECT_TRUE(init_result.ok());

    sys.shutdown();

    EXPECT_FALSE(sys.isInitialized());
}

TEST_F(OrchestratorTest, InjectionGuard)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    auto init_result = sys.init(config);

    // After init, injection should throw
    EXPECT_THROW(
        {
            sys.injectAuth({});
        },
        std::logic_error);
}

// =========================================================================
// Sub-Facade Access Tests
// =========================================================================

TEST_F(OrchestratorTest, AccessAuthFacade)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    auto &auth = sys.auth();
    EXPECT_NE(&auth, nullptr);
}

TEST_F(OrchestratorTest, AccessCaseFacade)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    auto &cases = sys.cases();
    EXPECT_NE(&cases, nullptr);
}

TEST_F(OrchestratorTest, AccessInvestigationFacade)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    auto &investigation = sys.investigation();
    EXPECT_NE(&investigation, nullptr);
}

TEST_F(OrchestratorTest, AccessPersonnelFacade)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    auto &personnel = sys.personnel();
    EXPECT_NE(&personnel, nullptr);
}

TEST_F(OrchestratorTest, AccessDutyFacade)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    auto &duty = sys.duty();
    EXPECT_NE(&duty, nullptr);
}

TEST_F(OrchestratorTest, AccessEnforcementFacade)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    auto &enforcement = sys.enforcement();
    EXPECT_NE(&enforcement, nullptr);
}

TEST_F(OrchestratorTest, AccessAuditFacade)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    auto &audit = sys.audit();
    EXPECT_NE(&audit, nullptr);
}

TEST_F(OrchestratorTest, AccessForensicFacade)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    auto &forensic = sys.forensic();
    EXPECT_NE(&forensic, nullptr);
}

// =========================================================================
// Result Type Tests
// =========================================================================

TEST(SystemResultTest, SuccessResult)
{
    auto result = system_layer::SystemResult<int>::success(42);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value, 42);
    EXPECT_EQ(result.code, JusticeFlow::ResultCode::OK);
}

TEST(SystemResultTest, FailureResult)
{
    auto result = system_layer::SystemResult<int>::failure(JusticeFlow::ResultCode::NOT_FOUND);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, JusticeFlow::ResultCode::NOT_FOUND);
}

TEST(SystemResultTest, VoidSuccessResult)
{
    auto result = system_layer::SystemResult<void>::success();

    EXPECT_TRUE(result.ok());
}

TEST(SystemResultTest, VoidFailureResult)
{
    auto result = system_layer::SystemResult<void>::failure(JusticeFlow::ResultCode::AUTH_FAILED);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.code, JusticeFlow::ResultCode::AUTH_FAILED);
}

// =========================================================================
// Adapter Tests
// =========================================================================

TEST_F(OrchestratorTest, CustomAdapterInjection)
{
    auto &sys = system_layer::SystemManager::getInstance();

    // Inject an explicit empty adapter handle before init
    sys.injectAuth({});

    system_layer::SystemInitConfig config;
    auto result = sys.init(config);

    EXPECT_TRUE(result.ok());
}

// =========================================================================
// Thread Safety Tests
// =========================================================================

TEST_F(OrchestratorTest, ConcurrentAccess)
{
    auto &sys = system_layer::SystemManager::getInstance();

    system_layer::SystemInitConfig config;
    sys.init(config);

    std::vector<std::thread> threads;

    for (int i = 0; i < 5; ++i)
    {
        threads.emplace_back([&sys]()
                             {
            auto &auth = sys.auth();
            EXPECT_NE(&auth, nullptr); });
    }

    for (auto &t : threads)
    {
        t.join();
    }
}