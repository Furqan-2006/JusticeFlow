#include "os_layer/os_layer.h"
#include <gtest/gtest.h>
#include <unistd.h>
#include <signal.h>

TEST(OSLayer, SupervisorLoopRunsAndDrains)
{
    SignalHandler::init();

    // Start scheduler
    Scheduler::getInstance().setState(SchedulerState::RUNNING);

    // Arm a 1-second timer (generates SIGALRM)
    ASSERT_EQ(Timer::arm(1), JusticeFlow::ResultCode::OK);

    // Let it run briefly
    for (int i = 0; i < 50; ++i)
    {
        SignalHandler::processPendingSignals();
        usleep(10 * 1000);
    }

    // Trigger shutdown (SIGTERM handler requests drain)
    raise(SIGTERM);

    // Drain loop
    for (int i = 0; i < 200; ++i)
    {
        SignalHandler::processPendingSignals();
        if (Scheduler::getInstance().getState() == SchedulerState::STOPPED)
            break;
        usleep(10 * 1000);
    }

    EXPECT_EQ(Scheduler::getInstance().getState(), SchedulerState::STOPPED);
}