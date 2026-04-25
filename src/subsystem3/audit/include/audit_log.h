#pragma once

#include <string>
#include <vector>
#include <ctime>
#include "common/constants.h"
#include "common/common.h"

namespace audit
{

    /**
     * @struct AuditRecord
     * @brief Plain data structure mirroring audit.Audit_Log table columns
     *
     * Represents a single audit log entry from the database.
     * All timestamps are in UTC (time_t).
     */
    struct AuditRecord
    {
        int audit_id;                         ///< Primary key
        JusticeFlow::AuditedTable table_name; ///< Which table was modified
        int record_id;                        ///< ID of record that changed
        JusticeFlow::AuditAction action;      ///< INSERT, UPDATE, DELETE
        std::string old_value;                ///< Previous value (JSON string) or empty for INSERT
        std::string new_value;                ///< New value (JSON string) or empty for DELETE
        std::string changed_by_user;          ///< Username of officer who made change
        int changed_by_officer_id;            ///< Officer ID (0 if system/trigger)
        std::string changed_by_belt;          ///< Officer's belt number
        int client_process_id;                ///< Process ID of client that triggered change
        std::string client_ip;                ///< IP address of client (if remote)
        time_t changed_at;                    ///< UTC timestamp of change
    };

    /**
     * @file audit_log.h
     * @brief C++ read interface into audit.Audit_Log table
     *
     * Read-only queries into the immutable audit log. Database trigger owns all writes.
     * Four query wrappers for common audit access patterns.
     *
     * Does NOT own a DB connection — caller provides connection via IPC manager.
     * All queries are SELECT-only (no writes).
     *
     * Thread Safety: All functions are read-only and thread-safe (no state modifications).
     *
     * Dependencies: common/constants.h, common/common.h, utils/time_utils.h
     */

    class AuditLog
    {
    public:
        /**
         * Retrieves all changes related to a specific case.
         *
         * Queries audit.Audit_Log for all changes to:
         *   - subsystem2.cases (case metadata)
         *   - subsystem2.evidence (evidence linked to case)
         *   - subsystem3.warrants (warrants for case)
         *   - subsystem3.arrests (arrests in case)
         *   - subsystem3.bail_records (bail records for case)
         *   - Any case_officer, complainant, victim, witness, accused records
         *
         * Results are returned in reverse chronological order (newest first).
         *
         * @param case_id The case ID to query
         * @param out_records Output vector populated with AuditRecord structs
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if case has no audit history
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT * FROM audit.Audit_Log WHERE record_id = case_id
         *       OR (table_name IN (...) AND links to case_id)
         *       ORDER BY changed_at DESC
         */
        static JusticeFlow::ResultCode getChangeHistory(int case_id,
                                                        std::vector<AuditRecord> &out_records);

        /**
         * Retrieves all actions performed by a specific officer within a time window.
         *
         * Queries audit.Audit_Log for all changes initiated by an officer
         * (where changed_by_officer_id = officer_id).
         *
         * Filters by time window [from, to] in UTC.
         * Results are returned in reverse chronological order (newest first).
         *
         * @param officer_id The officer ID to query
         * @param from Start time (UTC) — inclusive
         * @param to End time (UTC) — inclusive
         * @param out_records Output vector populated with AuditRecord structs
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if officer has no actions in window
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT * FROM audit.Audit_Log
         *       WHERE changed_by_officer_id = officer_id
         *       AND changed_at >= from AND changed_at <= to
         *       ORDER BY changed_at DESC
         */
        static JusticeFlow::ResultCode getOfficerActions(int officer_id, time_t from, time_t to,
                                                         std::vector<AuditRecord> &out_records);

        /**
         * Retrieves all changes to a specific record in a specific table.
         *
         * Tracks the full modification history of a single record.
         * Useful for understanding how an evidence item, warrant, or arrest evolved.
         *
         * Results are returned in chronological order (oldest first).
         *
         * @param table_name The table being tracked (e.g., EVIDENCE, WARRANTS)
         * @param record_id The primary key in that table
         * @param out_records Output vector populated with AuditRecord structs
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if record has no audit history
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT * FROM audit.Audit_Log
         *       WHERE table_name = table_name AND record_id = record_id
         *       ORDER BY changed_at ASC
         */
        static JusticeFlow::ResultCode getTableChanges(JusticeFlow::AuditedTable table_name,
                                                       int record_id,
                                                       std::vector<AuditRecord> &out_records);

        /**
         * Retrieves all audit log entries within a time window.
         *
         * Broad query for auditing system-wide activity.
         * Useful for compliance reporting, forensic investigation, and system health checks.
         *
         * Results are returned in reverse chronological order (newest first).
         * Large time windows may return many records — caller should paginate if needed.
         *
         * @param from Start time (UTC) — inclusive
         * @param to End time (UTC) — inclusive
         * @param out_records Output vector populated with AuditRecord structs
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if no changes in window
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT * FROM audit.Audit_Log
         *       WHERE changed_at >= from AND changed_at <= to
         *       ORDER BY changed_at DESC
         *
         *       For large windows (e.g., 30+ days), consider adding pagination
         *       or filtering by table_name to avoid excessive result sets.
         */
        static JusticeFlow::ResultCode queryByTimeWindow(time_t from, time_t to,
                                                         std::vector<AuditRecord> &out_records);
    };

} // namespace audit