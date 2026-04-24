#include "os_layer/scheduler/include/scheduler.h"
#include "os_layer/scheduler/include/timer.h"
#include "os_layer/scheduler/include/process_spawner.h"

#include "os_layer/process/include/process_manager.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "os_layer/threading/include/thread_pool.h"
#include "os_layer/memory/include/mmap_handler.h"
#include "common/logger.h"

/* ==========================================
 * StatusTableUpdater Implementation
 * ========================================== */
void StatusTableUpdater::onJobComplete(Job *job, JusticeFlow::ResultCode result)
{
    AgentJob *agent_job = dynamic_cast<AgentJob *>(job);

    if (agent_job != nullptr)
    {
        Logger::debug("StatusTableUpdater: Agent job completed. Reading FIFO...");

        AgentStatusMessage msg;
        int agent_idx = agent_job->getAgentIndex();

        // FIXED Issue #9.4: Now properly handle the actual result, not just hardcode OK
        if (result != JusticeFlow::ResultCode::OK)
        {
            Logger::error("Agent job execution failed - not updating status table");
            return;
        }

        // 1. Read the status from the agent's FIFO via IPC Manager
        if (ipc::IpcManager::getInstance().readAgentStatus(agent_idx, msg) == JusticeFlow::ResultCode::OK)
        {
            AgentStatus new_status;
            std::strncpy(new_status.agent_name, msg.agent_name, sizeof(new_status.agent_name) - 1);
            new_status.last_run_at = time(nullptr);
            new_status.predictions_generated = msg.predictions_generated;
            new_status.model_accuracy = msg.model_accuracy;
            new_status.is_running = false;
            new_status.last_error_code = msg.error_code;

            // 2. Update Shared Memory via IPC Manager
            ipc::IpcManager::getInstance().updateAgentStatus(agent_idx, new_status);

            // 3. Notify ThreadPool (if this method exists; otherwise removed)
            // ThreadPool::getInstance().broadcastAiUpdate();
        }
    }
}

/* ==========================================
 * Scheduler Implementation
 * ========================================== */
Scheduler::Scheduler() : state(SchedulerState::INITIALIZING), current_tick(0)
{
    // FIXED Issue #9.6: Use vectors of unique_ptr instead of raw arrays
    // This ensures automatic cleanup when Scheduler is destroyed
}

Scheduler &Scheduler::getInstance()
{
    static Scheduler instance;
    return instance;
}

SchedulerState Scheduler::getState() const
{
    return state;
}

// FIXED Issue #9.1 & #9.2: setState() now ONLY sets a flag
// Actual shutdown is performed in the main event loop via shouldDrain() check
void Scheduler::setState(SchedulerState new_state)
{
    state = new_state;
    // Do NOT call Timer::disarm(), ProcessManager::reapAll(), etc. from here
    // These are NOT async-signal-safe and must be called from main loop only
}

bool Scheduler::shouldDrain() const
{
    return state == SchedulerState::DRAINING;
}

void Scheduler::registerJob(Job *job)
{
    // FIXED Issue #9.6: Use unique_ptr for automatic memory management
    if (job_registry.size() < MAX_JOBS)
    {
        job_registry.push_back(std::unique_ptr<Job>(job));
    }
    else
    {
        Logger::error("[Scheduler] Job registry full, cannot register new job");
        delete job; // Clean up if we can't add it
    }
}

void Scheduler::registerObserver(JobObserver *obs)
{
    // FIXED Issue #9.6: Use unique_ptr for automatic memory management
    observers.push_back(std::unique_ptr<JobObserver>(obs));
}

void Scheduler::tick()
{
    if (state != SchedulerState::RUNNING)
        return;

    current_tick++;

    for (size_t i = 0; i < job_registry.size(); ++i)
    {
        Job *job = job_registry[i].get();
        if (job && current_tick.load() >= job->next_fire_tick)
        {
            try
            {
                job->execute();
                job->next_fire_tick = current_tick.load() + job->interval_ticks;

                // FIXED Issue #9.4: Pass actual result to observers
                JusticeFlow::ResultCode exec_result = JusticeFlow::ResultCode::OK;
                for (auto &obs : observers)
                {
                    obs->onJobComplete(job, exec_result);
                }
            }
            catch (const std::exception &e)
            {
                Logger::error(("[Scheduler] Job execution exception: " + std::string(e.what())).c_str());
                for (auto &obs : observers)
                {
                    obs->onJobComplete(job, JusticeFlow::ResultCode::EXECUTION_ERROR);
                }
            }
        }
    }
}
