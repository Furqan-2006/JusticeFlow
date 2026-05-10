#include "test_common.h"

TEST_F(JusticeFlowTestFixture, ForensicFacadeSmoke)
{
    auto &sys = system_layer::SystemManager::getInstance();
    auto list = sys.forensic().getForensicRequestsByCase("", -1);
    EXPECT_FALSE(list.ok());

    auto pending = sys.forensic().getPendingForensicRequests("", -1);
    EXPECT_FALSE(pending.ok());
}
