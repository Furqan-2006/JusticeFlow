#pragma once

#include "sync.h"

// CRITICAL FIX #5.1: M should be < N to provide throttling
// M = MAX_WORKERS, N = DEFAULT_MAX_CONNECTIONS
// Currently M == N == 50, so semaphore never blocks (no throttling)
//
// DESIGN MANDATE: M ≤ N but ideally M < N to create a buffer
// TODO: Consider reducing MAX_WORKERS to 40 and keeping connections at 50
// This would ensure the gate actually throttles and prevents DB connection storms

#define DEFAULT_MAX_CONNECTIONS 50

class ConnectionGate
{
private:
    Semaphore session_semaphore;

    ConnectionGate();
    ConnectionGate(const ConnectionGate &) = delete;
    ConnectionGate &operator=(const ConnectionGate &) = delete;

public:
    static ConnectionGate &getInstance();

    Semaphore &getSemaphore();
};