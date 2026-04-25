#include "integration/include/audit_bridge.h"
#include "audit/include/audit_log.h"
#include "audit/include/audit_query.h"
#include "common/logger.h"
#include <sstream>

using namespace JusticeFlow;

namespace integration
{

    AuditBridge &AuditBridge::getInstance()
    {
        static AuditBridge instance;
        return instance;
    }

    ResultCode AuditBridge::log(const std::string &operation,
                                AuditedTable table,
                                int record_id,
                                const std::string &context)
    {
        // Convert table enum to string for logging
        std::string table_str;
        switch (table)
        {
        case AuditedTable::CASES:
            table_str = "CASES";
            break;
        case AuditedTable::EVIDENCE:
            table_str = "EVIDENCE";
            break;
        case AuditedTable::OFFICERS:
            table_str = "OFFICERS";
            break;
        case AuditedTable::ARRESTS:
            table_str = "ARRESTS";
            break;
        case AuditedTable::WARRANTS:
            table_str = "WARRANTS";
            break;
        case AuditedTable::CHARGE_SHEETS:
            table_str = "CHARGE_SHEETS";
            break;
        case AuditedTable::BAIL_RECORDS:
            table_str = "BAIL_RECORDS";
            break;
        case AuditedTable::ACCUSED:
            table_str = "ACCUSED";
            break;
        default:
            table_str = "UNKNOWN";
        }

        // Log the operation — the actual audit entry is created by the DB trigger
        std::stringstream msg;
        msg << "audit_bridge: Operation logged for " << table_str << " record_id=" << record_id;
        Logger::info(msg.str().c_str());

        // The operation will be executed by the caller, which fires the DB trigger
        // that writes the immutable audit entry. We don't write to audit.Audit_Log directly.
        return ResultCode::OK;
    }

    ResultCode AuditBridge::query(const AuditQueryParams &params,
                                  std::vector<audit::AuditRecord> &out_records)
    {
        switch (params.query_type)
        {
        case AuditQueryParams::QueryType::CASE_HISTORY:
            Logger::info("audit_bridge: Querying case history");
            return audit::AuditLog::getChangeHistory(params.case_id, out_records);

        case AuditQueryParams::QueryType::OFFICER_ACTIONS:
            Logger::info("audit_bridge: Querying officer actions");
            return audit::AuditLog::getOfficerActions(params.officer_id, params.from_time,
                                                      params.to_time, out_records);

        case AuditQueryParams::QueryType::TABLE_CHANGES:
            Logger::info("audit_bridge: Querying table changes");
            return audit::AuditLog::getTableChanges(params.table_name, params.record_id, out_records);

        case AuditQueryParams::QueryType::TIME_WINDOW:
            Logger::info("audit_bridge: Querying time window");
            return audit::AuditLog::queryByTimeWindow(params.from_time, params.to_time, out_records);

        default:
            Logger::error("audit_bridge: Unknown query type");
            return ResultCode::INVALID_INPUT;
        }
    }

} // namespace integration