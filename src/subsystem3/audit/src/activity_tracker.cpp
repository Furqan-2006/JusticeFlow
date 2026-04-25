#include "audit/include/activity_tracker.h"
#include "audit/include/audit_query.h"
#include "utils/include/time_utils.h"
#include "common/logger.h"
#include <algorithm>
#include <cmath>

using namespace JusticeFlow;

namespace audit
{

    ResultCode ActivityTracker::detectSuspiciousActivity(int station_id,
                                                         SuspiciousActivityReport &out_report)
    {
        // Initialize report
        out_report.station_id = station_id;
        out_report.generated_at = std::time(nullptr);
        out_report.total_suspicious_activities = 0;
        out_report.severity_level = 0;
        out_report.recommendation = "No suspicious activity detected";

        // Query station activity for last 24 hours
        time_t now = std::time(nullptr);
        time_t from_time = now - (24 * 3600); // 24 hours ago

        StationActivitySummary activity;
        ResultCode result = AuditQuery::getStationActivity(station_id, from_time, now, activity);

        if (result != ResultCode::OK)
        {
            Logger::debug("activity_tracker: No activity at station in 24-hour window");
            return ResultCode::OK; // OK with no findings
        }

        std::vector<SuspiciousActivity> suspicious_list;
        int total_severity = 0;

        // Analysis Rule 1: BULK_CHANGE (10+ changes in < 1 minute per session)
        std::map<int, std::vector<AuditRecord>> records_by_session;
        for (const auto &record : activity.activity_records)
        {
            records_by_session[record.client_process_id].push_back(record);
        }

        for (const auto &[session_id, session_records] : records_by_session)
        {
            if (session_records.size() >= 10)
            {
                time_t earliest = session_records.front().changed_at;
                time_t latest = session_records.back().changed_at;

                if ((latest - earliest) < 60)
                { // 1 minute
                    SuspiciousActivity activity_item;
                    activity_item.activity_type = "BULK_CHANGE";
                    activity_item.officer_id = session_records.front().changed_by_officer_id;
                    activity_item.officer_name = session_records.front().changed_by_user;
                    activity_item.session_id = session_id;
                    activity_item.occurred_at = latest;
                    activity_item.severity_score = 6;
                    activity_item.description = "Bulk changes detected: " +
                                                std::to_string(session_records.size()) +
                                                " records modified in under 1 minute";

                    suspicious_list.push_back(activity_item);
                    total_severity += 6;

                    Logger::debug("activity_tracker: Bulk change detected");
                }
            }
        }

        // Analysis Rule 2: RAPID_DELETE (5+ deletes in < 5 minutes by one officer)
        std::map<int, std::vector<AuditRecord>> delete_records_by_officer;
        for (const auto &record : activity.activity_records)
        {
            if (record.action == AuditAction::DELETE)
            {
                delete_records_by_officer[record.changed_by_officer_id].push_back(record);
            }
        }

        for (const auto &[officer_id, delete_records] : delete_records_by_officer)
        {
            if (delete_records.size() >= 5)
            {
                time_t earliest = delete_records.front().changed_at;
                time_t latest = delete_records.back().changed_at;

                if ((latest - earliest) < 300)
                { // 5 minutes
                    int severity = 8;
                    // Higher severity if deletes are to EVIDENCE or WARRANTS
                    for (const auto &rec : delete_records)
                    {
                        if (rec.table_name == AuditedTable::EVIDENCE ||
                            rec.table_name == AuditedTable::WARRANTS)
                        {
                            severity = 10;
                            break;
                        }
                    }

                    SuspiciousActivity activity_item;
                    activity_item.activity_type = "RAPID_DELETE";
                    activity_item.officer_id = officer_id;
                    activity_item.officer_name = delete_records.front().changed_by_user;
                    activity_item.session_id = 0;
                    activity_item.occurred_at = latest;
                    activity_item.severity_score = severity;
                    activity_item.description = "Rapid delete pattern: " +
                                                std::to_string(delete_records.size()) +
                                                " records deleted in under 5 minutes";

                    suspicious_list.push_back(activity_item);
                    total_severity += severity;

                    Logger::debug("activity_tracker: Rapid delete pattern detected");
                }
            }
        }

        // Analysis Rule 3: AFTER_HOURS (06:00-22:00 UTC is normal)
        for (const auto &record : activity.activity_records)
        {
            struct tm tm_info;
            gmtime_r(&record.changed_at, &tm_info);
            int hour = tm_info.tm_hour;

            if (hour < 6 || hour >= 22)
            {
                int severity = 4;

                // Higher severity if modifying case closure or evidence
                if (record.action == AuditAction::DELETE ||
                    (record.action == AuditAction::UPDATE && record.table_name == AuditedTable::CASES))
                {
                    severity = 6;
                }

                SuspiciousActivity activity_item;
                activity_item.activity_type = "AFTER_HOURS";
                activity_item.officer_id = record.changed_by_officer_id;
                activity_item.officer_name = record.changed_by_user;
                activity_item.session_id = record.client_process_id;
                activity_item.occurred_at = record.changed_at;
                activity_item.table_name = record.table_name;
                activity_item.record_id = record.record_id;
                activity_item.severity_score = severity;
                activity_item.description = "After-hours operation at " + std::to_string(hour) + ":00 UTC";

                suspicious_list.push_back(activity_item);
                total_severity += severity;

                Logger::debug("activity_tracker: After-hours operation detected");
            }
        }

        // Aggregate findings
        out_report.flagged_records = suspicious_list;
        out_report.total_suspicious_activities = static_cast<int>(suspicious_list.size());
        out_report.severity_level = std::min(10, total_severity / std::max(1, (int)suspicious_list.size()));

        if (out_report.total_suspicious_activities > 0)
        {
            if (out_report.severity_level >= 8)
            {
                out_report.recommendation = "IMMEDIATE REVIEW REQUIRED - Escalate to DSP";
            }
            else if (out_report.severity_level >= 6)
            {
                out_report.recommendation = "Review by SI+ supervisor recommended";
            }
            else
            {
                out_report.recommendation = "Log and monitor for patterns";
            }

            Logger::info("activity_tracker: Suspicious activity detected and reported");
        }

        return ResultCode::OK;
    }

} // namespace audit