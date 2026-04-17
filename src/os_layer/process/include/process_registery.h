#pragma once

#include <sys/types.h>
#include <pthread.h>
#include <unordered_map>
#include <vector>
#include <cstring>
#include "../../../common/constants.h"

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
    pthread_mutex_t mutex_;

    ProcessRegistry();
    ~ProcessRegistry();

    ProcessRegistry(const ProcessRegistry &) = delete;
    ProcessRegistry &operator=(const ProcessRegistry &) = delete;
    ProcessRegistry(ProcessRegistry &&) = delete;
    ProcessRegistry &operator=(ProcessRegistry &&) = delete;

public:
    static ProcessRegistry &getProcessRegistry();

    ResultCode registerProcess(pid_t pid, const ProcessRecord &record);
    ResultCode updateState(pid_t pid, ProcessState new_state);
    ResultCode getRecord(pid_t pid, ProcessRecord &out_record);
    ResultCode removeRecord(pid_t pid);
    ResultCode getAllPids(std::vector<pid_t> &out_pids);
};