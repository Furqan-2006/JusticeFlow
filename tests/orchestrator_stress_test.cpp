#include "test_common.h"
#include <thread>
#include <vector>

TEST(OrchestratorStressTest, SingletonStableAcrossThreads)
{
    std::vector<std::thread> threads;
    std::vector<system_layer::SystemManager *> seen(1000, nullptr);

    for (int t = 0; t < 10; ++t)
    {
        threads.emplace_back([t, &seen]() {
            for (int i = 0; i < 100; ++i)
            {
                seen[t * 100 + i] = &system_layer::SystemManager::getInstance();
            }
        });
    }
    for (auto &th : threads)
        th.join();

    auto *first = seen.front();
    for (auto *ptr : seen)
        EXPECT_EQ(ptr, first);
}

TEST(OrchestratorStressTest, ReinitCycleDoesNotCrash)
{
    auto &sys = system_layer::SystemManager::getInstance();
    for (int i = 0; i < 5; ++i)
    {
        system_layer::SystemInitConfig cfg;
        auto r = sys.init(cfg);
        EXPECT_TRUE(r.ok());
        sys.shutdown();
    }
}
