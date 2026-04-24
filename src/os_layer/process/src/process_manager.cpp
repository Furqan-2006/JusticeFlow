#include "../include/process_manager.h"
#include "common/logger.h"
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <chrono>

ProcessManager &ProcessManager::getInstance()
{
    static ProcessManager instance;
    return instance;
}

JusticeFlow::ResultCode ProcessManager::reapOne(pid_t pid, ProcessAction &out_action)
{
    out_action = ProcessAction::NONE;
    int status;

    // Non-blocking reap with WNOHANG
    pid_t result = waitpid(pid, &status, WNOHANG);

    if (result == 0)
    {
        // Child is still alive (not yet reaped)
        return JusticeFlow::ResultCode::NOT_FOUND;
    }
    else if (result < 0)
    {
        // Error from waitpid (ECHILD = child already reaped, EINVAL = invalid pid, etc)
        if (errno == ECHILD)
        {
            // Child was already reaped by someone else - treat as OK
            return JusticeFlow::ResultCode::OK;
        }
        return JusticeFlow::ResultCode::DB_ERROR;
    }

    // result == pid: Child was successfully reaped
    ProcessRegistry &registry = ProcessRegistry::getInstance();
    registry.updateState(pid, ProcessState::REAPED);

    bool clean_exit = (WIFEXITED(status) && WEXITSTATUS(status) == 0);

    if (!clean_exit)
    {
        ProcessRecord record;
        if (registry.getRecord(pid, record) == JusticeFlow::ResultCode::OK)
        {
            // CRITICAL FIX: Use atomic incrementRestartCount() instead of local increment
            // This ensures the count is immediately persisted in the registry
            int new_restart_count = 0;
            if (registry.incrementRestartCount(pid, new_restart_count) == JusticeFlow::ResultCode::OK)
            {
                char log_buf[256];
                std::snprintf(log_buf, sizeof(log_buf),
                              "Process %s (PID: %d) exited abnormally. Restart count: %d/%d",
                              record.agent_name, pid, new_restart_count, MAX_RESTARTS);
                Logger::error(log_buf);

                if (new_restart_count <= MAX_RESTARTS)
                {
                    out_action = ProcessAction::RESTART;
                }
                else
                {
                    out_action = ProcessAction::ESCALATE_ERROR;
                }
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

JusticeFlow::ResultCode ProcessManager::sendSignal(pid_t pid, int signal)
{
    if (pid <= 0)
    {
        return JusticeFlow::ResultCode::INVALID_INPUT;
    }

    if (kill(pid, signal) == -1)
    {
        if (errno == ESRCH)
        {
            // Process does not exist
            return JusticeFlow::ResultCode::NOT_FOUND;
        }
        return JusticeFlow::ResultCode::DB_ERROR;
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

    // PHASE 1: Send SIGTERM to all processes (graceful shutdown)
    for (pid_t pid : pids)
    {
        if (sendSignal(pid, SIGTERM) == JusticeFlow::ResultCode::OK)
        {
            char log_buf[128];
            std::snprintf(log_buf, sizeof(log_buf),
                          "ProcessManager: Sent SIGTERM to PID %d", pid);
            Logger::info(log_buf);
        }
    }

    // PHASE 2: Non-blocking wait with timeout
    // Attempt to reap each process with a timeout window
    auto start_time = std::chrono::steady_clock::now();
    std::vector<pid_t> remaining = pids;

    while (!remaining.empty())
    {
        auto now = std::chrono::steady_clock::now();
        int elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        if (elapsed_sec >= SHUTDOWN_TIMEOUT_SEC)
        {
            // Timeout exceeded - move to SIGKILL phase
            break;
        }

        std::vector<pid_t> still_alive;

        for (pid_t pid : remaining)
        {
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);

            if (result == pid)
            {
                // Successfully reaped
                registry.updateState(pid, ProcessState::REAPED);
                char log_buf[128];
                std::snprintf(log_buf, sizeof(log_buf),
                              "ProcessManager: PID %d reaped after SIGTERM", pid);
                Logger::info(log_buf);
            }
            else if (result == 0 || (result == -1 && errno == EINTR))
            {
                // Still alive or interrupted - keep trying
                still_alive.push_back(pid);
            }
            else if (result == -1 && errno == ECHILD)
            {
                // Already reaped by someone else
                registry.updateState(pid, ProcessState::REAPED);
            }
        }

        remaining = still_alive;

        // Brief sleep to avoid busy-waiting
        if (!remaining.empty())
        {
            usleep(10000); // 10ms
        }
    }

    // PHASE 3: Send SIGKILL to any stragglers
    if (!remaining.empty())
    {
        Logger::info("ProcessManager: Timeout expired. Sending SIGKILL to stragglers...");

        for (pid_t pid : remaining)
        {
            if (sendSignal(pid, SIGKILL) == JusticeFlow::ResultCode::OK)
            {
                char log_buf[128];
                std::snprintf(log_buf, sizeof(log_buf),
                              "ProcessManager: Sent SIGKILL to PID %d", pid);
                Logger::info(log_buf);
            }
        }

        // Final reap after SIGKILL
        for (pid_t pid : remaining)
        {
            int status;
            if (waitpid(pid, &status, 0) > 0) // Blocking wait for SIGKILL'd processes
            {
                registry.updateState(pid, ProcessState::REAPED);
            }
        }
    }

    Logger::info("ProcessManager: Shutdown complete.");
    return JusticeFlow::ResultCode::OK;
}
