#include "../include/session_manager.h"
#include "common/logger.h"

SessionManager::SessionManager() {}

SessionManager &SessionManager::getInstance()
{
    static SessionManager instance;
    return instance;
}

void SessionManager::register_session(int thread_id, SessionContext context)
{
    MutexGuard lock(registry_mutex);
    
    // CRITICAL FIX #4.1: Check for duplicate thread_id before overwriting
    if (registry.find(thread_id) != registry.end())
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[SessionManager] WARNING: Overwriting existing session for thread_id %d", thread_id);
        Logger::error(buf);
    }
    
    registry[thread_id] = context;
}

void SessionManager::unregister_session(int thread_id)
{
    MutexGuard lock(registry_mutex);
    registry.erase(thread_id);
}

bool SessionManager::getSession(int thread_id, SessionContext &out_context)
{
    MutexGuard lock(registry_mutex);
    auto it = registry.find(thread_id);
    if (it != registry.end())
    {
        out_context = it->second;
        return true;
    }
    return false;
}

int SessionManager::getActiveCount()
{
    MutexGuard lock(registry_mutex);
    
    // CRITICAL FIX #4.2: Cast size_t to int safely
    // For small connection counts (50), overflow is not a practical concern
    // but it's better practice to be explicit
    return static_cast<int>(registry.size());
}