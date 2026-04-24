#include "../include/signal_handler.h"
#include "../include/scheduler.h"
#include "os_layer/process/include/process_manager.h"

#include <sys/wait.h>
#include <errno.h>
#include <cstddef>

SignalHandler::SignalCallback SignalHandler::dispatch_table[NSIG] = {nullptr};
void (*SignalHandler::config_reload_cb)() = nullptr;

// CRITICAL FIX #9.1, #9.3: Flags for main loop to poll
static volatile sig_atomic_t sig_alarm_pending = 0;
static volatile sig_atomic_t sig_child_pending = 0;

void SignalHandler::init()
{
    dispatch_table[SIGALRM] = handle_sigalrm;
    dispatch_table[SIGCHLD] = handle_sigchld;
    dispatch_table[SIGTERM] = handle_sigterm_int;
    dispatch_table[SIGINT] = handle_sigterm_int;
    dispatch_table[SIGHUP] = handle_sighup;

    struct sigaction sa;
    sa.sa_handler = master_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    struct sigaction sa_chld = sa;
    sa_chld.sa_flags |= SA_NOCLDSTOP;

    sigaction(SIGALRM, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGCHLD, &sa_chld, nullptr);
}

void SignalHandler::setConfigReloadCallback(void (*cb)())
{
    config_reload_cb = cb;
}

void SignalHandler::master_handler(int sig)
{
    if (sig >= 0 && sig < NSIG && dispatch_table[sig] != nullptr)
    {
        dispatch_table[sig](sig);
    }
}

// CRITICAL FIX #9.1: handle_sigalrm now ONLY sets a flag (async-signal-safe)
// Main loop polls this flag and calls Scheduler::tick()
void SignalHandler::handle_sigalrm(int sig)
{
    int saved_errno = errno;
    sig_alarm_pending = 1;  // Only atomic flag write - async-signal-safe
    errno = saved_errno;
}

// CRITICAL FIX #9.3: handle_sigchld now does NOT acquire mutexes
// Only sets a flag; ProcessManager::reapOne is called from main loop
void SignalHandler::handle_sigchld(int sig)
{
    int saved_errno = errno;
    sig_child_pending = 1;  // Only atomic flag write - async-signal-safe
    errno = saved_errno;
}

// CRITICAL FIX #9.2: handle_sigterm_int only requests drain (async-signal-safe)
// Actual shutdown operations happen in Scheduler::handleDrainRequest() from main loop
void SignalHandler::handle_sigterm_int(int sig)
{
    int saved_errno = errno;
    Scheduler::getInstance().requestDrain();  // Only writes to atomic bool - safe
    errno = saved_errno;
}

void SignalHandler::handle_sighup(int sig)
{
    int saved_errno = errno;
    if (config_reload_cb != nullptr)
    {
        config_reload_cb();
    }
    errno = saved_errno;
}

// NEW: Helper function for main loop to process pending signals
// This should be called from the main event loop (NOT from signal handler)
void SignalHandler::processPendingSignals()
{
    // Process pending SIGALRM
    if (sig_alarm_pending)
    {
        sig_alarm_pending = 0;
        Scheduler::getInstance().tick();  // Now safe - main loop context
    }

    // Process pending SIGCHLD
    if (sig_child_pending)
    {
        sig_child_pending = 0;
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            ProcessAction action;
            ProcessManager::getInstance().reapOne(pid, action);
        }
    }

    // Process pending drain request
    if (Scheduler::getInstance().shouldDrain())
    {
        Scheduler::getInstance().handleDrainRequest();
    }
}