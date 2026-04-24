#pragma once

#include "common/constants.h"
#include <vector>
#include <atomic>

class Job;

// Observer interface for job completion/status updates
class JobObserver
{
public:
    virtual ~JobObserver() = default;
    virtual void onJobComplete(Job *job, JusticeFlow::ResultCode result) = 0;
};

// Base Job interface
class Job
{
public:
    int interval_ticks;
    int next_fire_tick;

    explicit Job(int interval) : interval_ticks(interval), next_fire_tick(interval) {}
    virtual ~Job() = default;

    virtual void execute() = 0;
};

// Concrete observer that updates SHM and wakes the ThreadPool
class StatusTableUpdater : public JobObserver
{
public:
    void onJobComplete(Job *job, JusticeFlow::ResultCode result) override;
};

enum class SchedulerState
{
    INITIALIZING,
    RUNNING,
    DRAINING,
    STOPPED
};

class Scheduler
{
private:
    static constexpr int MAX_JOBS = 32;

    std::atomic<SchedulerState> state;
    std::atomic<bool> drain_requested;

    long current_tick;

    Job *job_registry[MAX_JOBS];
    int job_count;

    std::vector<JobObserver *> observers;

    Scheduler();
    ~Scheduler() = default;

    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;

public:
    static Scheduler &getInstance();

    SchedulerState getState() const;

    void setState(SchedulerState new_state);

    void registerJob(Job *job);
    void registerObserver(JobObserver *obs);

    // Called ONLY from safe context (main loop) via SignalHandler::processPendingSignals()
    void tick();

    // Called from signal handler: safe (atomic store only)
    void requestDrain();

    // Called from safe context: performs teardown if drain requested
    void handleDrainRequest();
};