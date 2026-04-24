#pragma once

#include <string>
#include <cstring>
#include <pthread.h>
#include <vector>
#include <postgresql/libpq-fe.h>
#include "../../../common/constants.h"

namespace ipc
{

    /**
     * Thread-safe wrapper around a PostgreSQL connection via Unix domain socket.
     *
     * CRITICAL: All access to conn and PQexec operations MUST be protected by
     * holding the db_mutex lock. Call lock()/unlock() explicitly, or better yet,
     * use a RAII MutexGuard wrapper (see sync.h) to ensure exception safety.
     *
     * PQexec is NOT thread-safe; calling it from multiple threads on the same
     * PGconn simultaneously causes data races. This class enforces serialization
     * via mutex.
     */
    class UnixSocket
    {
    private:
        PGconn *conn;
        std::string conn_string;
        pthread_mutex_t db_mutex;

        // Helper to acquire lock - returns true on success, false on error
        bool acquireLock();

        // Helper to release lock - returns true on success
        bool releaseLock();

    public:
        explicit UnixSocket(const std::string &connectionString);
        ~UnixSocket();

        // Prevent copying to avoid double-destruction of mutex
        UnixSocket(const UnixSocket &) = delete;
        UnixSocket &operator=(const UnixSocket &) = delete;

        /**
         * Establishes connection to PostgreSQL via the provided connection string.
         * Must be called and return OK before any execute() or isHealthy() calls.
         *
         * @return ResultCode::OK if connection succeeded, DB_ERROR otherwise
         */
        JusticeFlow::ResultCode connect();

        /**
         * Closes the current database connection.
         * Safe to call multiple times or without a prior connect() call.
         */
        void disconnect();

        /**
         * Checks if the connection is healthy (connected and ready for queries).
         *
         * CRITICAL: This method acquires the mutex before checking conn.
         * Do NOT call this from within a lock/unlock pair without proper handling.
         *
         * @return true if connected and ready, false otherwise
         */
        bool isHealthy() const;

        /**
         * Executes a query on the database connection.
         *
         * CRITICAL: This method acquires the lock internally before calling PQexec.
         * The caller does NOT need to call lock()/unlock() before calling this.
         *
         * For result rows, out_results is populated as a vector of vectors:
         *   out_results[row_index][col_index] = cell value (as string)
         *
         * @param query The SQL query to execute
         * @param out_results Output parameter populated with result rows
         * @return ResultCode::OK on success, DB_ERROR on failure
         */
        JusticeFlow::ResultCode execute(const std::string &query, std::vector<std::vector<std::string>> &out_results);

        /**
         * Explicitly acquires the database lock.
         *
         * WARNING: Use with extreme caution. Prefer execute() which handles locking internally.
         * If you must use lock(), ALWAYS follow with unlock() in all code paths.
         * Better yet, wrap in a RAII guard to ensure exception safety.
         *
         * @return ResultCode::OK on success, or error code on failure
         */
        JusticeFlow::ResultCode lock();

        /**
         * Explicitly releases the database lock.
         *
         * WARNING: Only call if you previously called lock() successfully.
         * Must be called exactly once per lock() call.
         *
         * @return ResultCode::OK on success, or error code on failure
         */
        JusticeFlow::ResultCode unlock();
    };

} // namespace ipc
