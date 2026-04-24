#pragma once

#include <sys/types.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "common/constants.h"
#include "../../threading/include/sync.h"

enum class ProcessState
{
    RUNNING,
    COMPLETED,
    FAILED,
    REAPED
};

struct ProcessRecord
{
    pid_t pid;
    char agent_name[32];
    char fifo_path[64];
    char shm_name[32];
    ProcessState state;
    int restart_count;

    ProcessRecord() : pid(-1), state(ProcessState::FAILED), restart_count(0)
    {
        std::memset(agent_name, 0, sizeof(agent_name));
        std::memset(fifo_path, 0, sizeof(fifo_path));
        std::memset(shm_name, 0, sizeof(shm_name));
    }
};

class ProcessRegistry
{
private:
    std::unordered_map<pid_t, ProcessRecord> registry_;
    Mutex mutex_; // Abu Bakar's Mutex wrapper

    ProcessRegistry() = default;
    ~ProcessRegistry() = default;

    ProcessRegistry(const ProcessRegistry &) = delete;
    ProcessRegistry &operator=(const ProcessRegistry &) = delete;

public:
    static ProcessRegistry &getInstance();

    /**
     * Register a new process in the registry.
     * Returns ALREADY_EXISTS if PID already registered.
     * 
     * @param pid Process ID
     * @param record The process record to store
     * @return OK if registered, ALREADY_EXISTS if PID in use
     */
    JusticeFlow::ResultCode registerProcess(pid_t pid, const ProcessRecord &record);

    /**
     * Update only the state field of an existing record.
     * Atomic operation under lock.
     * 
     * @param pid Process ID
     * @param new_state New ProcessState value
     * @return OK if updated, NOT_FOUND if PID not in registry
     */
    JusticeFlow::ResultCode updateState(pid_t pid, ProcessState new_state);

    /**
     * CRITICAL FIX: Atomically increment restart_count in registry.
     * This ensures the count is immediately persisted and visible to all threads.
     * Caller must check return value to determine if restart is allowed.
     * 
     * @param pid Process ID
     * @param out_new_count Set to the incremented restart_count value
     * @return OK if incremented, NOT_FOUND if PID not in registry
     * @note: Lock is held for entire operation - no TOCTOU window
     */
    JusticeFlow::ResultCode incrementRestartCount(pid_t pid, int& out_new_count);

    /**
     * Retrieve a copy of a process record.
     * Safe to read outside the lock (this is a copy).
     * 
     * @param pid Process ID
     * @param out_record Populated with a copy of the record
     * @return OK if found, NOT_FOUND if PID not in registry
     */
    JusticeFlow::ResultCode getRecord(pid_t pid, ProcessRecord &out_record);

    /**
     * Remove a process record from the registry (cleanup).
     * Called after process is reaped and no longer needed.
     * 
     * @param pid Process ID
     * @return OK if removed, NOT_FOUND if PID not in registry
     */
    JusticeFlow::ResultCode removeRecord(pid_t pid);

    /**
     * Get all PIDs currently in the registry.
     * Returns a snapshot - safe for iteration outside lock.
     * 
     * @param out_pids Vector filled with all registered PIDs
     * @return OK always (empty vector if registry empty)
     */
    JusticeFlow::ResultCode getAllPids(std::vector<pid_t> &out_pids);
};
