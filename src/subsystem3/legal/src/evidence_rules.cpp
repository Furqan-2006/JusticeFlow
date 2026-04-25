#include "legal/include/evidence_rules.h"
#include "../../os_layer/ipc/include/ipc_manager.h"
#include "../../common/logger.h"
#include <sstream>

using namespace JusticeFlow;

namespace legal
{

    ResultCode EvidenceRules::enforceSoftDelete(int evidence_id)
    {
        // ALWAYS block hard DELETE at C++ layer
        Logger::debug("evidence_rules: Hard delete blocked by soft-delete enforcement");
        return ResultCode::INVALID_STATE;
    }

    bool EvidenceRules::isAdmissible(int evidence_id, ResultCode &out_code)
    {
        // Query: SELECT is_deleted, evidence_status FROM subsystem2.evidence WHERE evidence_id = ?
        std::stringstream query;
        query << "SELECT is_deleted, evidence_status FROM subsystem2.evidence WHERE evidence_id = "
              << evidence_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("evidence_rules: Database query failed");
            return false;
        }

        if (results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::debug("evidence_rules: Evidence not found");
            return false;
        }

        const std::string &is_deleted_str = results[0][0];
        const std::string &status_str = results[0][1];

        // Check is_deleted = false
        if (is_deleted_str == "true" || is_deleted_str == "1")
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::debug("evidence_rules: Evidence is soft-deleted");
            return false;
        }

        // Check status is admissible
        // Inadmissible: DISPOSED
        if (status_str == "DISPOSED")
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::debug("evidence_rules: Evidence in inadmissible state (DISPOSED)");
            return false;
        }

        // Admissible states: RECEIVED, SEALED, SENT_TO_LAB, RETURNED_FROM_LAB, PRODUCED_IN_COURT
        if (status_str == "RECEIVED" || status_str == "SEALED" || status_str == "SENT_TO_LAB" ||
            status_str == "RETURNED_FROM_LAB" || status_str == "PRODUCED_IN_COURT")
        {
            out_code = ResultCode::OK;
            Logger::info("evidence_rules: Evidence is admissible");
            return true;
        }

        // Unknown status — treat as inadmissible for safety
        out_code = ResultCode::INVALID_STATE;
        Logger::debug("evidence_rules: Evidence in unknown status");
        return false;
    }

    bool EvidenceRules::validateEvidenceOwnership(int evidence_id, int case_id, ResultCode &out_code)
    {
        // Step 1: Get evidence's case_id and is_deleted
        std::stringstream evidence_query;
        evidence_query << "SELECT case_id, is_deleted FROM subsystem2.evidence WHERE evidence_id = "
                       << evidence_id << ";";

        std::vector<std::vector<std::string>> evidence_results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(evidence_query.str(), evidence_results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("evidence_rules: Failed to query evidence");
            return false;
        }

        if (evidence_results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::debug("evidence_rules: Evidence not found");
            return false;
        }

        int evidence_case_id = std::stoi(evidence_results[0][0]);
        const std::string &is_deleted_str = evidence_results[0][1];

        // Check evidence is not deleted
        if (is_deleted_str == "true" || is_deleted_str == "1")
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::debug("evidence_rules: Evidence is soft-deleted");
            return false;
        }

        // Check ownership: evidence must belong to the case
        if (evidence_case_id != case_id)
        {
            out_code = ResultCode::INVALID_INPUT;
            Logger::debug("evidence_rules: Evidence ownership violation (cross-case usage)");
            return false;
        }

        // Step 2: Verify case exists
        std::stringstream case_query;
        case_query << "SELECT case_id FROM subsystem2.cases WHERE case_id = " << case_id << ";";

        std::vector<std::vector<std::string>> case_results;
        db_result = ipc::IpcManager::getInstance().executeQuery(case_query.str(), case_results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("evidence_rules: Failed to verify case");
            return false;
        }

        if (case_results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::debug("evidence_rules: Case not found");
            return false;
        }

        out_code = ResultCode::OK;
        Logger::info("evidence_rules: Evidence ownership validated");
        return true;
    }

} // namespace legal