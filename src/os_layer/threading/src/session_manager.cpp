#include "../include/session_manager.h"

SessionManager::SessionManager() {}

SessionManager& SessionManager::getInstance() {
    static SessionManager instance;
    return instance;
}

void SessionManager::register_session(int thread_id, SessionContext context) {
    MutexGuard lock(registry_mutex);
    registry[thread_id] = context;
}

void SessionManager::unregister_session(int thread_id) {
    MutexGuard lock(registry_mutex);
    registry.erase(thread_id);
}

bool SessionManager::getSession(int thread_id, SessionContext& out_context) {
    MutexGuard lock(registry_mutex);
    auto it = registry.find(thread_id);
    if (it != registry.end()) {
        out_context = it->second;
        return true;
    }
    return false;
}

int SessionManager::getActiveCount() {
    MutexGuard lock(registry_mutex);
    return registry.size();
}