#pragma once

#include <string>
#include <vector>
#include "common/constants.h"
#include "common/common.h"
#include "audit/include/audit_log.h"
#include "audit/include/audit_query.h"

namespace integration
{

    /**
     * @struct AuditQueryParams
     * @brief Parameters for querying audit history
     *
     * Flexible query structure for different audit access patterns.
     */
    struct AuditQueryParams
    {
        enum class QueryType
        {
            CASE_HISTORY,    ///< All changes related to a case
            OFFICER_ACTIONS, ///< Officer's actions in time window
            TABLE_CHANGES,   ///< Changes to specific record
            TIME_WINDOW      ///< All changes in time window
        };

        QueryType query_type;
        int case_id;                          ///< For CASE_HISTORY
        int officer_id;                       ///< For OFFICER_ACTIONS
        int record_id;                        ///< For TABLE_CHANGES
        JusticeFlow::AuditedTable table_name; ///< For TABLE_CHANGES
        time_t from_time;                     ///< For TIME_WINDOW, OFFICER_ACTIONS
        time_t to_time;                       ///< For TIME_WINDOW, OFFICER_ACTIONS
    };

    /**
     * @file audit_bridge.h
     * @brief Single entry point for all subsystems to interact with audit system
     *
     * Singleton facade that mediates all audit interactions:
     *   1. Log operations (delegates to DB trigger via operation execution)
     *   2. Query audit history (reads via audit_log, audit_query)
     *
     * Every subsystem calls AuditBridge::getInstance() rather than touching
     * audit_log or audit_query directly. This is the ONLY audit file that
     * subsystems 1, 2, and 3 include.
     *
     * The bridge never writes to audit.Audit_Log directly — it executes
     * the operation that fires the SECURITY DEFINER trigger which writes
     * the audit entry. This ensures audit records are immutable and complete.
     *
     * Thread Safety: Singleton pattern with const operations. Safe for concurrent use.
     *
     * Dependencies: audit/include/audit_log.h, audit/include/audit_query.h
     */

    class AuditBridge
    {
    private:
        AuditBridge() = default;
        ~AuditBridge() = default;

        AuditBridge(const AuditBridge &) = delete;
        AuditBridge &operator=(const AuditBridge &) = delete;

    public:
        /**
         * @brief Gets the singleton instance
         *
         * @return Reference to the single AuditBridge instance
         */
        static AuditBridge &getInstance();

        /**
         * Logs an operation by executing it and letting the DB trigger capture the audit entry.
         *
         * The bridge does NOT write to audit.Audit_Log directly. Instead:
         *   1. Caller executes the operation (INSERT, UPDATE, or DELETE)
         *   2. DB trigger fires (SECURITY DEFINER, runs as DB admin)
         *   3. Trigger writes complete audit entry with full context
         *
         * This design ensures:
         *   - Audit entries are immutable (app cannot tamper)
         *   - All metadata is captured (officer_id, session_id, client_ip, timestamp)
         *   - No race conditions between operation and audit write
         *
         * @param operation The SQL operation that was/will be executed
         *                 Examples: "INSERT INTO cases ...", "UPDATE evidence SET status = ..."
         * @param table The table being modified (for context)
         * @param record_id The primary key of the record being modified
         * @param context Additional context string for audit trail (e.g., "Warrant issued for case #123")
         * @return ResultCode::OK if operation logged successfully
         *         ResultCode::DB_ERROR if operation execution failed
         *
         * @note This is a documentation/logging method. The actual audit entry is created
         *       by the database trigger when the operation executes.
         *
         * @example
         *   // When enforcement issues a warrant:
         *   std::string operation = "INSERT INTO subsystem3.warrants (case_id, ...) VALUES (...)";
         *   AuditBridge::getInstance().log(
         *       operation,
         *       JusticeFlow::AuditedTable::WARRANTS,
         *       warrant_id,
         *       "Arrest warrant issued for accused CNIC"
         *   );
         */
        JusticeFlow::ResultCode log(const std::string &operation,
                                    JusticeFlow::AuditedTable table,
                                    int record_id,
                                    const std::string &context);

        /**
         * Queries audit history with flexible parameters.
         *
         * High-level audit query interface used by subsystems to read audit history.
         * Supports multiple query patterns via AuditQueryParams::QueryType.
         *
         * @param params Query parameters specifying what to retrieve
         * @param out_records Output vector of audit records
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if no matching records
         *         ResultCode::DB_ERROR on query failure
         *
         * @example
         *   // Query case history:
         *   AuditQueryParams params;
         *   params.query_type = AuditQueryParams::QueryType::CASE_HISTORY;
         *   params.case_id = 123;
         *   std::vector<audit::AuditRecord> records;
         *   AuditBridge::getInstance().query(params, records);
         *
         *   // Query officer actions in last 24 hours:
         *   params.query_type = AuditQueryParams::QueryType::OFFICER_ACTIONS;
         *   params.officer_id = 45;
         *   params.from_time = std::time(nullptr) - 86400;
         *   params.to_time = std::time(nullptr);
         *   AuditBridge::getInstance().query(params, records);
         */
        JusticeFlow::ResultCode query(const AuditQueryParams &params,
                                      std::vector<audit::AuditRecord> &out_records);
    };

} // namespace integration