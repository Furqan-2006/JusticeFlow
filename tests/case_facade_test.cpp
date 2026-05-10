#include "test_common.h"

TEST_F(JusticeFlowTestFixture, CaseFacadeUnknownIdHandling)
{
    auto &sys = system_layer::SystemManager::getInstance();
    auto missing_case = sys.cases().getCaseById(nullptr, -1);
    EXPECT_FALSE(missing_case.ok());

    auto victims = sys.cases().getVictimsByCase(nullptr, -1);
    EXPECT_FALSE(victims.ok());

    auto witnesses = sys.cases().getWitnessesByCase(nullptr, -1);
    EXPECT_FALSE(witnesses.ok());

    auto accused = sys.cases().getAccusedByCase(nullptr, -1);
    EXPECT_FALSE(accused.ok());

    auto vehicles = sys.cases().getVehiclesByCase(nullptr, -1);
    EXPECT_FALSE(vehicles.ok());
}
