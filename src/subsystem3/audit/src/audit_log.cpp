#include "audit/include/audit_log.h"
#include "utils/include/time_utils.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"
#include <sstream>

using namespace JusticeFlow;

namespace audit
{

    ResultCode AuditLog::getChangeHistory(int case_id, std::vector<AuditRecord> &out_records)
    {
        // Query: SELECT * FROM audit.Audit_Log WHERE record_id = case_id OR linked to case_id
        std::stringstream query;
        query << "SELECT audit_id, table_name, record_id, action, old_value, new_value, "
              << "changed_by_user, changed_by_officer_id, changed_by_belt, client_process_id, "
              << "client_ip, changed_at FROM audit.Audit_Log "
              << "WHERE record_id = " << case_id << " "
              << "OR (table_name = 'EVIDENCE' AND record_id IN "
              << "  (SELECT evidence_id FROM subsystem2.evidence WHERE case_id = " << case_id << ")) "
              << "OR (table_name = 'WARRANTS' AND record_id IN "
              << "  (SELECT warrant_id FROM subsystem3.warrants WHERE case_id = " << case_id << ")) "
              << "OR (table_name = 'ARRESTS' AND record_id IN "
              << "  (SELECT arrest_id FROM subsystem3.arrests WHERE case_id = " << case_id << ")) "
              << "ORDER BY changed_at DESC;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("audit_log: Database query failed for case history");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("audit_log: No audit history found for case");
            return ResultCode::NOT_FOUND;
        }

        // Parse results into AuditRecord structs
        for (const auto &row : results)
        {
            AuditRecord record;
            record.audit_id = std::stoi(row[0]);

            // Parse AuditedTable enum
            const std::string &table_str = row[1];
            if (table_str == "CASES")
                record.table_name = AuditedTable::CASES;
            else if (table_str == "EVIDENCE")
                record.table_name = AuditedTable::EVIDENCE;
            else if (table_str == "OFFICERS")
                record.table_name = AuditedTable::OFFICERS;
            else if (table_str == "ARRESTS")
                record.table_name = AuditedTable::ARRESTS;
            else if (table_str == "WARRANTS")
                record.table_name = AuditedTable::WARRANTS;
            else if (table_str == "CHARGE_SHEETS")
                record.table_name = AuditedTable::CHARGE_SHEETS;
            else if (table_str == "BAIL_RECORDS")
                record.table_name = AuditedTable::BAIL_RECORDS;
            else if (table_str == "ACCUSED")
                record.table_name = AuditedTable::ACCUSED;

            record.record_id = std::stoi(row[2]);

            // Parse AuditAction enum
            const std::string &action_str = row[3];
            if (action_str == "INSERT")
                record.action = AuditAction::INSERT;
            else if (action_str == "UPDATE")
                record.action = AuditAction::UPDATE;
            else if (action_str == "DELETE")
                record.action = AuditAction::DELETE;

            record.old_value = row[4];
            record.new_value = row[5];
            record.changed_by_user = row[6];
            record.changed_by_officer_id = std::stoi(row[7]);
            record.changed_by_belt = row[8];
            record.client_process_id = std::stoi(row[9]);
            record.client_ip = row[10];
            record.changed_at = std::stol(row[11]);

            out_records.push_back(record);
        }

        Logger::info("audit_log: Retrieved case history");
        return ResultCode::OK;
    }

    ResultCode AuditLog::getOfficerActions(int officer_id, time_t from, time_t to,
                                           std::vector<AuditRecord> &out_records)
    {
        // Query: SELECT * FROM audit.Audit_Log WHERE changed_by_officer_id = officer_id AND time window
        std::stringstream query;
        query << "SELECT audit_id, table_name, record_id, action, old_value, new_value, "
              << "changed_by_user, changed_by_officer_id, changed_by_belt, client_process_id, "
              << "client_ip, changed_at FROM audit.Audit_Log "
              << "WHERE changed_by_officer_id = " << officer_id << " "
              << "AND changed_at >= " << from << " AND changed_at <= " << to << " "
              << "ORDER BY changed_at DESC;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("audit_log: Database query failed for officer actions");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("audit_log: No actions found for officer in time window");
            return ResultCode::NOT_FOUND;
        }

        // Parse results into AuditRecord structs
        for (const auto &row : results)
        {
            AuditRecord record;
            record.audit_id = std::stoi(row[0]);

            const std::string &table_str = row[1];
            if (table_str == "CASES")
                record.table_name = AuditedTable::CASES;
            else if (table_str == "EVIDENCE")
                record.table_name = AuditedTable::EVIDENCE;
            else if (table_str == "OFFICERS")
                record.table_name = AuditedTable::OFFICERS;
            else if (table_str == "ARRESTS")
                record.table_name = AuditedTable::ARRESTS;
            else if (table_str == "WARRANTS")
                record.table_name = AuditedTable::WARRANTS;
            else if (table_str == "CHARGE_SHEETS")
                record.table_name = AuditedTable::CHARGE_SHEETS;
            else if (table_str == "BAIL_RECORDS")
                record.table_name = AuditedTable::BAIL_RECORDS;
            else if (table_str == "ACCUSED")
                record.table_name = AuditedTable::ACCUSED;

            record.record_id = std::stoi(row[2]);

            const std::string &action_str = row[3];
            if (action_str == "INSERT")
                record.action = AuditAction::INSERT;
            else if (action_str == "UPDATE")
                record.action = AuditAction::UPDATE;
            else if (action_str == "DELETE")
                record.action = AuditAction::DELETE;

            record.old_value = row[4];
            record.new_value = row[5];
            record.changed_by_user = row[6];
            record.changed_by_officer_id = std::stoi(row[7]);
            record.changed_by_belt = row[8];
            record.client_process_id = std::stoi(row[9]);
            record.client_ip = row[10];
            record.changed_at = std::stol(row[11]);

            out_records.push_back(record);
        }

        Logger::info("audit_log: Retrieved officer actions");
        return ResultCode::OK;
    }

    ResultCode AuditLog::getTableChanges(AuditedTable table_name, int record_id,
                                         std::vector<AuditRecord> &out_records)
    {
        // Convert AuditedTable enum to string
        std::string table_str;
        switch (table_name)
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

        // Query: SELECT * FROM audit.Audit_Log WHERE table_name = ? AND record_id = ?
        std::stringstream query;
        query << "SELECT audit_id, table_name, record_id, action, old_value, new_value, "
              << "changed_by_user, changed_by_officer_id, changed_by_belt, client_process_id, "
              << "client_ip, changed_at FROM audit.Audit_Log "
              << "WHERE table_name = '" << table_str << "' AND record_id = " << record_id << " "
              << "ORDER BY changed_at ASC;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("audit_log: Database query failed for table changes");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("audit_log: No changes found for record");
            return ResultCode::NOT_FOUND;
        }

        // Parse results into AuditRecord structs
        for (const auto &row : results)
        {
            AuditRecord record;
            record.audit_id = std::stoi(row[0]);
            record.table_name = table_name;
            record.record_id = std::stoi(row[2]);

            const std::string &action_str = row[3];
            if (action_str == "INSERT")
                record.action = AuditAction::INSERT;
            else if (action_str == "UPDATE")
                record.action = AuditAction::UPDATE;
            else if (action_str == "DELETE")
                record.action = AuditAction::DELETE;

            record.old_value = row[4];
            record.new_value = row[5];
            record.changed_by_user = row[6];
            record.changed_by_officer_id = std::stoi(row[7]);
            record.changed_by_belt = row[8];
            record.client_process_id = std::stoi(row[9]);
            record.client_ip = row[10];
            record.changed_at = std::stol(row[11]);

            out_records.push_back(record);
        }

        Logger::info("audit_log: Retrieved table changes");
        return ResultCode::OK;
    }

    ResultCode AuditLog::queryByTimeWindow(time_t from, time_t to,
                                           std::vector<AuditRecord> &out_records)
    {
        // Query: SELECT * FROM audit.Audit_Log WHERE changed_at >= from AND changed_at <= to
        std::stringstream query;
        query << "SELECT audit_id, table_name, record_id, action, old_value, new_value, "
              << "changed_by_user, changed_by_officer_id, changed_by_belt, client_process_id, "
              << "client_ip, changed_at FROM audit.Audit_Log "
              << "WHERE changed_at >= " << from << " AND changed_at <= " << to << " "
              << "ORDER BY changed_at DESC;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("audit_log: Database query failed for time window");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("audit_log: No changes in time window");
            return ResultCode::NOT_FOUND;
        }

        // Parse results into AuditRecord structs
        for (const auto &row : results)
        {
            AuditRecord record;
            record.audit_id = std::stoi(row[0]);

            const std::string &table_str = row[1];
            if (table_str == "CASES")
                record.table_name = AuditedTable::CASES;
            else if (table_str == "EVIDENCE")
                record.table_name = AuditedTable::EVIDENCE;
            else if (table_str == "OFFICERS")
                record.table_name = AuditedTable::OFFICERS;
            else if (table_str == "ARRESTS")
                record.table_name = AuditedTable::ARRESTS;
            else if (table_str == "WARRANTS")
                record.table_name = AuditedTable::WARRANTS;
            else if (table_str == "CHARGE_SHEETS")
                record.table_name = AuditedTable::CHARGE_SHEETS;
            else if (table_str == "BAIL_RECORDS")
                record.table_name = AuditedTable::BAIL_RECORDS;
            else if (table_str == "ACCUSED")
                record.table_name = AuditedTable::ACCUSED;

            record.record_id = std::stoi(row[2]);

            const std::string &action_str = row[3];
            if (action_str == "INSERT")
                record.action = AuditAction::INSERT;
            else if (action_str == "UPDATE")
                record.action = AuditAction::UPDATE;
            else if (action_str == "DELETE")
                record.action = AuditAction::DELETE;

            record.old_value = row[4];
            record.new_value = row[5];
            record.changed_by_user = row[6];
            record.changed_by_officer_id = std::stoi(row[7]);
            record.changed_by_belt = row[8];
            record.client_process_id = std::stoi(row[9]);
            record.client_ip = row[10];
            record.changed_at = std::stol(row[11]);

            out_records.push_back(record);
        }

        Logger::info("audit_log: Retrieved time window query");
        return ResultCode::OK;
    }

} // namespace audit