#include "os_layer/scheduler/include/scheduler.h"
#include "os_layer/scheduler/include/timer.h"
#include "../include/process_spawner.h"

#include "../../process/include/process_manager.h"
#include "../../ipc/include/ipc_manager.h"
#include "../../threading/include/thread_pool.h"

#include "common/logger.h"
#include "common/ipc_types.h"

#include <ctime>
#include <cstring>

/* ==========================================
 * StatusTableUpdater Implementation
 * ========================================== */
void StatusTableUpdater::onJobComplete(Job *job, JusticeFlow::ResultCode result)
{
    AgentJob *agent_job = dynamic_cast<AgentJob *>(job);
    if (agent_job == nullptr)
        return;

    if (result != JusticeFlow::ResultCode::OK)
    {
        Logger::error("StatusTableUpdater: job failed, not updating SHM.");
        return;
    }

    AgentStatusMessage msg;
    int agent_idx = agent_job->getAgentIndex();

    if (ipc::IpcManager::getInstance().readAgentStatus(agent_idx, msg) == JusticeFlow::ResultCode::OK)
    {
        AgentStatus st;
        std::memset(&st, 0, sizeof(st));
        std::strncpy(st.agent_name, msg.agent_name, sizeof(st.agent_name) - 1);
        st.current_status = msg.status_code;
        std::strncpy(st.error_detail, msg.error_detail, sizeof(st.error_detail) - 1);
        st.last_updated = msg.timestamp;

        ipc::IpcManager::getInstance().updateAgentStatus(agent_idx, st);

        ThreadPool::getInstance().broadcastAiUpdate();
    }
}

/* ==========================================
 * Scheduler Implementation
 * ========================================== */
Scheduler::Scheduler()
    : state(SchedulerState::INITIALIZING),
      drain_requested(false),
      current_tick(0),
      job_count(0)
{
    for (int i = 0; i < MAX_JOBS; ++i)
        job_registry[i] = nullptr;
}

Scheduler &Scheduler::getInstance()
{
    static Scheduler instance;
    return instance;
}

SchedulerState Scheduler::getState() const
{
    return state.load(std::memory_order_acquire);
}

void Scheduler::setState(SchedulerState new_state)
{
    state.store(new_state, std::memory_order_release);
}

void Scheduler::registerJob(Job *job)
{
    if (job_count < MAX_JOBS)
        job_registry[job_count++] = job;
}

void Scheduler::registerObserver(JobObserver *obs)
{
    observers.push_back(obs);
}

void Scheduler::tick()
{
    if (getState() != SchedulerState::RUNNING)
        return;

    current_tick++;

    for (int i = 0; i < job_count; ++i)
    {
        Job *job = job_registry[i];
        if (!job)
            continue;

        if (current_tick >= job->next_fire_tick)
        {
            JusticeFlow::ResultCode rc = JusticeFlow::ResultCode::OK;

            try
            {
                job->execute();
            }
            catch (...)
            {
                rc = JusticeFlow::ResultCode::INVALID_STATE;
            }

            job->next_fire_tick = current_tick + job->interval_ticks;

            for (auto obs : observers)
            {
                if (obs)
                    obs->onJobComplete(job, rc);
            }
        }
    }
}

void Scheduler::requestDrain()
{
    drain_requested.store(true, std::memory_order_release);
}

void Scheduler::handleDrainRequest()
{
    if (!drain_requested.load(std::memory_order_acquire))
        return;

    // Only perform drain once
    drain_requested.store(false, std::memory_order_release);

    Logger::info("Scheduler: drain requested. Performing teardown from safe context.");

    setState(SchedulerState::DRAINING);

    Timer::disarm();
    ProcessManager::getInstance().reapAll();
    ThreadPool::getInstance().shutdown();
    ipc::IpcManager::getInstance().disconnectDatabase();

    setState(SchedulerState::STOPPED);
    Logger::info("Scheduler: teardown complete. STOPPED.");
}