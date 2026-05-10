#include "test_common.h"

TEST_F(JusticeFlowTestFixture, EnforcementFacadeSmoke)
{
    auto &sys = system_layer::SystemManager::getInstance();
    auto warrants = sys.enforcement().getWarrantsByCase(nullptr, -1);
    EXPECT_FALSE(warrants.ok());

    auto arrests = sys.enforcement().getArrestsByCase(nullptr, -1);
    EXPECT_FALSE(arrests.ok());
}
