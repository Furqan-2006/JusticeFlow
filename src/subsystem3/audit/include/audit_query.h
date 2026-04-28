#pragma once

/**
 * @file audit_query.h
 * @brief Stateless query builder for audit.Audit_Log reads.
 *
 * Internal implementation detail of the audit module.
 * External callers NEVER include this file — include audit_manager.h instead.
 *
 * Design Patterns
 * ---------------
 * Template Method:  _executeAndMap() is the skeleton shared by every public
 *                   method. Each method supplies its own SQL and parameters;
 *                   the skeleton executes, maps rows, manages PGresult lifetime.
 *
 * Observer (passive): The C++ layer is purely read-only. All writes to
 *                   audit.Audit_Log happen through SECURITY DEFINER triggers —
 *                   no application code ever INSERTs here. The trigger fires as
 *                   the observer of every DML on the 8 audited tables.
 *
 * Thread Safety
 * -------------
 * All functions are stateless. Thread-safe by construction — no shared state.
 *
 * SQL Injection
 * -------------
 * All queries use PQexecParams with parameterised placeholders ($1, $2…).
 * No string concatenation is ever used to build SQL.
 *
 * Memory
 * ------
 * PGresult* lifetime is managed entirely inside _executeAndMap.
 * Callers never call PQclear.
 *
 * Dependencies
 * ------------
 * libpq (PGconn*, PQexecParams, PQclear), common/constants.h
 */

#include <vector>
#include <ctime>
#include <postgresql/libpq-fe.h>
#include "common/constants.h"

namespace audit
{

    // -----------------------------------------------------------------------
    // AuditRecord field-size constants
    // These values mirror the column widths in audit.Audit_Log.
    // Changing them requires a matching schema migration.
    // -----------------------------------------------------------------------
    constexpr int AUDIT_TABLE_NAME_LEN = 32; ///< e.g. "WARRANTS", "EVIDENCE"
    constexpr int AUDIT_OPERATION_LEN = 16;  ///< "INSERT", "UPDATE", "DELETE"
    constexpr int AUDIT_BELT_LEN = 32;       ///< Officer belt number string
    constexpr int AUDIT_ADDR_LEN = 48;       ///< IPv6 max = 45 chars + NUL
    constexpr int AUDIT_JSONB_LEN = 8192;    ///< JSONB snapshot of OLD/NEW row

    /**
     * @struct AuditRecord
     * @brief Plain data struct mirroring every column of audit.Audit_Log.
     *
     * Fixed-size char arrays throughout — no std::string.
     * Safe to memcpy, pass across module boundaries, and serialise over IPC.
     *
     * Numeric IDs use int / time_t for direct comparison without parsing.
     *
     * Field mapping (struct ↔ column):
     *   log_id       ← audit_log_id    (PK, SERIAL)
     *   table_name   ← table_name      (VARCHAR 32)
     *   record_pk    ← record_pk       (INTEGER — PK of the modified row)
     *   operation    ← operation       (VARCHAR 16 — 'INSERT'/'UPDATE'/'DELETE')
     *   officer_id   ← officer_id      (INTEGER — 0 for system/trigger)
     *   belt_number  ← belt_number     (VARCHAR 32)
     *   backend_pid  ← backend_pid     (INTEGER — pg_backend_pid())
     *   client_addr  ← client_addr     (INET cast to TEXT)
     *   old_values   ← old_values      (JSONB — NULL for INSERT)
     *   new_values   ← new_values      (JSONB — NULL for DELETE)
     *   changed_at   ← changed_at      (TIMESTAMPTZ cast to Unix epoch)
     */
    struct AuditRecord
    {
        int log_id;
        char table_name[AUDIT_TABLE_NAME_LEN];
        int record_pk;
        char operation[AUDIT_OPERATION_LEN];
        int officer_id;
        char belt_number[AUDIT_BELT_LEN];
        int backend_pid;
        char client_addr[AUDIT_ADDR_LEN];
        char old_values[AUDIT_JSONB_LEN];
        char new_values[AUDIT_JSONB_LEN];
        time_t changed_at;
    };

    /**
     * @class AuditQuery
     * @brief Stateless parameterised query executor for audit.Audit_Log.
     *
     * Never instantiated. All methods are static.
     * Caller owns PGconn* — this class never touches the connection lifecycle.
     */
    class AuditQuery
    {
    public:
        /**
         * Retrieves all audit log entries related to a case.
         *
         * Covers the case row itself plus every linked entity:
         *   Evidence, Warrants, Arrests, Bail_Records, Charge_Sheets, Accused.
         * Returned newest-first.
         *
         * @param conn   Active PostgreSQL connection. Must not be NULL.
         * @param case_id  Primary key of the case.
         * @param out    Vector appended with matching AuditRecords.
         * @return ResultCode::OK            — records found and mapped
         *         ResultCode::NOT_FOUND     — no audit history for case_id
         *         ResultCode::DB_ERROR      — PQexecParams returned an error status
         */
        static JusticeFlow::ResultCode getChangeHistory(
            PGconn *conn, int case_id,
            std::vector<AuditRecord> &out);

        /**
         * Retrieves all actions taken by a single officer in [from, to].
         *
         * Results are newest-first within the time window.
         *
         * @param conn       Active PostgreSQL connection.
         * @param officer_id Officer whose actions are requested.
         * @param from       Inclusive start of the window (Unix epoch, UTC).
         * @param to         Inclusive end of the window (Unix epoch, UTC).
         * @param out        Vector appended with matching AuditRecords.
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        static JusticeFlow::ResultCode getOfficerActions(
            PGconn *conn, int officer_id, time_t from, time_t to,
            std::vector<AuditRecord> &out);

        /**
         * Retrieves the full mutation history of a single record.
         *
         * @param conn        Active PostgreSQL connection.
         * @param table_name  Audited table name (e.g. "WARRANTS").
         * @param record_id   Primary key of the record.
         * @param out         Vector appended (oldest-first — chronological narrative).
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        static JusticeFlow::ResultCode getTableChanges(
            PGconn *conn, const char *table_name, int record_id,
            std::vector<AuditRecord> &out);

        /**
         * Broad dump of all audit activity within a time window.
         *
         * For large windows (> 30 days) consider adding table_name filtering
         * at the call site to cap result set size. Returned newest-first.
         *
         * @param conn  Active PostgreSQL connection.
         * @param from  Inclusive start (Unix epoch, UTC).
         * @param to    Inclusive end (Unix epoch, UTC).
         * @param out   Vector appended with matching AuditRecords.
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        static JusticeFlow::ResultCode queryByTimeWindow(
            PGconn *conn, time_t from, time_t to,
            std::vector<AuditRecord> &out);

        /**
         * SQL-level suspicious activity detection for a station.
         *
         * Returns AuditRecords that match one or more of the four detection
         * patterns. The caller inspects each record to determine which pattern
         * triggered it (using operation, backend_pid, changed_at fields).
         *
         * Detection patterns (all evaluated over the last 24 hours):
         *
         * 1. BULK_CHANGE
         *    More than 20 rows written by a single backend_pid within any
         *    one-hour window. Detected via GROUP BY + HAVING in SQL.
         *    Indicator: batch data manipulation.
         *
         * 2. RAPID_DELETE
         *    Five or more DELETE operations by the same officer_id within
         *    any five-minute window. Especially concerning on EVIDENCE or
         *    WARRANTS tables.
         *    Indicator: evidence tampering.
         *
         * 3. AFTER_HOURS
         *    Any operation performed outside 06:00–22:00 UTC.
         *    Severity increases if the operation is a DELETE or touches CASES.
         *
         * 4. JURISDICTION_VIOLATION
         *    An officer whose station_id (from subsystem1.officers) does not
         *    match the station of the record being modified.
         *    Indicator: unauthorised cross-station access.
         *
         * @param conn        Active PostgreSQL connection.
         * @param station_id  Station to analyse.
         * @param out         Vector appended with flagged AuditRecords.
         * @return ResultCode::OK (even if out is empty — no findings is valid)
         *         ResultCode::DB_ERROR on query failure
         */
        static JusticeFlow::ResultCode detectSuspiciousActivity(
            PGconn *conn, int station_id,
            std::vector<AuditRecord> &out);

    private:
        /**
         * @brief Template Method skeleton — shared by every public query.
         *
         * Executes a parameterised SQL statement via PQexecParams, maps every
         * result row into an AuditRecord, appends to out, and calls PQclear.
         * Callers never touch PGresult* directly.
         *
         * @param conn     Active PostgreSQL connection.
         * @param sql      Parameterised SQL string ($1, $2, … placeholders).
         * @param params   Array of C-string parameter values (may be NULL).
         * @param nparams  Length of params array (0 if params is NULL).
         * @param out      Output vector — rows appended here.
         * @return ResultCode::OK      — at least one row found and mapped
         *         ResultCode::NOT_FOUND — PGRES_TUPLES_OK but zero rows
         *         ResultCode::DB_ERROR  — PGRES_FATAL_ERROR or connection error
         */
        static JusticeFlow::ResultCode _executeAndMap(
            PGconn *conn,
            const char *sql,
            const char *const *params,
            int nparams,
            std::vector<AuditRecord> &out);

        /**
         * @brief Maps a single PGresult row into an AuditRecord.
         *
         * Column-to-field mapping is positional and matches the SELECT column
         * order defined in each SQL constant inside audit_query.cpp.
         * Changing the SQL SELECT list requires updating this function too.
         *
         * @param res  PGresult* from a successful PQexecParams call.
         * @param row  Zero-based row index.
         * @param rec  Output record (fields overwritten, not appended).
         */
        static void _mapRow(PGresult *res, int row, AuditRecord &rec);
    };

} // namespace audit