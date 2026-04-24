#pragma once

#include "scheduler.h"
#include "common/ipc_types.h"
#include <string>

// CRITICAL FIX #9.5: AgentJob now declares getAgentIndex()
class AgentJob : public Job
{
private:
    std::string agent_name;
    std::string fifo_path;
    std::string executable_path;
    int agent_index; // NEW: Track which AI agent [0]=hotspot, [1]=priority, [2]=workload

    void cleanupFifo();

public:
    AgentJob(int interval, const char *name, const char *fifo, const char *exec_path, int idx)
        : Job(interval), agent_name(name), fifo_path(fifo), executable_path(exec_path), agent_index(idx) {}

    virtual ~AgentJob() = default;

    void execute() override;
    void onProcessReaped();

    // CRITICAL FIX #9.5: Now declared
    int getAgentIndex() const { return agent_index; }
};