/**
 * @file os_layer.h
 * @brief Single entry point for all OS layer functionality
 *
 * # Architecture
 *
 * The OS layer provides complete operating system abstraction:
 * - Process management (spawning, monitoring, reaping)
 * - Memory management (safe MMAP, credential pinning)
 * - IPC (PostgreSQL, FIFO, shared memory)
 * - Threading (worker pool, session tracking, synchronization)
 * - Scheduling (event loop, signal handling, timers)
 *
 * # Single Import Rule
 *
 * External code MUST import ONLY this header:
 *
 *   #include "os_layer/os_layer.h"
 *
 * All other headers in os_layer/ are internal and should not be
 * included directly. This ensures:
 * - Decoupled architecture
 * - Easier refactoring
 * - Clear API boundaries
 *
 * # Subsystems
 *
 * ## 1. Threading (os_layer/threading/)
 * Worker thread pool with session management and DB throttling.
 *
 *   ThreadPool::getInstance().init(num_workers);
 *   ThreadPool::getInstance().submit(task);
 *   ThreadPool::getInstance().shutdown();
 *   int active_sessions = SessionManager::getInstance().getActiveCount();
 *
 * ## 2. IPC (os_layer/ipc/)
 * Database queries, agent status updates, shared memory synchronization.
 *
 *   ipc::IpcManager::getInstance().connectDatabase();
 *   ipc::IpcManager::getInstance().executeQuery(query, results);
 *   ipc::IpcManager::getInstance().readAgentStatus(idx, msg);
 *   ipc::IpcManager::getInstance().updateAgentStatus(idx, status);
 *
 * ## 3. Process (os_layer/process/)
 * Child process spawning, reaping, restart management.
 *
 *   ProcessManager::getInstance().reapOne(pid, action);
 *   ProcessManager::getInstance().reapAll();
 *
 * ## 4. Scheduler (os_layer/scheduler/)
 * Event loop with job scheduling and signal handling.
 *
 *   Scheduler::getInstance().registerJob(job);
 *   Scheduler::getInstance().tick();
 *   Scheduler::getInstance().setState(SchedulerState::RUNNING);
 *   SignalHandler::init();
 *   Timer::arm(interval_seconds);
 *
 * ## 5. Memory (os_layer/memory/)
 * Safe MMAP operations and credential pinning.
 *
 *   memory::MmapHandler handler;
 *   handler.map(fd, size, is_shared);
 *   handler.getPointer();
 *   mlock_guard guard(addr, size);
 *
 * # Synchronization Primitives (os_layer/threading/sync.h)
 *
 * Core RAII synchronization for intra-process coordination:
 *
 *   Mutex mutex;
 *   {
 *       MutexGuard lock(mutex);
 *       // Protected section
 *   } // Lock released automatically
 *
 *   CondVar cond;
 *   cond.wait(mutex);
 *   cond.signal();
 *   cond.broadcast();
 *
 *   Semaphore sem(initial_value);
 *   {
 *       SemGuard gate(sem);
 *       // Resource acquired
 *   } // Released automatically
 *
 * # Authentication & Sessions (shr_infra/auth/)
 *
 * IMPORTANT: Auth is in shared infrastructure, NOT os_layer!
 * Import separately:
 *
 *   #include "shr_infra/auth/include/auth_module.h"
 *
 * Usage:
 *
 *   auth::AuthManager::getInstance().login(cnic, password, session);
 *   auth::AuthManager::getInstance().validateToken(token, session);
 *   auth::AuthManager::getInstance().isDutyActive(officer_id, is_active);
 *
 * # Error Codes
 *
 * All public APIs return JusticeFlow::ResultCode:
 * - OK                      — Success
 * - NOT_FOUND              — Resource not found
 * - INVALID_INPUT          — Bad parameter
 * - INVALID_STATE          — Operation invalid in current state
 * - DB_ERROR               — Database query failed
 * - FILE_SYSTEM_ERROR      — FS operation failed
 * - SESSION_EXPIRED        — Session past expiry deadline
 * - AUTHENTICATION_FAILED  — Auth credentials invalid
 * - PERMISSION_DENIED      — Insufficient privileges
 *
 * # Thread Safety
 *
 * - ThreadPool: Thread-safe (concurrent submit)
 * - SessionManager: Thread-safe (RWLock protected)
 * - IpcManager: Database connection serialized internally
 * - Scheduler: Single-threaded event loop (signal-safe)
 * - ProcessManager: Signal-safe (called from SIGCHLD handler)
 *
 * # Signal Safety
 *
 * Signal handlers (SIGALRM, SIGCHLD, SIGTERM) only set atomic flags.
 * Main event loop polls flags and performs work from safe context.
 * This prevents deadlocks and undefined behavior.
 *
 * See scheduler/signal_handler.h for implementation details.
 *
 * # Initialization Order
 *
 * Recommended startup sequence:
 *
 *   1. Daemon::init(pid_file)           ← Daemonize process
 *   2. SignalHandler::init()            ← Setup signal handlers
 *   3. IpcManager::connectDatabase()    ← Establish DB connection
 *   4. ThreadPool::init(num_workers)    ← Start worker threads
 *   5. Scheduler::registerJob(job)      ← Register scheduled jobs
 *   6. Timer::arm(interval_seconds)     ← Start event loop timer
 *   7. Scheduler::setState(RUNNING)     ← Begin processing
 *
 * # Shutdown Sequence
 *
 * Initiated by SIGTERM or SIGINT:
 *
 *   1. Signal handler sets drain flag
 *   2. Main loop observes flag
 *   3. Timer::disarm()
 *   4. ProcessManager::reapAll()        ← Sends SIGTERM → wait → SIGKILL
 *   5. ThreadPool::shutdown()           ← Wait for worker threads
 *   6. IpcManager::disconnectDatabase()
 *   7. Scheduler::setState(STOPPED)
 *
 * # Example Usage
 *
 *   #include "os_layer/os_layer.h"
 *   #include "shr_infra/auth/include/auth_module.h"
 *
 *   int main() {
 *       // Initialize
 *       Daemon::init("/var/run/justiceflow.pid");
 *       SignalHandler::init();
 *       ipc::IpcManager::getInstance().connectDatabase();
 *       ThreadPool::getInstance().init(50);
 *
 *       // Register jobs
 *       auto warrants_job = new ExpireWarrantsJob(3600, nullptr);
 *       Scheduler::getInstance().registerJob(warrants_job);
 *       Scheduler::getInstance().registerObserver(new StatusTableUpdater());
 *
 *       // Start event loop
 *       Timer::arm(1);
 *       Scheduler::getInstance().setState(SchedulerState::RUNNING);
 *
 *       // Process requests (main loop calls SignalHandler::processPendingSignals())
 *       while (Scheduler::getInstance().getState() != SchedulerState::STOPPED) {
 *           SignalHandler::processPendingSignals();
 *           usleep(100000);
 *       }
 *
 *       return 0;
 *   }
 *
 * @author Furqan, Abu Bakar, Abdullah, Others
 * @version 1.0
 * @date 2026-04
 */

#pragma once

// ============================================================================
// Threading Subsystem
// ============================================================================
#include "threading/include/threading_module.h"

// ============================================================================
// IPC Subsystem
// ============================================================================
#include "ipc/include/ipc_module.h"

// ============================================================================
// Process Subsystem
// ============================================================================
#include "process/include/process_manager.h"
#include "process/include/process_registry.h"

// ============================================================================
// Scheduler Subsystem
// ============================================================================
#include "scheduler/include/scheduler_module.h"

// ============================================================================
// Memory Subsystem
// ============================================================================
#include "memory/include/mlock_guard.h"
#include "memory/include/mmap_handler.h"

// ============================================================================
// Constants & Utilities
// ============================================================================
#include "common/constants.h"
#include "common/logger.h"

// ============================================================================
// Public API Summary
// ============================================================================

/**
 * @namespace JusticeFlow
 * @brief Main framework namespace
 */

/**
 * OS Layer subsystems (all via os_layer.h):
 *
 * Threading:
 *   - ThreadPool::getInstance()
 *   - SessionManager::getInstance()
 *   - Mutex, CondVar, RWLock, Semaphore primitives
 *
 * IPC:
 *   - ipc::IpcManager::getInstance()
 *   - ipc::UnixSocket (PostgreSQL interface)
 *   - ipc::Fifo (Agent status updates)
 *   - ipc::SharedMemory (Shared status table)
 *
 * Process:
 *   - ProcessManager::getInstance()
 *   - ProcessRegistry::getInstance()
 *
 * Scheduler:
 *   - Scheduler::getInstance()
 *   - Timer namespace functions
 *   - SignalHandler namespace functions
 *   - Job, JobObserver, AgentJob, StatusTableUpdater classes
 *
 * Memory:
 *   - memory::MmapHandler
 *   - mlock_guard
 *
 * Shared Infrastructure (separate import from shr_infra/):
 *
 * Auth:
 *   - auth::AuthManager::getInstance()
 *   - auth::SessionContext
 */