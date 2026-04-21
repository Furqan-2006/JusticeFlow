#include "os_layer/scheduler/include/scheduler.h"
#include "os_layer/scheduler/include/timer.h"
#include "../include/process_spawner.h"

#include "../../process/include/process_manager.h"
#include "../../ipc/include/ipc_manager.h"
#include "../..//threading/include/thread_pool.h"
#include "../../memory/include/mmap_handler.h"
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

        // 1. Read the status from the agent's FIFO via IPC Manager
        if (ipc::IpcManager::getInstance().readAgentStatus(agent_idx, msg) == JusticeFlow::ResultCode::OK)
        {

            AgentStatus new_status;
            std::strncpy(new_status.agent_name, msg.agent_name, sizeof(new_status.agent_name) - 1);
            std::strncpy(new_status.error_detail, msg.error_detail, sizeof(new_status.error_detail) - 1);
            new_status.current_status = msg.status_code;
            new_status.last_updated = time(nullptr);

            // 2. Update Shared Memory via IPC Manager
            ipc::IpcManager::getInstance().updateAgentStatus(agent_idx, new_status);

            // 3. Broadcast to Abu Bakar's Thread Pool
            ThreadPool::getInstance().broadcastAiUpdate();
        }
    }
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

SchedulerState Scheduler::getState() const
{
    return state;
}

void Scheduler::setState(SchedulerState new_state)
{
    state = new_state;
    if (state == SchedulerState::DRAINING)
    {
        Logger::info("Scheduler entering DRAINING state. Initiating teardown.");

        Timer::disarm();
        ProcessManager::getInstance().reapAll();
        ThreadPool::getInstance().shutdown();
        ipc::IpcManager::getInstance().disconnectDatabase();

        state = SchedulerState::STOPPED;
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

            for (auto obs : observers)
            {
                obs->onJobComplete(job, JusticeFlow::ResultCode::OK);
            }
        }
    }
}