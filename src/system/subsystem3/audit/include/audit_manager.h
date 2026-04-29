#pragma once

/**
 * @file audit_manager.h
 * @brief Singleton entry point for all audit queries in Subsystem 3.
 *
 * THIS is the only audit header that other S3 modules include.
 * AuditQuery is an internal implementation detail and is never included
 * outside the audit/ directory.
 *
 * Role in the Architecture
 * ------------------------
 * AuditManager is a read-only service. It owns the dedicated PGconn* used
 * for all audit reads, separate from the worker thread pool connection.
 * Writes to audit.Audit_Log happen exclusively via SECURITY DEFINER triggers
 * fired by DML on the 8 audited tables — no application code ever INSERTs.
 *
 * Ownership & Lifecycle
 * ---------------------
 * connect(conninfo) must be called once during process startup, before any
 * query method is used. disconnect() is called on clean shutdown.
 * If the connection is lost, every query method attempts one reconnect before
 * returning DB_ERROR to the caller.
 *
 * Design Patterns
 * ---------------
 * Singleton:  One instance per process — audit reads share a single connection.
 * Facade:     Hides PGconn*, PQexecParams, and all AuditQuery internals.
 *             Callers only see typed methods returning ResultCode.
 *
 * Thread Safety
 * -------------
 * The underlying PGconn* is NOT thread-safe (libpq restriction).
 * Concurrent callers must acquire a mutex before calling any method.
 * The recommended pattern is to call AuditManager from a dedicated
 * audit-reader thread rather than from the worker thread pool.
 *
 * Dependencies
 * ------------
 * audit/include/audit_query.h (internal), libpq, common/constants.h
 */

#include <vector>
#include <ctime>
#include <postgresql/libpq-fe.h>
#include "common/constants.h"
#include "audit_query.h" // exposes AuditRecord to callers

namespace audit
{

    /**
     * @class AuditManager
     * @brief Singleton facade over audit.Audit_Log reads.
     *
     * Usage:
     * @code
     *   // During process startup:
     *   audit::AuditManager::getInstance().connect("host=localhost dbname=justiceflow");
     *
     *   // Query audit history for a case:
     *   std::vector<audit::AuditRecord> records;
     *   auto rc = audit::AuditManager::getInstance().getChangeHistory(case_id, records);
     *   if (rc == JusticeFlow::ResultCode::OK) {
     *       // process records…
     *   }
     *
     *   // Detect suspicious activity at a station:
     *   std::vector<audit::AuditRecord> flags;
     *   audit::AuditManager::getInstance().detectSuspiciousActivity(station_id, flags);
     * @endcode
     */
    class AuditManager
    {
    private:
        PGconn *conn_; ///< Dedicated read-only connection. Never shared.

        AuditManager();
        ~AuditManager();
        AuditManager(const AuditManager &) = delete;
        AuditManager &operator=(const AuditManager &) = delete;

        /**
         * @brief Attempts to reconnect using the stored conninfo string.
         * Called automatically when a query detects a broken connection.
         * @return true if reconnection succeeded.
         */
        bool _reconnect();

        char conninfo_[256]; ///< Stored for reconnect attempts. Never exposed.

    public:
        /**
         * @brief Returns the single AuditManager instance.
         *
         * Thread-safe since C++11 (static local initialisation is
         * guaranteed to execute exactly once).
         */
        static AuditManager &getInstance();

        // -----------------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------------

        /**
         * @brief Establishes the dedicated audit read connection.
         *
         * Must be called once before any query method is used.
         * Second call on an already-connected instance is a no-op (returns OK).
         *
         * @param conninfo  libpq connection string,
         *                  e.g. "host=localhost dbname=justiceflow user=audit_reader".
         * @return ResultCode::OK       — connection established
         *         ResultCode::DB_ERROR — PQconnectdb failed (bad conninfo or DB down)
         */
        JusticeFlow::ResultCode connect(const char *conninfo);

        /**
         * @brief Closes the audit connection.
         * Idempotent — safe to call even if connect() was never called.
         */
        void disconnect();

        // -----------------------------------------------------------------------
        // Query operations
        // All delegate to AuditQuery, passing conn_.
        // -----------------------------------------------------------------------

        /**
         * Retrieves all audit history for a case and its linked entities.
         *
         * Covers: Cases, Evidence, Warrants, Arrests, Bail_Records,
         *         Charge_Sheets, Accused rows related to case_id.
         * Returned newest-first.
         *
         * @param case_id   The case to query.
         * @param out       Appended with matching AuditRecords.
         * @return ResultCode::OK        — one or more records found
         *         ResultCode::NOT_FOUND — no audit history exists
         *         ResultCode::DB_ERROR  — connection or query failure
         */
        JusticeFlow::ResultCode getChangeHistory(int case_id,
                                                 std::vector<AuditRecord> &out);

        /**
         * Retrieves all audit entries for a single officer within [from, to].
         *
         * @param officer_id  Officer to query.
         * @param from        Inclusive window start (Unix epoch, UTC).
         * @param to          Inclusive window end (Unix epoch, UTC).
         * @param out         Appended with matching AuditRecords (newest-first).
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        JusticeFlow::ResultCode getOfficerActions(int officer_id,
                                                  time_t from,
                                                  time_t to,
                                                  std::vector<AuditRecord> &out);

        /**
         * Retrieves the full mutation history of a single record.
         *
         * Useful for chain-of-custody views: "how did evidence item 42 evolve?"
         * Returned oldest-first (chronological narrative).
         *
         * @param table_name  Audited table (e.g. "EVIDENCE", "WARRANTS").
         * @param record_id   Primary key of the record.
         * @param out         Appended with matching AuditRecords.
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        JusticeFlow::ResultCode getTableChanges(const char *table_name,
                                                int record_id,
                                                std::vector<AuditRecord> &out);

        /**
         * Broad dump of all audit activity within a time window.
         *
         * Intended for compliance reporting and forensic investigation.
         * Returned newest-first. Consider providing a short window (≤7 days)
         * to avoid very large result sets.
         *
         * @param from  Inclusive window start (Unix epoch, UTC).
         * @param to    Inclusive window end (Unix epoch, UTC).
         * @param out   Appended with matching AuditRecords.
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        JusticeFlow::ResultCode queryByTimeWindow(time_t from,
                                                  time_t to,
                                                  std::vector<AuditRecord> &out);

        /**
         * SQL-level suspicious activity detection for a station (last 24 h).
         *
         * Runs the four-pattern detection query against audit.Audit_Log.
         * Detected patterns:
         *   1. BULK_CHANGE      — >20 rows from one backend_pid in 1 hour
         *   2. RAPID_DELETE     — 5+ DELETEs by one officer in 5 minutes
         *   3. AFTER_HOURS      — any DML outside 06:00–22:00 UTC
         *   4. JURISDICTION     — officer acting outside their station
         *
         * @param station_id  Station to analyse.
         * @param out         Appended with flagged AuditRecords.
         *                    May be empty — no findings is a valid, non-error result.
         * @return ResultCode::OK       — query executed (out may be empty)
         *         ResultCode::DB_ERROR — connection or query failure
         */
        JusticeFlow::ResultCode detectSuspiciousActivity(int station_id,
                                                         std::vector<AuditRecord> &out);
    };

} // namespace audit