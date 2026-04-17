#pragma once

#include <sys/types.h>
#include "common/constants.h"
#include "../include/process_registery.h"

enum class ProcessAction
{
    NONE,
    RESTART,
    ESCALATE_ERROR
};

class ProcessManager
{
private:
    static constexpr int MAX_RESTARTS = 3;

    ProcessManager() = default;
    ~ProcessManager() = default;

    ProcessManager(const ProcessManager &) = delete;
    ProcessManager &operator=(const ProcessManager &) = delete;

public:
    static ProcessManager &getInstance();

    ResultCode reapOne(pid_t pid, ProcessAction &out_action);
    ResultCode reapAll();
};