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

ResultCode ProcessManager::reapOne(pid_t pid, ProcessAction &out_action)
{
    out_action = ProcessAction::NONE;
    int status;

    pid_t result = waitpid(pid, &status, WNOHANG);

    if (result = 0)
    {
        return ResultCode::INVALID_STATE;
    }
    else if (result < 0)
    {
        return ResultCode::NOT_FOUND;
    }

    ProcessRegistry &registry = ProcessRegistry::getProcessRegistry();
    registry.updateState(pid, ProcessState::REAPED);

    bool clean_exit = (WIFEXITED(status) && WEXITSTATUS(status) == 0);

    if (!clean_exit)
    {
        ProcessRecord record;
        if (registry_.getRecord(pid, record) == ResultCode::OK)
        {
            record.restart_count++;

            char log_buff[256];
            std::snprintf(log_buff, sizeof(log_buff), "Process %s (PID %d) exited abnormally. Restart count: %d/%d", record.agent_name, pid, record.restart_count, MAX_RESTARTS);
            Logger::error(log_buff);

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
        char log_buff[128];
        std::snprintf(log_buff, sizeof(log_buff), "Process PID: %d exited cleanly.", pid);
        Logger::info(log_buff);
    }

    return ResultCode::OK;
}

ResultCode ProcessManager::reapAll()
{
    ProcessRegistry &registry = ProcessRegistry::getProcessRegistry();
    std::vector<pid_t> pids;

    if (registry.getAllPids(pids) != ResultCode::OK)
    {
        return ResultCode::INVALID_STATE;
    }

    Logger::info("ProcessManager: Reaping all the processes for shutdown...");

    for (pid_t pid : pids)
    {
        int status;
        if (waitpid(pid, &status, 0) > 0)
        {
            registry.updateState(pid, ProcessState::REAPED);
        }
    }
    return ResultCode::OK;
}