#include "os_layer/os_layer.h"
#include "shr_infra/auth/include/auth_module.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <signal.h>
#include <unistd.h>

TEST(AuthModule, TokenGeneratorCreatesUuidV4)
{
    std::string token = auth::token_generator::generate();
    ASSERT_EQ(token.size(), 36u);
    EXPECT_EQ(token[8], '-');
    EXPECT_EQ(token[13], '-');
    EXPECT_EQ(token[18], '-');
    EXPECT_EQ(token[23], '-');
    EXPECT_EQ(token[14], '4');
}

TEST(OSLayer, MutexSemaphoreCondVarBasic)
{
    Mutex m;
    CondVar cv;
    Semaphore sem(1);

    bool ready = false;

    std::thread producer([&]()
                         {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        {
            MutexGuard lock(m);
            ready = true;
        }
        cv.signal(); });

    {
        MutexGuard lock(m);
        while (!ready)
        {
            cv.wait(m);
        }
    }

    producer.join();

    {
        SemGuard g(sem);
        EXPECT_EQ(sem.get_value(), 0);
    }
    EXPECT_EQ(sem.get_value(), 1);
}

TEST(OSLayer, SupervisorLoopTickAndDrain)
{
    SignalHandler::init();
    Scheduler::getInstance().setState(SchedulerState::RUNNING);

    ASSERT_EQ(Timer::arm(1), JusticeFlow::ResultCode::OK);

    for (int i = 0; i < 20; ++i)
    {
        SignalHandler::processPendingSignals();
        usleep(10 * 1000);
    }

    raise(SIGTERM);

    for (int i = 0; i < 200; ++i)
    {
        SignalHandler::processPendingSignals();
        if (Scheduler::getInstance().getState() == SchedulerState::STOPPED)
            break;
        usleep(10 * 1000);
    }

    EXPECT_EQ(Scheduler::getInstance().getState(), SchedulerState::STOPPED);
}