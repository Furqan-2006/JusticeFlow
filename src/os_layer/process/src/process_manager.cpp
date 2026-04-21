#include "../include/process_manager.h"
#include "common/logger.h"
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>

ProcessManager &ProcessManager::getInstance()
{
    static ProcessManager instance;
    return instance;
}

JusticeFlow::ResultCode ProcessManager::reapOne(pid_t pid, ProcessAction &out_action)
{
    out_action = ProcessAction::NONE;
    int status;

    pid_t result = waitpid(pid, &status, WNOHANG);

    if (result == 0)
    {
        return JusticeFlow::ResultCode::INVALID_STATE;
    }
    else if (result < 0)
    {
        return JusticeFlow::ResultCode::NOT_FOUND;
    }

    ProcessRegistry &registry = ProcessRegistry::getInstance();
    registry.updateState(pid, ProcessState::REAPED);

    bool clean_exit = (WIFEXITED(status) && WEXITSTATUS(status) == 0);

    if (!clean_exit)
    {
        ProcessRecord record;
        if (registry.getRecord(pid, record) == JusticeFlow::ResultCode::OK)
        {
            record.restart_count++;

            char log_buf[256];
            std::snprintf(log_buf, sizeof(log_buf),
                          "Process %s (PID: %d) exited abnormally. Restart count: %d/%d",
                          record.agent_name, pid, record.restart_count, MAX_RESTARTS);
            Logger::error(log_buf);

            registry.registerProcess(pid, record);

            if (record.restart_count <= MAX_RESTARTS)
            {
                out_action = ProcessAction::RESTART;
            }
            else
            {
                out_action = ProcessAction::ESCALATE_ERROR;
            }
        }
    }
    else
    {
        char log_buf[128];
        std::snprintf(log_buf, sizeof(log_buf), "Process PID: %d exited cleanly.", pid);
        Logger::info(log_buf);
    }

    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode ProcessManager::reapAll()
{
    ProcessRegistry &registry = ProcessRegistry::getInstance();
    std::vector<pid_t> pids;

    if (registry.getAllPids(pids) != JusticeFlow::ResultCode::OK)
    {
        return JusticeFlow::ResultCode::INVALID_STATE;
    }

    Logger::info("ProcessManager: Reaping all processes for shutdown...");

    for (pid_t pid : pids)
    {
        int status;
        if (waitpid(pid, &status, 0) > 0)
        {
            registry.updateState(pid, ProcessState::REAPED);
        }
    }

    return JusticeFlow::ResultCode::OK;
}