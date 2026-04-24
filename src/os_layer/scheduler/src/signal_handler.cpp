#include "../include/signal_handler.h"
#include "../include/scheduler.h"
#include "../../process/include/process_manager.h"

#include <sys/wait.h>
#include <errno.h>
#include <cstddef>

// Dispatch + callbacks
SignalHandler::SignalCallback SignalHandler::dispatch_table[NSIG] = {nullptr};
void (*SignalHandler::config_reload_cb)() = nullptr;

// ✅ Signal-safe flags
static volatile sig_atomic_t alarm_pending = 0;
static volatile sig_atomic_t chld_pending = 0;

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

// ✅ Async-signal-safe: only sets a flag
void SignalHandler::handle_sigalrm(int /*sig*/)
{
    alarm_pending = 1;
}

// ✅ Async-signal-safe: only sets a flag
void SignalHandler::handle_sigchld(int /*sig*/)
{
    chld_pending = 1;
}

// ✅ Async-signal-safe: request drain via atomic write only
void SignalHandler::handle_sigterm_int(int /*sig*/)
{
    Scheduler::getInstance().requestDrain();
}

void SignalHandler::handle_sighup(int /*sig*/)
{
    // NOTE: calling arbitrary callbacks from signal handler is NOT async-signal-safe.
    // If you want strict safety here too, convert SIGHUP into a flag like others.
    if (config_reload_cb != nullptr)
    {
        config_reload_cb();
    }
}

void SignalHandler::processPendingSignals()
{
    // Drain request first (so shutdown doesn't keep doing work)
    Scheduler::getInstance().handleDrainRequest();

    if (alarm_pending)
    {
        alarm_pending = 0;
        Scheduler::getInstance().tick();
    }

    if (chld_pending)
    {
        chld_pending = 0;

        pid_t pid;
        int status;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            ProcessAction action;
            ProcessManager::getInstance().reapOne(pid, action);
            // Optional: act on action (restart/escalate) from supervisor loop
        }
    }
}