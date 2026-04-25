#pragma once

#include <string>
#include <vector>
#include <ctime>
#include "common/constants.h"
#include "audit_log.h"

namespace audit
{

    /**
     * @struct SuspiciousActivity
     * @brief A single flagged suspicious activity record
     *
     * Represents one instance of detected suspicious behavior.
     */
    struct SuspiciousActivity
    {
        std::string activity_type;            ///< Type: BULK_CHANGE, RAPID_DELETE, AFTER_HOURS, JURISDICTION_VIOLATION
        int officer_id;                       ///< Officer who performed action
        std::string officer_name;             ///< Officer's name (for readability)
        int session_id;                       ///< Session context
        time_t occurred_at;                   ///< Timestamp of suspicious action
        std::string description;              ///< Human-readable description
        JusticeFlow::AuditedTable table_name; ///< Table affected
        int record_id;                        ///< Record ID affected
        int severity_score;                   ///< 1-10 score (higher = more suspicious)
    };

    /**
     * @struct SuspiciousActivityReport
     * @brief Report of suspicious activities at a station
     *
     * Aggregates all detected suspicious activities for a station.
     */
    struct SuspiciousActivityReport
    {
        int station_id;                                  ///< Station analyzed
        time_t generated_at;                             ///< Report generation timestamp
        int total_suspicious_activities;                 ///< Count of flagged activities
        int severity_level;                              ///< Overall severity: 1-10
        std::string recommendation;                      ///< Action recommendation for supervisor
        std::vector<SuspiciousActivity> flagged_records; ///< Details of each flagged activity
    };

    /**
     * @file activity_tracker.h
     * @brief Anomaly and suspicious activity detection
     *
     * Analyzes audit log patterns to detect:
     *   1. Bulk changes from single session (possible batch data manipulation)
     *   2. Rapid sequential deletes (possible evidence tampering)
     *   3. After-hours operations (outside normal shift hours)
     *   4. Operations outside officer's jurisdiction (possible unauthorized access)
     *
     * Called by:
     *   - Dashboard on-demand queries
     *   - Scheduler hourly checks (compliance monitoring)
     *   - Incident investigation tools
     *
     * Thread Safety: All functions are read-only and thread-safe.
     *
     * Dependencies: audit_log.h, audit_query.h, utils/time_utils.h
     */

    class ActivityTracker
    {
    public:
        /**
         * Detects suspicious activity patterns at a station.
         *
         * Analysis rules:
         *
         * 1. BULK_CHANGE:
         *    - Single session makes 10+ changes in < 1 minute
         *    - Severity: 6 (modification batching is unusual)
         *
         * 2. RAPID_DELETE:
         *    - 5+ DELETE/soft-delete actions in < 5 minutes by one officer
         *    - Severity: 8 (evidence tampering indicator)
         *    - Higher severity if deletes are to EVIDENCE or WARRANTS tables
         *
         * 3. AFTER_HOURS:
         *    - Changes outside 06:00-22:00 UTC (shift hours)
         *    - Severity: 4 (unusual but may be valid for emergency cases)
         *    - Higher (6) if combined with case closure or evidence deletion
         *
         * 4. JURISDICTION_VIOLATION:
         *    - Officer modifies case/evidence outside their assigned station
         *    - Severity: 9 (direct policy violation)
         *    - Requires immediate review
         *
         * @param station_id The station to analyze
         * @param out_report Output structure populated with findings
         * @return ResultCode::OK on success (with or without findings)
         *         ResultCode::NOT_FOUND if station doesn't exist
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Processing:
         *       1. Query last 24 hours of audit_log for station's officers
         *       2. Scan for each suspicious pattern
         *       3. Calculate per-activity severity scores
         *       4. Aggregate and generate recommendation
         *
         * @example
         *   SuspiciousActivityReport report;
         *   auto result = ActivityTracker::detectSuspiciousActivity(station_id, report);
         *   if (result == ResultCode::OK && report.total_suspicious_activities > 0) {
         *       dashboard.alertSupervisor(report);
         *   }
         */
        static JusticeFlow::ResultCode detectSuspiciousActivity(int station_id,
                                                                SuspiciousActivityReport &out_report);
    };

} // namespace audit