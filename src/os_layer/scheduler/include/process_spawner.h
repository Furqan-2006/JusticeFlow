#pragma once
#include "os_layer/scheduler/include/scheduler.h"
#include "common/ipc_types.h"
#include <string>

class AgentJob : public Job
{
private:
    std::string agent_name;
    std::string fifo_path;
    std::string executable_path;

    void cleanupFifo();

public:
    AgentJob(int interval, const char *name, const char *fifo, const char *exec_path)
        : Job(interval), agent_name(name), fifo_path(fifo), executable_path(exec_path) {}

    void execute() override;

    // Called by observers when process_manager reaps the child
    void onProcessReaped();
};