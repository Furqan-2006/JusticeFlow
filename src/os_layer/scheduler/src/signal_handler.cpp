#include "../include/signal_handler.h"
#include "../../process/include/process_manager.h"
#include "../../scheduler/include/scheduler.h"

#include <sys/wait.h>
#include <errno.h>
#include <cstddef>

SignalHandler::SignalCallback SignalHandler::dispatch_table[NSIG] = {nullptr};
void (*SignalHandler::config_reload_cb)() = nullptr;

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

void SignalHandler::handle_sigalrm(int sig)
{
    int saved_errno = errno;

    Scheduler::getInstance().tick();

    errno = saved_errno;
}

void SignalHandler::handle_sigchld(int sig)
{
    int saved_errno = errno;

    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        ProcessAction action;
        ProcessManager::getInstance().reapOne(pid, action);
    }

    errno = saved_errno;
}

void SignalHandler::handle_sigterm_int(int sig)
{
    int saved_errno = errno;

    Scheduler::getInstance().getState(SchedulerState::DRAINING);

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