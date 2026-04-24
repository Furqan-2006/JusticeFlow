#pragma once

#include "sync.h"

#define DEFAULT_MAX_CONNECTIONS 50

class ConnectionGate {
private:
    Semaphore session_semaphore;

    ConnectionGate();
    ConnectionGate(const ConnectionGate&) = delete;
    ConnectionGate& operator=(const ConnectionGate&) = delete;

public:
    static ConnectionGate& getInstance();

    Semaphore& getSemaphore();
};