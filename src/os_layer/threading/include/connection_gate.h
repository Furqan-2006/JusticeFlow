#ifndef CONNECTION_GATE_H
#define CONNECTION_GATE_H

#include "sync.h"

#define DEFAULT_MAX_CONNECTIONS 50

class ConnectionGate {
private:
    Semaphore session_semaphore;

    ConnectionGate() : session_semaphore(DEFAULT_MAX_CONNECTIONS) {}
    ConnectionGate(const ConnectionGate&) = delete;
    ConnectionGate& operator=(const ConnectionGate&) = delete;

public:
    static ConnectionGate& getInstance() {
        static ConnectionGate instance;
        return instance;
    }

    // Instead of raw acquire/release, we hand the worker the Semaphore.
    // The worker does: SemGuard lock(ConnectionGate::getInstance().getSemaphore());
    Semaphore& getSemaphore() {
        return session_semaphore;
    }
};

#endif // CONNECTION_GATE_H
