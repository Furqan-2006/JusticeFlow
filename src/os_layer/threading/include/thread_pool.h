#pragma once

#include <pthread.h>
#include <string>
#include "sync.h"
#include "worker.h"

#define MAX_WORKERS 50
#define QUEUE_CAPACITY 100

class ThreadPool
{
private:
    pthread_t workers[MAX_WORKERS];
    int active_worker_count;
    bool initialized; // CRITICAL FIX #2.2: Prevent multiple init() calls

    // Circular Buffer (Ring Buffer) for the work queue
    WorkerTask task_queue[QUEUE_CAPACITY];
    int queue_head;
    int queue_tail;
    int task_count;

    bool shutdown_flag;

    Mutex queue_mutex;
    CondVar task_available_cond;

    // Architecture mandate: CondVar for AI result notification owned here
    CondVar ai_result_cond;

    static void *worker_routine(void *arg);

    // Singleton instance
    ThreadPool() : active_worker_count(0), initialized(false), queue_head(0), queue_tail(0),
                   task_count(0), shutdown_flag(false) {}
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

public:
    static ThreadPool &getInstance()
    {
        static ThreadPool instance;
        return instance;
    }

    void init(int num_workers);
    bool submit(WorkerTask task);
    void shutdown();

    // CRITICAL FIX #2.3: Now properly declared and implemented
    void broadcastAiUpdate();

    // Exposed for the StatusTableUpdater from the scheduler side
    CondVar &get_ai_result_cond() { return ai_result_cond; }
};