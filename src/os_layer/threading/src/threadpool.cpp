#include "../include/thread_pool.h"
#include <iostream>

void* ThreadPool::worker_routine(void* arg) {
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    
    while (true) {
        WorkerTask task;
        
        {
            // Acquire lock to check the queue
            MutexGuard lock(pool->queue_mutex);
            
            // Sleep if there are no tasks and we are not shutting down
            while (pool->task_count == 0 && !pool->shutdown_flag) {
                pool->task_available_cond.wait(pool->queue_mutex);
            }
            
            // If woken up to shut down and queue is empty, break out of loop
            if (pool->shutdown_flag && pool->task_count == 0) {
                break; 
            }
            
            // Dequeue the task
            task = pool->task_queue[pool->queue_head];
            pool->queue_head = (pool->queue_head + 1) % QUEUE_CAPACITY;
            pool->task_count--;
            
        } // RAII Magic: queue_mutex is automatically released here
        
        // Execute the task using the Worker logic we built in Phase 3
        Worker::process_task(task);
    }
    
    return nullptr;
}

void ThreadPool::init(int num_workers) {
    if (num_workers > MAX_WORKERS) num_workers = MAX_WORKERS;
    active_worker_count = num_workers;
    
    for (int i = 0; i < active_worker_count; i++) {
        if (pthread_create(&workers[i], nullptr, worker_routine, this) != 0) {
            std::cerr << "CRITICAL: Failed to spawn thread " << i << "\n";
        }
    }
    std::cout << "[ThreadPool] Initialized with " << active_worker_count << " workers.\n";
}

bool ThreadPool::submit(WorkerTask task) {
    MutexGuard lock(queue_mutex);
    
    if (task_count == QUEUE_CAPACITY) {
        std::cerr << "[ThreadPool] Queue full. Dropping task.\n";
        return false;
    }
    
    // Enqueue the new task
    task_queue[queue_tail] = task;
    queue_tail = (queue_tail + 1) % QUEUE_CAPACITY;
    task_count++;
    
    // Signal ONE sleeping worker to wake up and take the task
    task_available_cond.signal();
    
    return true;
}

void ThreadPool::shutdown() {
    {
        MutexGuard lock(queue_mutex);
        shutdown_flag = true;
    }
    
    // Wake up ALL sleeping workers so they see the shutdown flag
    task_available_cond.broadcast();
    
    for (int i = 0; i < active_worker_count; i++) {
        pthread_join(workers[i], nullptr);
    }
    
    std::cout << "[ThreadPool] Shutdown complete. All threads joined.\n";
}
