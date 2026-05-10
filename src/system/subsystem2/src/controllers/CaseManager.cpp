#include "../../include/controllers/CaseManager.h"
#include "../../../common/logger.h"
#include "../../../os_layer/ipc/include/ipc_manager.h"

#include <string>
#include <vector>

namespace subsystem2
{

    static ipc::IpcManager &getDB()
    {
        return ipc::IpcManager::getInstance();
    }

    static void parseCaseRow(const std::vector<std::string> &row, CaseDTO &data)
    {
        int col = 0;
        data.case_id = std::stoll(row[col++]);
        data.fir_number = row[col++];

        std::string case_type_str = row[col++];
        if (case_type_str == "MURDER")
            data.case_type = JusticeFlow::CaseType::MURDER;
        else if (case_type_str == "THEFT")
            data.case_type = JusticeFlow::CaseType::THEFT;
        else
            data.case_type = JusticeFlow::CaseType::PUBLIC_DISTURBANCE;

        std::string case_status_str = row[col++];
        if (case_status_str == "REGISTERED")
            data.case_status = JusticeFlow::CaseStatus::REGISTERED;
        else if (case_status_str == "UNDER_INVESTIGATION")
            data.case_status = JusticeFlow::CaseStatus::UNDER_INVESTIGATION;
        else if (case_status_str == "CLOSED")
            data.case_status = JusticeFlow::CaseStatus::CLOSED;
        else
            data.case_status = JusticeFlow::CaseStatus::REGISTERED;

        data.incident_date = std::stoll(row[col++]);
        data.incident_address = row[col++];
        data.incident_description = row[col++];
        data.incident_lat = std::stod(row[col++]);
        data.incident_lon = std::stod(row[col++]);
        data.station_id = std::stoll(row[col++]);
        data.primary_complainant_cnic = row[col++];
        data.filed_by = std::stoll(row[col++]);
        data.filed_at = std::stoll(row[col++]);
        data.updated_at = std::stoll(row[col++]);
        data.lead_officer_id = row[col] != "0" ? std::stoll(row[col]) : 0;
        col++;
        data.parent_case_id = row[col] != "0" ? std::stoll(row[col]) : 0;
        col++;
        data.closed_at = row[col] != "" ? std::stoll(row[col]) : 0;
        col++;
        data.closure_reason = row[col++];

        std::string approval_str = row[col++];
        data.approval_status = (approval_str == "APPROVED") ? JusticeFlow::ApprovalStatus::APPROVED
                                                            : JusticeFlow::ApprovalStatus::NOT_REQUIRED;

        data.approved_by = row[col] != "0" ? std::stoll(row[col]) : 0;
        col++;
        data.approved_at = row[col] != "" ? std::stoll(row[col]) : 0;
        col++;
        data.reopened_by = row[col] != "0" ? std::stoll(row[col]) : 0;
        col++;
        data.reopened_at = row[col] != "" ? std::stoll(row[col]) : 0;
        col++;
        data.reopen_reason = row[col++];
    }

    JusticeFlow::ResultCode CaseManager::registerFIR(const FIRRegistrationRequest &request,
                                                     const JusticeFlow::SessionContext &session,
                                                     Case *&out_case)
    {
        if (!session.isValid)
        {
            Logger::error("[S2][CaseManager] Unauthorized FIR attempt.");
            return JusticeFlow::ResultCode::AUTH_FAILED;
        }

        if (session.rank != JusticeFlow::OfficerRank::ASI &&
            session.rank != JusticeFlow::OfficerRank::SI &&
            session.rank != JusticeFlow::OfficerRank::INSPECTOR)
        {
            Logger::error("[S2][CaseManager] Rank insufficient to register FIR.");
            return JusticeFlow::ResultCode::RANK_INSUFFICIENT;
        }

        std::string case_type_str;
        if (request.type == JusticeFlow::CaseType::MURDER)
            case_type_str = "MURDER";
        else if (request.type == JusticeFlow::CaseType::THEFT)
            case_type_str = "THEFT";
        else
            case_type_str = "OTHER";

        std::string sql = "INSERT INTO public.cases (case_type, case_status, incident_date, incident_address, "
                          "incident_description, incident_lat, incident_lon, station_id, "
                          "primary_complainant_cnic, filed_by) VALUES ("
                          "'" +
                          case_type_str + "', "
                                          "'REGISTERED', "
                                          "to_timestamp(" +
                          std::to_string(request.incident_date) + "), "
                                                                  "'" +
                          request.incident_address + "', "
                                                     "'" +
                          request.description + "', " + std::to_string(request.lat) + ", " + std::to_string(request.lon) + ", " + std::to_string(request.station_id) + ", "
                                                                                                                                                                       "'" +
                          request.complainant_cnic + "', " + std::to_string(request.filed_by_officer_id) + ") RETURNING case_id, fir_number;";

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

        if (db_res != JusticeFlow::ResultCode::OK || results.empty())
        {
            Logger::error("[S2][CaseManager] Database insert failed for FIR.");
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        CaseDTO new_data;
        new_data.case_id = std::stoll(results[0][0]);
        new_data.fir_number = results[0][1];
        new_data.case_type = request.type;
        new_data.case_status = JusticeFlow::CaseStatus::REGISTERED;
        new_data.incident_address = request.incident_address;
        new_data.station_id = request.station_id;
        new_data.filed_by = request.filed_by_officer_id;

        out_case = new Case(new_data);

        Logger::info(("[S2][CaseManager] Successfully registered FIR: " + new_data.fir_number).c_str());
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode CaseManager::fetchCase(int64_t case_id, Case *&out_case)
    {
        std::string sql =
            "SELECT case_id, fir_number, case_type, case_status, incident_date, "
            "       incident_address, incident_description, incident_lat, incident_lon, "
            "       station_id, primary_complainant_cnic, filed_by, filed_at, updated_at, "
            "       lead_officer_id, parent_case_id, closed_at, closure_reason, "
            "       approval_status, approved_by, approved_at, reopened_by, "
            "       reopened_at, reopen_reason "
            "FROM public.cases WHERE case_id = " +
            std::to_string(case_id) + ";";

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

        if (db_res != JusticeFlow::ResultCode::OK || results.empty())
        {
            Logger::error(("[S2][CaseManager] Case not found: " + std::to_string(case_id)).c_str());
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        CaseDTO data;
        parseCaseRow(results[0], data);

        out_case = new Case(data);
        Logger::info(("[S2][CaseManager] Successfully fetched case: " + data.fir_number).c_str());
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode CaseManager::getCasesByStation(int64_t station_id, std::vector<Case *> &out_cases)
    {
        std::string sql =
            "SELECT case_id, fir_number, case_type, case_status, incident_date, "
            "       incident_address, incident_description, incident_lat, incident_lon, "
            "       station_id, primary_complainant_cnic, filed_by, filed_at, updated_at, "
            "       lead_officer_id, parent_case_id, closed_at, closure_reason, "
            "       approval_status, approved_by, approved_at, reopened_by, "
            "       reopened_at, reopen_reason "
            "FROM public.cases WHERE station_id = " +
            std::to_string(station_id) +
            " ORDER BY filed_at DESC;";

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

        if (db_res != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[S2][CaseManager] Database error fetching cases by station.");
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        if (results.empty())
        {
            Logger::info("[S2][CaseManager] No cases found for station.");
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        for (const auto &row : results)
        {
            CaseDTO data;
            parseCaseRow(row, data);
            out_cases.push_back(new Case(data));
        }

        Logger::info(("[S2][CaseManager] Retrieved " + std::to_string(out_cases.size()) + " cases for station.").c_str());
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode CaseManager::getCasesByStatus(JusticeFlow::CaseStatus status, std::vector<Case *> &out_cases)
    {
        std::string status_str;
        if (status == JusticeFlow::CaseStatus::REGISTERED)
            status_str = "REGISTERED";
        else if (status == JusticeFlow::CaseStatus::UNDER_INVESTIGATION)
            status_str = "UNDER_INVESTIGATION";
        else if (status == JusticeFlow::CaseStatus::CLOSED)
            status_str = "CLOSED";
        else
            status_str = "REGISTERED";

        std::string sql =
            "SELECT case_id, fir_number, case_type, case_status, incident_date, "
            "       incident_address, incident_description, incident_lat, incident_lon, "
            "       station_id, primary_complainant_cnic, filed_by, filed_at, updated_at, "
            "       lead_officer_id, parent_case_id, closed_at, closure_reason, "
            "       approval_status, approved_by, approved_at, reopened_by, "
            "       reopened_at, reopen_reason "
            "FROM public.cases WHERE case_status = '" +
            status_str + "' ORDER BY filed_at DESC;";

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

        if (db_res != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[S2][CaseManager] Database error fetching cases by status.");
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        if (results.empty())
        {
            Logger::info("[S2][CaseManager] No cases found with given status.");
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        for (const auto &row : results)
        {
            CaseDTO data;
            parseCaseRow(row, data);
            out_cases.push_back(new Case(data));
        }

        Logger::info(("[S2][CaseManager] Retrieved " + std::to_string(out_cases.size()) + " cases by status.").c_str());
        return JusticeFlow::ResultCode::OK;
    }

} // namespace subsystem2