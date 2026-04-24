#pragma once

#include <unordered_map>
#include <ctime>
#include "sync.h"

struct SessionContext
{
    int officer_id;
    int socket_fd;
    long login_timestamp; // CRITICAL FIX #3.1: Now properly initialized
};

class SessionManager
{
private:
    std::unordered_map<int, SessionContext> registry;
    Mutex registry_mutex;

    SessionManager();
    SessionManager(const SessionManager &) = delete;
    SessionManager &operator=(const SessionManager &) = delete;

public:
    static SessionManager &getInstance();

    void register_session(int thread_id, SessionContext context);
    void unregister_session(int thread_id);
    bool getSession(int thread_id, SessionContext &out_context);
    int getActiveCount();
};