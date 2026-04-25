#include "legal/include/case_validation.h"
#include "utils/include/time_utils.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"
#include <sstream>
#include <cstring>

using namespace JusticeFlow;

namespace legal
{

    bool CaseValidation::caseExistsAndOpen(int case_id, ResultCode &out_code)
    {
        // Query: SELECT case_status FROM subsystem2.cases WHERE case_id = ?
        std::stringstream query;
        query << "SELECT case_status FROM subsystem2.cases WHERE case_id = " << case_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("case_validation: Database query failed for case_id");
            return false;
        }

        if (results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::debug("case_validation: Case not found");
            return false;
        }

        // Parse case_status from result
        const std::string &status_str = results[0][0];

        // Valid states for warrant operations: REGISTERED, UNDER_INVESTIGATION
        if (status_str == "REGISTERED" || status_str == "UNDER_INVESTIGATION")
        {
            out_code = ResultCode::OK;
            Logger::info("case_validation: Case is open and valid for warrant");
            return true;
        }

        // Invalid states (case exists but in wrong state)
        out_code = ResultCode::INVALID_STATE;
        Logger::debug("case_validation: Case exists but in invalid state for warrant");
        return false;
    }

    bool CaseValidation::officerBelongsToStation(int officer_id, int case_id, ResultCode &out_code)
    {
        // Step 1: Get case's station_id
        std::stringstream case_query;
        case_query << "SELECT station_id FROM subsystem2.cases WHERE case_id = " << case_id << ";";

        std::vector<std::vector<std::string>> case_results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(case_query.str(), case_results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("case_validation: Failed to query case");
            return false;
        }

        if (case_results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::debug("case_validation: Case not found");
            return false;
        }

        int case_station_id = std::stoi(case_results[0][0]);

        // Step 2: Get officer's station_id, currentRank, and status
        std::stringstream officer_query;
        officer_query << "SELECT station_id, currentRank, status FROM subsystem1.officers WHERE officer_id = "
                      << officer_id << ";";

        std::vector<std::vector<std::string>> officer_results;
        db_result = ipc::IpcManager::getInstance().executeQuery(officer_query.str(), officer_results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("case_validation: Failed to query officer");
            return false;
        }

        if (officer_results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::debug("case_validation: Officer not found");
            return false;
        }

        int officer_station_id = std::stoi(officer_results[0][0]);
        const std::string &rank_str = officer_results[0][1];
        const std::string &status_str = officer_results[0][2];

        // Check officer is ACTIVE
        if (status_str != "ACTIVE")
        {
            out_code = ResultCode::JURISDICTION_DENIED;
            Logger::debug("case_validation: Officer not in ACTIVE status");
            return false;
        }

        // Check jurisdiction: same station
        if (officer_station_id == case_station_id)
        {
            out_code = ResultCode::OK;
            Logger::info("case_validation: Officer has jurisdiction (same station)");
            return true;
        }

        // Check if officer is DSP+ rank (higher ranks have zone jurisdiction)
        // Rank hierarchy: DSP(5), SP(6), SSP(7), DIG(8), ADDL_IG(9), IGP(10)
        if (rank_str == "DSP" || rank_str == "SP" || rank_str == "SSP" || rank_str == "DIG" ||
            rank_str == "ADDL_IG" || rank_str == "IGP")
        {
            // HQ officers: zone-wide jurisdiction (simplified — always grant)
            out_code = ResultCode::OK;
            Logger::info("case_validation: Officer has zone jurisdiction (DSP+ rank)");
            return true;
        }

        // No jurisdiction match
        out_code = ResultCode::JURISDICTION_DENIED;
        Logger::debug("case_validation: Officer lacks jurisdiction");
        return false;
    }

    bool CaseValidation::validateCaseForWarrant(int case_id, int officer_id, ResultCode &out_code)
    {
        // Step 1: Check case is open
        if (!caseExistsAndOpen(case_id, out_code))
        {
            return false;
        }

        // Step 2: Check officer jurisdiction
        if (!officerBelongsToStation(officer_id, case_id, out_code))
        {
            return false;
        }

        out_code = ResultCode::OK;
        Logger::info("case_validation: Full warrant validation passed");
        return true;
    }

} // namespace legal