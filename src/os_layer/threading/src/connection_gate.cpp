#include "../include/connection_gate.h"
#include "common/logger.h"

ConnectionGate::ConnectionGate() : session_semaphore(DEFAULT_MAX_CONNECTIONS) 
{
    char log_buf[128];
    std::snprintf(log_buf, sizeof(log_buf), 
                 "[ConnectionGate] Initialized with max connections: %d", DEFAULT_MAX_CONNECTIONS);
    Logger::info(log_buf);
}

ConnectionGate &ConnectionGate::getInstance()
{
    static ConnectionGate instance;
    return instance;
}

Semaphore &ConnectionGate::getSemaphore()
{
    return session_semaphore;
}