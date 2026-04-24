/**
 * @file ipc_module.h
 * @brief Umbrella header for the IPC (Inter-Process Communication) subsystem.
 * 
 * This header provides a single include point for all IPC functionality.
 * External modules should include this file instead of individual IPC headers.
 * 
 * Architecture (DESIGN.md §6):
 * - UnixSocket: PostgreSQL database communication via Unix domain sockets
 * - Fifo: Agent status updates via named pipes (FIFO)
 * - SharedMemory: Live status table shared across all processes
 * - IpcManager: High-level facade coordinating all IPC operations
 * 
 * Thread Safety:
 * - Database operations (UnixSocket) are internally serialized by mutex
 * - Shared memory operations use robust, process-shared pthread_mutex_t
 * - FIFO reads are non-blocking and atomic for message-sized reads
 * 
 * CRITICAL: Do NOT call any IPC functions from signal handlers.
 * All IPC operations (PQexec, pthread_mutex_lock, etc.) are NOT async-signal-safe.
 */

#pragma once

#include "ipc_manager.h"
#include "unix_socket.h"
#include "fifo.h"
#include "shared_memory.h"
#include "shm_layout.h"
