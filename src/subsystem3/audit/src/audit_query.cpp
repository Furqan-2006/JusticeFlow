#include "audit/include/audit_query.h"
#include "utils/include/time_utils.h"
#include "common/logger.h"
#include <algorithm>

using namespace JusticeFlow;

namespace audit
{

    ResultCode AuditQuery::getFullCaseTimeline(int case_id, CaseTimeline &out_timeline)
    {
        // Initialize output
        out_timeline.case_id = case_id;
        out_timeline.total_changes = 0;
        out_timeline.first_change = 0;
        out_timeline.last_change = 0;

        // Query audit log for all changes related to case
        std::vector<AuditRecord> records;
        ResultCode result = AuditLog::getChangeHistory(case_id, records);

        if (result != ResultCode::OK)
        {
            Logger::debug("audit_query: No audit history for case");
            return result;
        }

        // Sort by timestamp (oldest first for timeline narrative)
        std::sort(records.begin(), records.end(),
                  [](const AuditRecord &a, const AuditRecord &b)
                  {
                      return a.changed_at < b.changed_at;
                  });

        out_timeline.timeline_entries = records;
        out_timeline.total_changes = static_cast<int>(records.size());

        if (!records.empty())
        {
            out_timeline.first_change = records.front().changed_at;
            out_timeline.last_change = records.back().changed_at;
        }

        Logger::info("audit_query: Generated full case timeline");
        return ResultCode::OK;
    }

    ResultCode AuditQuery::getStationActivity(int station_id, time_t from, time_t to,
                                              StationActivitySummary &out_summary)
    {
        // Initialize output
        out_summary.station_id = station_id;
        out_summary.period_from = from;
        out_summary.period_to = to;
        out_summary.total_officers_active = 0;
        out_summary.total_actions = 0;
        out_summary.inserts = 0;
        out_summary.updates = 0;
        out_summary.deletes = 0;

        // Query all officers at station
        std::stringstream query;
        query << "SELECT officer_id FROM subsystem1.officers WHERE station_id = " << station_id << ";";

        std::vector<std::vector<std::string>> officer_results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), officer_results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("audit_query: Failed to query officers at station");
            return db_result;
        }

        if (officer_results.empty())
        {
            Logger::debug("audit_query: No officers found at station");
            return ResultCode::NOT_FOUND;
        }

        // For each officer, get their actions in time window
        std::vector<int> active_officers;
        std::vector<AuditRecord> all_records;

        for (const auto &row : officer_results)
        {
            int officer_id = std::stoi(row[0]);
            std::vector<AuditRecord> officer_actions;

            ResultCode officer_result = AuditLog::getOfficerActions(officer_id, from, to, officer_actions);

            if (officer_result == ResultCode::OK)
            {
                active_officers.push_back(officer_id);
                all_records.insert(all_records.end(), officer_actions.begin(), officer_actions.end());
            }
        }

        if (all_records.empty())
        {
            Logger::debug("audit_query: No activity at station in time window");
            return ResultCode::NOT_FOUND;
        }

        // Aggregate statistics
        out_summary.total_officers_active = static_cast<int>(active_officers.size());
        out_summary.total_actions = static_cast<int>(all_records.size());

        for (const auto &record : all_records)
        {
            if (record.action == AuditAction::INSERT)
                out_summary.inserts++;
            else if (record.action == AuditAction::UPDATE)
                out_summary.updates++;
            else if (record.action == AuditAction::DELETE)
                out_summary.deletes++;
        }

        // Sort records by timestamp (newest first for display)
        std::sort(all_records.begin(), all_records.end(),
                  [](const AuditRecord &a, const AuditRecord &b)
                  {
                      return a.changed_at > b.changed_at;
                  });

        out_summary.activity_records = all_records;

        Logger::info("audit_query: Generated station activity summary");
        return ResultCode::OK;
    }

} // namespace audit