#pragma once

#include <sys/types.h>

#include <ctime>
#include <string>

#include "common/ipc_types.h"
#include "scheduler.h"

/**
 * Runs one Python AI agent as a child process on a schedule and reports how
 * it went in the shared status table.
 *
 * The agent does its work in the database; the daemon's part is lifecycle:
 *   execute()  starts the agent (skipped while the previous run is still going);
 *   onTick()   notices when it has exited, or kills it after a timeout, and
 *              records the outcome in shared memory.
 * Status code 0 means the last run succeeded; anything else is the agent's
 * exit code (or 128 + signal, or 124 for a timeout) and error_detail says why.
 *
 * The child gets database credentials from JF_AI_DB_* in the daemon's
 * environment, never the daemon's own, and is started with posix_spawn so no
 * daemon thread state is copied into it.
 */
class AgentJob : public Job
{
public:
    AgentJob(int interval, const char *name, const char *script_path, int agent_index,
             int timeout_seconds = 120);
    ~AgentJob() override = default;

    void execute() override;
    void onTick() override;

    /// Kills a run still in progress; used at shutdown so no agent is orphaned.
    void terminate();

    int getAgentIndex() const { return agent_index_; }

private:
    void record(int status_code, const std::string &detail);

    std::string agent_name_;
    std::string script_path_;
    int agent_index_;
    int timeout_seconds_;
    pid_t pid_{-1};
    time_t started_at_{0};
};
