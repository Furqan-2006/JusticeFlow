#pragma once

#include <sys/types.h>
#include "common/constants.h"
#include "../include/process_manager.h"

enum class ProcessAction {
    NONE,           
    RESTART,        
    ESCALATE_ERROR  
};

class ProcessManager {
private:
    static constexpr int MAX_RESTARTS = 3;

    ProcessManager() = default;
    ~ProcessManager() = default;

public:
    static ProcessManager& getInstance();

    JusticeFlow::ResultCode reapOne(pid_t pid, ProcessAction& out_action);
    JusticeFlow::ResultCode reapAll();
};