#include "../include/connection_gate.h"

ConnectionGate::ConnectionGate() : session_semaphore(DEFAULT_MAX_CONNECTIONS) {}

ConnectionGate& ConnectionGate::getInstance() {
    static ConnectionGate instance;
    return instance;
}

Semaphore& ConnectionGate::getSemaphore() {
    return session_semaphore;
}