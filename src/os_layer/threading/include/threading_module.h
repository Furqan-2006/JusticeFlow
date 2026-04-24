/**
 * @file threading_module.h
 * @brief Umbrella header for the Threading subsystem
 *
 * Exports all public threading interfaces to os_layer.h
 *
 * # Components
 * - Sync: Synchronization primitives (Mutex, CondVar, Semaphore, RWLock)
 * - ThreadPool: Worker thread pool with task queue and shutdown coordination
 * - SessionManager: Per-thread session registry with login timestamps
 * - ConnectionGate: Semaphore-based throttling of database connections
 * - Worker: Task execution logic for handling officer requests
 *
 * # Architecture Notes
 * - All sync primitives use PTHREAD_PROCESS_PRIVATE (single-process)
 * - Only raw pthread_mutex_t in shared memory uses PROCESS_SHARED
 * - MAX_WORKERS (50) == DEFAULT_MAX_CONNECTIONS (50) - should be adjusted for throttling
 * - ThreadPool::init() is idempotent (safe to call multiple times)
 *
 * # Usage in os_layer.h
 * #include "threading/include/threading_module.h"
 *
 * Then call:
 * - ThreadPool::getInstance().init(num_workers)
 * - ThreadPool::getInstance().submit(task)
 * - SessionManager::getInstance().register_session(thread_id, context)
 */

#pragma once

#include "sync.h"
#include "connection_gate.h"
#include "session_manager.h"
#include "worker.h"
#include "thread_pool.h"