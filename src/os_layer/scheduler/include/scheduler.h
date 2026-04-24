#pragma once

#include "common/constants.h"
#include <vector>
#include <atomic>
#include <memory>

enum class SchedulerState
{
    INITIALIZING,
    RUNNING,
    DRAINING,
    STOPPED
};

// Base Job interface
class Job
{
public:
    int interval_ticks;
    int next_fire_tick;

    Job(int interval) : interval_ticks(interval), next_fire_tick(interval) {}
    virtual ~Job() = default;

    virtual void execute() = 0;
};

// Concrete observer that updates SHM and wakes the ThreadPool
class AgentJob : public Job
{
private:
    std::string agent_name;
    std::string fifo_path;
    std::string executable_path;
    int agent_index;

public:
    AgentJob(int interval, const char *name, const char *fifo, const char *exec_path, int idx)
        : Job(interval), agent_name(name), fifo_path(fifo), executable_path(exec_path), agent_index(idx) {}

    void execute() override;
    void onProcessReaped();

    // FIXED Issue #9.5: Declare getAgentIndex() method
    int getAgentIndex() const { return agent_index; }
    const std::string &getAgentName() const { return agent_name; }
    const std::string &getFifoPath() const { return fifo_path; }
};

// Observer interface for job completion/status updates
class JobObserver
{
public:
    virtual ~JobObserver() = default;
    virtual void onJobComplete(Job *job, JusticeFlow::ResultCode result) = 0;
};

// Concrete observer that updates SHM and wakes the ThreadPool
class StatusTableUpdater : public JobObserver
{
public:
    void onJobComplete(Job *job, JusticeFlow::ResultCode result) override;
};

class Scheduler
{
private:
    static constexpr int MAX_JOBS = 32;

    SchedulerState state;

    // FIXED Issue #9.7: Use atomic for tick counter accessed from signal handler
    std::atomic<long> current_tick;

    // FIXED Issue #9.6: Use smart pointers to prevent memory leaks
    std::vector<std::unique_ptr<Job>> job_registry;
    std::vector<std::unique_ptr<JobObserver>> observers;

    Scheduler();
    ~Scheduler() = default;

    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;

public:
    static Scheduler &getInstance();

    SchedulerState getState() const;

    // FIXED Issue #9.2: setState() now only sets a flag; actual shutdown happens in main loop
    void setState(SchedulerState new_state);

    void registerJob(Job *job);
    void registerObserver(JobObserver *obs);

    // Called by main event loop (NOT from signal handler)
    void tick();

    // Safe to call from signal handler - only checks state
    bool shouldDrain() const;
};
