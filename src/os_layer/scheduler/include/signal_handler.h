#pragma once

#include <signal.h>

class SignalHandler
{
private:
    typedef void (*SignalCallback)(int);

    static SignalCallback dispatch_table[NSIG];
    static void (*config_reload_cb)();

    // CRITICAL FIX #9.1, #9.3: These handlers now ONLY set flags (async-signal-safe)
    static void handle_sigalrm(int sig);
    static void handle_sigchld(int sig);
    static void handle_sigterm_int(int sig);
    static void handle_sighup(int sig);

    static void master_handler(int sig);

public:
    SignalHandler() = delete;

    static void init();
    static void setConfigReloadCallback(void (*cb)());
};