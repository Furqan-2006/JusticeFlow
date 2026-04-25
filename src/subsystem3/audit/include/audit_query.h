#pragma once

#include <string>
#include <vector>
#include <ctime>
#include "common/constants.h"
#include "audit_log.h"

namespace audit
{

    /**
     * @struct CaseTimeline
     * @brief Chronological view of all changes related to a case
     *
     * Combines audit records from all tables related to a case into a single
     * structured timeline for dashboard display.
     */
    struct CaseTimeline
    {
        int case_id;                               ///< Case ID
        std::vector<AuditRecord> timeline_entries; ///< All changes in chronological order (oldest first)
        int total_changes;                         ///< Total number of changes across all tables
        time_t first_change;                       ///< Timestamp of first change (case creation)
        time_t last_change;                        ///< Timestamp of last change (most recent)
    };

    /**
     * @struct StationActivitySummary
     * @brief Aggregated activity at a station during a time window
     *
     * Summary statistics of officer actions at a station.
     */
    struct StationActivitySummary
    {
        int station_id;                            ///< Station ID
        time_t period_from;                        ///< Start of time window
        time_t period_to;                          ///< End of time window
        int total_officers_active;                 ///< Unique officers who took actions
        int total_actions;                         ///< Total number of audit-logged actions
        int inserts;                               ///< Count of INSERT actions
        int updates;                               ///< Count of UPDATE actions
        int deletes;                               ///< Count of DELETE actions (soft-deletes)
        std::vector<AuditRecord> activity_records; ///< All audit records in window
    };

    /**
     * @file audit_query.h
     * @brief Higher-level audit query composition
     *
     * Built on top of audit_log.h, composes multiple queries into
     * structured results for dashboard consumption.
     *
     * Thread Safety: All functions are read-only and thread-safe.
     *
     * Dependencies: audit_log.h, utils/time_utils.h
     */

    class AuditQuery
    {
    public:
        /**
         * Retrieves the full chronological timeline of a case.
         *
         * Joins change history across all audited tables related to a case
         * (cases, evidence, warrants, arrests, etc.) into a single chronological view.
         *
         * Timeline is ordered oldest-to-newest for chronological narrative.
         * Useful for:
         *   - Dashboard case history tab
         *   - Compliance audits
         *   - Forensic investigation (tracking case evolution)
         *
         * @param case_id The case to query
         * @param out_timeline Output structure populated with timeline entries
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if case doesn't exist or has no audit history
         *         ResultCode::DB_ERROR on query failure
         *
         * @example
         *   CaseTimeline timeline;
         *   auto result = AuditQuery::getFullCaseTimeline(case_id, timeline);
         *   if (result == ResultCode::OK) {
         *       // Display timeline[0] (oldest) to timeline[n] (newest)
         *       for (const auto& entry : timeline.timeline_entries) {
         *           dashboard.display(entry.changed_at, entry.action, entry.old_value, entry.new_value);
         *       }
         *   }
         */
        static JusticeFlow::ResultCode getFullCaseTimeline(int case_id, CaseTimeline &out_timeline);

        /**
         * Retrieves aggregated activity at a station during a time window.
         *
         * Queries all officer actions at a station and returns:
         *   - Total unique officers who acted
         *   - Action counts by type (INSERT, UPDATE, DELETE)
         *   - Full list of records for detailed view
         *
         * Useful for:
         *   - Station activity dashboard
         *   - Workload analysis
         *   - Shift summaries
         *
         * @param station_id The station to query
         * @param from Start time (UTC) — inclusive
         * @param to End time (UTC) — inclusive
         * @param out_summary Output structure populated with activity data
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if station has no activity in window
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Implementation:
         *       1. Query all officers at station
         *       2. For each officer, call AuditLog::getOfficerActions(officer_id, from, to)
         *       3. Aggregate results and count by action type
         *       4. Return summary + full record list
         */
        static JusticeFlow::ResultCode getStationActivity(int station_id, time_t from, time_t to,
                                                          StationActivitySummary &out_summary);
    };

} // namespace audit