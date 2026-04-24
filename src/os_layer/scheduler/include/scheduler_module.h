/**
 * @file scheduler_module.h
 * @brief Umbrella header for the Scheduler subsystem
 *
 * Exports all public interfaces of the scheduler to os_layer.h
 *
 * # Components
 * - Scheduler: Main event loop scheduler with job registry and observers
 * - Timer: POSIX interval timer management (SIGALRM)
 * - SignalHandler: POSIX signal handling with async-signal-safe guarantees
 * - AgentJob: Job type for spawning and monitoring AI agent processes
 * - StatusTableUpdater: Observer that updates shared memory on job completion
 *
 * # Signal Safety Architecture
 * All signal handlers (SIGALRM, SIGCHLD, SIGTERM) only set atomic flags.
 * The main event loop polls these flags and performs actual work from safe context.
 * This prevents undefined behavior and deadlocks from async-signal-unsafe calls.
 *
 * # Usage in os_layer.h
 * #include "scheduler/include/scheduler_module.h"
 *
 * Then call:
 * - Scheduler::getInstance() to access the global scheduler
 * - SignalHandler::init() during startup
 * - SignalHandler::processPendingSignals() in main event loop
 */

#pragma once

#include "scheduler.h"
#include "timer.h"
#include "signal_handler.h"
#include "process_spawner.h"
#include "job_executor.h"
#include "daemon.h"