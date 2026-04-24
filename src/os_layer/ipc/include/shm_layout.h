#pragma once

#include <pthread.h>
#include <time.h>

namespace ipc {

// Contract Page 13: Live Agent Status Table
struct AgentStatus {
    char agent_name[32];
    time_t last_run_at;
    time_t next_run_at;
    int predictions_generated;
    double model_accuracy;
    bool is_running;
    int last_error_code;
};

/**
 * Full shared memory segment layout.
 * 
 * CRITICAL: This struct is placed in shared memory (see SharedMemory class).
 * All synchronization primitives (mutex, cond_var) MUST be initialized
 * with PTHREAD_PROCESS_SHARED attributes by the creator process.
 * 
 * The creator process (typically the main daemon) calls:
 *   - pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED)
 *   - pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST)
 *   - pthread_mutex_init(&table->mutex, &attr)
 *   - pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED)
 *   - pthread_cond_init(&table->cond_var, &cond_attr)
 * 
 * All other processes call:
 *   - pthread_mutex_lock(&table->mutex) (will succeed on shared memory)
 *   - pthread_cond_wait(&table->cond_var, &table->mutex)
 */
struct SharedStatusTable {
    // Robust mutex: survives if a process holding the lock crashes.
    // PROCESS_SHARED: can be used by multiple processes (all accessing shared memory).
    pthread_mutex_t mutex;

    // Condition variable: notifies waiting threads when AI agents update status.
    // MUST be initialized by the creator process before any waiter uses it.
    pthread_cond_t cond_var;

    // Number of active client sessions.
    int active_sessions;

    // Status of each AI agent: [0]=hotspot [1]=priority [2]=workload
    AgentStatus agents[3];
};

} // namespace ipc
