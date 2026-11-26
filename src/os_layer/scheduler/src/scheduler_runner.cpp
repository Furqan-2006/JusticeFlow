#include "../include/scheduler_runner.h"

#include <chrono>
#include <cstdlib>
#include <string>

#include "../include/job_executor.h"
#include "../include/process_spawner.h"
#include "common/logger.h"
#include "common/ipc_types.h"

namespace
{
    int envInt(const char *name, int fallback)
    {
        const char *v = std::getenv(name);
        if (!v || !*v)
            return fallback;
        const int n = std::atoi(v);
        return n > 0 ? n : fallback;
    }
} // namespace

void SchedulerRunner::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return;

    const int expire_every = envInt("JF_EXPIRE_INTERVAL_S", 30);
    const int agents_every = envInt("JF_AGENT_INTERVAL_S", 300);
    const char *dir_env = std::getenv("JF_AGENT_SCRIPT_DIR");
    const std::string dir = (dir_env && *dir_env) ? dir_env : "src/ai_agents";

    auto &scheduler = Scheduler::getInstance();

    auto expire = std::make_unique<ExpireWarrantsJob>(expire_every, nullptr);
    expire->next_fire_tick = 3;
    scheduler.registerJob(expire.get());
    jobs_.push_back(std::move(expire));

    struct AgentSpec
    {
        const char *name;
        const char *script;
        int index;
        int first_tick;
    };
    // Staggered so three Python processes do not start in the same second.
    const AgentSpec specs[] = {
        {"hotspot", "ha.py", AGENT_INDEX_HOTSPOT, 5},
        {"priority", "pa.py", AGENT_INDEX_PRIORITY, 8},
        {"workload", "workload_agent.py", AGENT_INDEX_WORKLOAD, 11},
    };
    for (const auto &s : specs)
    {
        auto job = std::make_unique<AgentJob>(agents_every, s.name, (dir + "/" + s.script).c_str(), s.index);
        job->next_fire_tick = s.first_tick;
        scheduler.registerJob(job.get());
        jobs_.push_back(std::move(job));
    }

    scheduler.setState(SchedulerState::RUNNING);
    stopping_ = false;
    running_ = true;
    thread_ = std::thread(&SchedulerRunner::loop, this);
    Logger::info("Scheduler running: warrant expiry and AI agents registered");
}

void SchedulerRunner::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
        stopping_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();

    for (auto &job : jobs_)
    {
        if (auto *agent = dynamic_cast<AgentJob *>(job.get()))
            agent->terminate();
    }

    Scheduler::getInstance().setState(SchedulerState::STOPPED);
    running_ = false;
    Logger::info("Scheduler stopped");
}

void SchedulerRunner::loop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_)
    {
        if (wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_; }))
            break;
        lock.unlock();
        Scheduler::getInstance().tick();
        lock.lock();
    }
}
