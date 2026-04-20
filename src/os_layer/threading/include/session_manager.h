#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <unordered_map>
#include "sync.h"

// Define a basic SessionContext struct 
struct SessionContext {
    int officer_id;
    int socket_fd;
    long login_timestamp;
};

class SessionManager {
private:
    std::unordered_map<int, SessionContext> registry;
    Mutex registry_mutex;

    // Singleton setup
    SessionManager() {}
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

public:
    static SessionManager& getInstance() {
        static SessionManager instance;
        return instance;
    }

    void register_session(int thread_id, SessionContext context) {
        MutexGuard lock(registry_mutex); // Locks the map safely!
        registry[thread_id] = context;
    } // lock goes out of scope here and automatically unlocks!

    void unregister_session(int thread_id) {
        MutexGuard lock(registry_mutex);
        registry.erase(thread_id);
    }

    bool getSession(int thread_id, SessionContext& out_context) {
        MutexGuard lock(registry_mutex);
        auto it = registry.find(thread_id);
        if (it != registry.end()) {
            out_context = it->second;
            return true;
        }
        return false;
    }

    int getActiveCount() {
        MutexGuard lock(registry_mutex);
        return registry.size();
    }
};

#endif // SESSION_MANAGER_H
