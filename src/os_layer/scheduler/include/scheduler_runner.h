#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "scheduler.h"

/**
 * Drives the Scheduler from an ordinary thread that ticks once a second.
 *
 * (The original design ticked from a SIGALRM handler. The daemon now receives
 * its stop signals through sigwait() on the main thread, so the clock is a
 * thread instead; the jobs themselves are unchanged.)
 *
 * start() registers the standing jobs:
 *   - ExpireWarrantsJob  every JF_EXPIRE_INTERVAL_S seconds (default 30)
 *   - hotspot, priority and workload agents every JF_AGENT_INTERVAL_S seconds
 *     (default 300), the first run a few seconds after start
 * Set JF_AGENT_SCRIPT_DIR to where the agent scripts live (default src/ai_agents).
 */
class SchedulerRunner
{
public:
    SchedulerRunner() = default;
    ~SchedulerRunner() { stop(); }

    SchedulerRunner(const SchedulerRunner &) = delete;
    SchedulerRunner &operator=(const SchedulerRunner &) = delete;

    void start();
    void stop();

private:
    void loop();

    std::vector<std::unique_ptr<Job>> jobs_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_{false};
    bool running_{false};
};
