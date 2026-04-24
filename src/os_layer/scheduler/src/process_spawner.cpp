#include "../include/process_spawner.h"
#include "os_layer/process/include/process_registry.h"
#include "common/logger.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

void AgentJob::execute()
{
    // 1. Remove old FIFO if it exists
    unlink(fifo_path.c_str());

    // CRITICAL FIX #11.3: Changed permissions from 0666 (world-writable) to 0600 (owner-only)
    // This prevents any local user from spoofing agent results
    if (mkfifo(fifo_path.c_str(), 0600) == -1)
    {
        Logger::error("Failed to create FIFO for agent");
        return;
    }

    // 2. Fork
    pid_t pid = fork();

    if (pid < 0)
    {
        Logger::error("Fork failed for agent");
        cleanupFifo();
        return;
    }

    if (pid == 0)
    {
        // Child Process: exec the Python agent
        // TODO: Make Python path configurable via environment or config file
        execl("/usr/bin/python3", "python3", executable_path.c_str(), fifo_path.c_str(), (char *)nullptr);

        // If execl returns, it failed
        _exit(1);
    }

    // 3. Parent Process: Register immediately, do not block
    ProcessRecord record;
    record.pid = pid;
    std::strncpy(record.agent_name, agent_name.c_str(), sizeof(record.agent_name) - 1);
    std::strncpy(record.fifo_path, fifo_path.c_str(), sizeof(record.fifo_path) - 1);
    record.state = ProcessState::RUNNING;

    ProcessRegistry::getInstance().registerProcess(pid, record);

    char log_buf[128];
    std::snprintf(log_buf, sizeof(log_buf), "Agent %s (PID: %d) spawned successfully", agent_name.c_str(), pid);
    Logger::info(log_buf);
}

void AgentJob::cleanupFifo()
{
    unlink(fifo_path.c_str());
}

void AgentJob::onProcessReaped()
{
    cleanupFifo();
}