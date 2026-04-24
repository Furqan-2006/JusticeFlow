#pragma once

#include <sys/types.h>
#include "common/constants.h"
#include "process_registry.h"

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
    static constexpr int SHUTDOWN_TIMEOUT_SEC = 10; // Grace period before SIGKILL
    static constexpr int REAP_TIMEOUT_MS = 100;     // Per-child wait timeout

    ProcessManager() = default;
    ~ProcessManager() = default;

public:
    static ProcessManager &getInstance();

    /**
     * Reap a single child process and determine action.
     * Non-blocking operation with WNOHANG.
     *
     * @param pid The process ID to reap
     * @param out_action Set to RESTART/ESCALATE_ERROR if process died abnormally
     * @return OK if child was reaped/already dead, NOT_FOUND if still running, DB_ERROR on system error
     */
    JusticeFlow::ResultCode reapOne(pid_t pid, ProcessAction &out_action);

    /**
     * Reap all registered child processes during shutdown.
     * Non-blocking with per-child timeout.
     * Sends SIGTERM → waits → SIGKILL if needed.
     *
     * @return OK on successful cleanup
     * @note: May not block indefinitely even if children hang
     */
    JusticeFlow::ResultCode reapAll();

    /**
     * Send graceful shutdown signal to a process.
     *
     * @param pid The process ID to signal
     * @return OK if signal sent, NOT_FOUND if process gone, DB_ERROR on error
     */
    JusticeFlow::ResultCode sendSignal(pid_t pid, int signal);
};
