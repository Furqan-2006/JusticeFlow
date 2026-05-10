#include <gtest/gtest.h>
#include "../src/os_layer/scheduler/include/timer.h"

TEST(SchedulerTest, TimerArmDisarm)
{
    auto arm = Timer::arm(1);
    EXPECT_TRUE(arm == JusticeFlow::ResultCode::OK || arm == JusticeFlow::ResultCode::INVALID_STATE);

    auto disarm = Timer::disarm();
    EXPECT_TRUE(disarm == JusticeFlow::ResultCode::OK || disarm == JusticeFlow::ResultCode::INVALID_STATE);
}
