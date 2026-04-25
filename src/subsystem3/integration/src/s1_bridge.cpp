#include "integration/include/s1_bridge.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"
#include <sstream>

using namespace JusticeFlow;

namespace integration
{

    ResultCode S1Bridge::getOfficerRecord(int officer_id, Officer &out_officer)
    {
        // Query: SELECT * FROM subsystem1.officers WHERE officer_id = ?
        std::stringstream query;
        query << "SELECT officer_id, beltNumber, cnic, qualification, joiningDate, joiningRank, "
              << "currentRank, retirementDate, bpsScale, station_id, status "
              << "FROM subsystem1.officers WHERE officer_id = " << officer_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("s1_bridge: Database query failed for officer record");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("s1_bridge: Officer not found");
            return ResultCode::NOT_FOUND;
        }

        const auto &row = results[0];
        out_officer.officerId = std::stoi(row[0]);
        out_officer.beltNumber = row[1];
        out_officer.cnic = row[2];
        out_officer.qualification = row[3];
        out_officer.joiningDate = row[4];

        // Parse joiningRank enum
        const std::string &join_rank_str = row[5];
        if (join_rank_str == "CONSTABLE")
            out_officer.joiningRank = OfficerRank::CONSTABLE;
        else if (join_rank_str == "HEAD_CONSTABLE")
            out_officer.joiningRank = OfficerRank::HEAD_CONSTABLE;
        else if (join_rank_str == "ASI")
            out_officer.joiningRank = OfficerRank::ASI;
        else if (join_rank_str == "SI")
            out_officer.joiningRank = OfficerRank::SI;
        else if (join_rank_str == "INSPECTOR")
            out_officer.joiningRank = OfficerRank::INSPECTOR;
        else if (join_rank_str == "DSP")
            out_officer.joiningRank = OfficerRank::DSP;
        else if (join_rank_str == "SP")
            out_officer.joiningRank = OfficerRank::SP;
        else if (join_rank_str == "SSP")
            out_officer.joiningRank = OfficerRank::SSP;
        else if (join_rank_str == "DIG")
            out_officer.joiningRank = OfficerRank::DIG;
        else if (join_rank_str == "ADDL_IG")
            out_officer.joiningRank = OfficerRank::ADDL_IG;
        else if (join_rank_str == "IGP")
            out_officer.joiningRank = OfficerRank::IGP;

        // Parse currentRank enum
        const std::string &curr_rank_str = row[6];
        if (curr_rank_str == "CONSTABLE")
            out_officer.currentRank = OfficerRank::CONSTABLE;
        else if (curr_rank_str == "HEAD_CONSTABLE")
            out_officer.currentRank = OfficerRank::HEAD_CONSTABLE;
        else if (curr_rank_str == "ASI")
            out_officer.currentRank = OfficerRank::ASI;
        else if (curr_rank_str == "SI")
            out_officer.currentRank = OfficerRank::SI;
        else if (curr_rank_str == "INSPECTOR")
            out_officer.currentRank = OfficerRank::INSPECTOR;
        else if (curr_rank_str == "DSP")
            out_officer.currentRank = OfficerRank::DSP;
        else if (curr_rank_str == "SP")
            out_officer.currentRank = OfficerRank::SP;
        else if (curr_rank_str == "SSP")
            out_officer.currentRank = OfficerRank::SSP;
        else if (curr_rank_str == "DIG")
            out_officer.currentRank = OfficerRank::DIG;
        else if (curr_rank_str == "ADDL_IG")
            out_officer.currentRank = OfficerRank::ADDL_IG;
        else if (curr_rank_str == "IGP")
            out_officer.currentRank = OfficerRank::IGP;

        out_officer.retirementDate = row[7];
        out_officer.bpsScale = std::stoi(row[8]);
        out_officer.stationId = std::stoi(row[9]);

        // Parse status enum
        const std::string &status_str = row[10];
        if (status_str == "ACTIVE")
            out_officer.status = OfficerStatus::ACTIVE;
        else if (status_str == "SUSPENDED")
            out_officer.status = OfficerStatus::SUSPENDED;
        else if (status_str == "ON_LEAVE")
            out_officer.status = OfficerStatus::ON_LEAVE;
        else if (status_str == "RETIRED")
            out_officer.status = OfficerStatus::RETIRED;
        else if (status_str == "TERMINATED")
            out_officer.status = OfficerStatus::TERMINATED;

        Logger::info("s1_bridge: Retrieved officer record");
        return ResultCode::OK;
    }

    ResultCode S1Bridge::getOfficerDutyStatus(int officer_id, bool &out_active)
    {
        // Query: SELECT status FROM subsystem1.officers WHERE officer_id = ?
        std::stringstream query;
        query << "SELECT status FROM subsystem1.officers WHERE officer_id = " << officer_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("s1_bridge: Database query failed for officer duty status");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("s1_bridge: Officer not found");
            return ResultCode::NOT_FOUND;
        }

        const std::string &status_str = results[0][0];
        out_active = (status_str == "ACTIVE");

        Logger::info("s1_bridge: Retrieved officer duty status");
        return ResultCode::OK;
    }

    ResultCode S1Bridge::notifyOfficerCaseAssignment(int officer_id, int case_id)
    {
        // Update: active_case_count += 1 for the officer
        std::stringstream update_query;
        update_query << "UPDATE subsystem1.officers SET active_case_count = active_case_count + 1 "
                     << "WHERE officer_id = " << officer_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("s1_bridge: Failed to notify officer case assignment");
            return db_result;
        }

        Logger::info("s1_bridge: Notified officer of case assignment");
        return ResultCode::OK;
    }

} // namespace integration