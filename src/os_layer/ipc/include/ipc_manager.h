#pragma once

#include "unix_socket.h"
#include "fifo.h"
#include "shared_memory.h"
#include "../../../common/constants.h"
#include "../../../common/ipc_types.h"

namespace ipc
{

    /**
     * IPC Manager: High-level facade for all inter-process communication.
     *
     * Responsibilities:
     * 1. Database queries via PostgreSQL (unix_socket.h)
     * 2. Agent status updates via FIFOs (fifo.h)
     * 3. Shared memory synchronization (shared_memory.h)
     *
     * Thread Safety:
     * - Singleton pattern ensures only one instance
     * - All database operations are serialized via UnixSocket's internal mutex
     * - Shared memory operations are protected by pthread_mutex_t (robust, process-shared)
     *
     * CRITICAL: The database connection (db_socket) is NOT thread-safe.
     * All calls to executeQuery() must be serialized by higher layers if called from
     * multiple threads. However, UnixSocket::execute() will serialize access
     * internally if you use that method directly.
     */
    class IpcManager
    {
    private:
        UnixSocket db_socket;
        Fifo fifo_handler;
        SharedMemory shm_handler;

        // Private Constructor for Singleton pattern
        IpcManager();

    public:
        // Singleton Instance getter
        static IpcManager &getInstance()
        {
            static IpcManager instance;
            return instance;
        }

        // Delete copy constructor and assignment (Singleton rule)
        IpcManager(const IpcManager &) = delete;
        IpcManager &operator=(const IpcManager &) = delete;

        // ========== Database Interface ==========

        /**
         * Connects to the PostgreSQL database via Unix domain socket.
         *
         * Must be called once during daemon initialization before any database
         * operations. Safe to call multiple times (subsequent calls are no-ops
         * if already connected).
         *
         * @return ResultCode::OK on success, DB_ERROR on connection failure
         */
        JusticeFlow::ResultCode connectDatabase();

        /**
         * Closes the PostgreSQL database connection.
         * Safe to call multiple times or without a prior connect() call.
         */
        void disconnectDatabase();

        /**
         * Executes a SQL query on the connected database.
         *
         * For queries returning results (SELECT), out_results is populated as:
         *   out_results[row_index][col_index] = cell value (as string)
         *
         * All database access is serialized internally by UnixSocket.
         *
         * CRITICAL: This method is NOT async-signal-safe. Do NOT call from
         * signal handlers. Instead, set a volatile sig_atomic_t flag and call
         * this from the main event loop.
         *
         * @param query The SQL query string (may be SELECT, INSERT, UPDATE, DELETE, etc.)
         * @param results Output parameter populated with result rows (for SELECT queries)
         * @return ResultCode::OK on success, DB_ERROR on query failure
         */
        JusticeFlow::ResultCode executeQuery(
            const std::string &query,
            std::vector<std::vector<std::string>> &results);

        // ========== Agent Status Interface ==========

        /**
         * Reads an agent's current status from the FIFO.
         *
         * Each agent (hotspot, priority, workload) has a corresponding FIFO where it
         * writes status updates. This method reads the latest update non-blockingly.
         *
         * @param agent_index Index into agents array: 0=hotspot, 1=priority, 2=workload
         * @param out_msg Output parameter populated with the agent's status message
         * @return ResultCode::OK if message read successfully
         *         ResultCode::NOT_FOUND if FIFO is empty or no new data
         *         ResultCode::FILE_SYSTEM_ERROR on I/O failure
         *         ResultCode::INVALID_INPUT if agent_index out of range
         */
        JusticeFlow::ResultCode readAgentStatus(int agent_index, AgentStatusMessage &out_msg);

        /**
         * Updates an agent's status in the shared memory table.
         *
         * Acquires the shared status table lock, updates agents[agent_index],
         * and releases the lock. Other processes can concurrently read this table.
         *
         * CRITICAL: This method acquires a process-shared mutex from shared memory.
         * If the process holding the mutex crashes, the ROBUST attribute ensures
         * the next caller receives EOWNERDEAD and can call pthread_mutex_consistent.
         *
         * @param agent_index Index into agents array: 0=hotspot, 1=priority, 2=workload
         * @param status The AgentStatus struct to write into shared memory
         * @return ResultCode::OK on success
         *         ResultCode::INVALID_INPUT if agent_index out of range
         *         ResultCode::INVALID_STATE if shared memory not attached
         */
        JusticeFlow::ResultCode updateAgentStatus(int agent_index, const AgentStatus &status);

        // ========== Shared Memory Interface ==========

        /**
         * Returns a pointer to the shared status table in shared memory.
         *
         * This allows callers to read agent status directly without going through
         * updateAgentStatus(). However, writes MUST use updateAgentStatus() for
         * proper synchronization.
         *
         * CRITICAL: The returned pointer is valid only while the SharedMemory
         * object remains allocated. In practice, this is the entire daemon lifetime.
         *
         * @return Pointer to SharedStatusTable in shared memory, or nullptr if not attached
         */
        SharedStatusTable *getStatusTable();
    };

} // namespace ipc
