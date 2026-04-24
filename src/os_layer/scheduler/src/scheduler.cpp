#include "os_layer/scheduler/include/scheduler.h"
#include "os_layer/scheduler/include/timer.h"
#include "common/logger.h"

// Cross-module dependencies (Assume these stubs exist until Abdullah/Abu Bakar/Shared push their code)
#include "os_layer/ipc/include/ipc_module.h"        // For IpcManager::updateAgentStatus
#include "../../memory/include/mmap_handler.h"   // For msync
#include "os_layer/threading/include/thread_pool.h" // For CondVar broadcast

/* ==========================================
 * StatusTableUpdater Implementation
 * ========================================== */
void StatusTableUpdater::onJobComplete(Job *job, JusticeFlow::ResultCode result)
{
    // In a full implementation, we'd cast the Job to AgentJob to get specific details.
    Logger::debug("StatusTableUpdater: Agent job completed. Updating SHM.");

    // 1. Update the Shared Memory via Abdullah's IPC Manager
    // IpcManager::getInstance().updateAgentStatus(...);

    // 2. Flush the SHM write to ensure other processes see it immediately (Shared requirement)
    // MmapHandler::msync(shm_address, shm_size);

    // 3. Broadcast to Abu Bakar's thread pool CondVar that new AI data is ready
    // ThreadPool::broadcastAiUpdate();
}

/* ==========================================
 * Scheduler Implementation
 * ========================================== */
Scheduler::Scheduler() : state(SchedulerState::INITIALIZING), current_tick(0), job_count(0)
{
    for (int i = 0; i < MAX_JOBS; ++i)
    {
        job_registry[i] = nullptr;
    }
}

Scheduler &Scheduler::getInstance()
{
    static Scheduler instance;
    return instance;
}

SchedulerState Scheduler::getState() const { return state; }

void Scheduler::setState(SchedulerState new_state)
{
    state = new_state;
    if (state == SchedulerState::DRAINING)
    {
        Logger::info("Scheduler entering DRAINING state. Disarming timer and initiating teardown.");
        Timer::disarm();

        // Initiate graceful shutdown logic here:
        // 1. ThreadPool::shutdown()
        // 2. IpcManager::getInstance().teardown()
    }
}

void Scheduler::registerJob(Job *job)
{
    if (job_count < MAX_JOBS)
    {
        job_registry[job_count++] = job;
    }
}

void Scheduler::registerObserver(JobObserver *obs)
{
    observers.push_back(obs);
}

void Scheduler::tick()
{
    if (state != SchedulerState::RUNNING)
        return;

    current_tick++;

    for (int i = 0; i < job_count; ++i)
    {
        Job *job = job_registry[i];
        if (job && current_tick >= job->next_fire_tick)
        {
            job->execute();
            job->next_fire_tick = current_tick + job->interval_ticks;

            // Notify observers (e.g., StatusTableUpdater)
            for (auto obs : observers)
            {
                obs->onJobComplete(job, JusticeFlow::ResultCode::OK);
            }
        }
    }
}