#include "os_layer/scheduler/include/scheduler.h"
#include "os_layer/scheduler/include/timer.h"
#include "common/logger.h"

Scheduler::Scheduler() : state(SchedulerState::INITIALIZING), current_tick(0), job_count(0) {
    for (int i = 0; i < MAX_JOBS; ++i) {
        job_registry[i] = nullptr;
    }
}

Scheduler& Scheduler::getInstance() {
    static Scheduler instance;
    return instance;
}

SchedulerState Scheduler::getState() const { return state; }

void Scheduler::setState(SchedulerState new_state) {
    state = new_state;
    if (state == SchedulerState::DRAINING) {
        Logger::info("Scheduler entering DRAINING state. Disarming timer.");
        Timer::disarm();
        // Trigger reapAll / graceful shutdown routines here
    }
}

void Scheduler::registerJob(Job* job) {
    if (job_count < MAX_JOBS) {
        job_registry[job_count++] = job;
    }
}

void Scheduler::registerObserver(JobObserver* obs) {
    observers.push_back(obs);
}

void Scheduler::tick() {
    if (state != SchedulerState::RUNNING) return;

    current_tick++;

    for (int i = 0; i < job_count; ++i) {
        Job* job = job_registry[i];
        if (job && current_tick >= job->next_fire_tick) {
            job->execute();
            job->next_fire_tick = current_tick + job->interval_ticks;
            
            // Notify observers (e.g., StatusTableUpdater)
            for (auto obs : observers) {
                obs->onJobComplete(job, ResultCode::OK);
            }
        }
    }
}