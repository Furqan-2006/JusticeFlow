#include "../../include/controllers/CaseManager.h"
#include "../../../common/logger.h"
#include "../../../os_layer/ipc/include/ipc_manager.h" 

#include <string>
#include <vector>

namespace subsystem2 {

// Helper to grab the singleton safely within this namespace
static ipc::IpcManager& getDB() {
    return ipc::IpcManager::getInstance();
}

JusticeFlow::ResultCode CaseManager::registerFIR(const FIRRegistrationRequest& request, 
                                                 const JusticeFlow::SessionContext& session, 
                                                 Case*& out_case) {
    if (!session.isValid) { 
        Logger::error("[S2][CaseManager] Unauthorized FIR attempt.");
        return JusticeFlow::ResultCode::AUTH_FAILED;
    }

    if (session.rank != JusticeFlow::OfficerRank::ASI && 
        session.rank != JusticeFlow::OfficerRank::SI && 
        session.rank != JusticeFlow::OfficerRank::INSPECTOR) {
        Logger::error("[S2][CaseManager] Rank insufficient to register FIR.");
        return JusticeFlow::ResultCode::RANK_INSUFFICIENT;
    }

    std::string case_type_str;
    if (request.type == JusticeFlow::CaseType::MURDER) case_type_str = "MURDER";
    else if (request.type == JusticeFlow::CaseType::THEFT) case_type_str = "THEFT";
    else case_type_str = "OTHER"; 

    std::string sql = "INSERT INTO public.cases (case_type, case_status, incident_date, incident_address, "
                      "incident_description, incident_lat, incident_lon, station_id, "
                      "primary_complainant_cnic, filed_by) VALUES ("
                      "'" + case_type_str + "', "                 
                      "'REGISTERED', "                            
                      "to_timestamp(" + std::to_string(request.incident_date) + "), "
                      "'" + request.incident_address + "', "
                      "'" + request.description + "', "
                      + std::to_string(request.lat) + ", " 
                      + std::to_string(request.lon) + ", "
                      + std::to_string(request.station_id) + ", "
                      "'" + request.complainant_cnic + "', "
                      + std::to_string(request.filed_by_officer_id) + ") RETURNING case_id, fir_number;";

    std::vector<std::vector<std::string>> results;
    
    // Call our safe helper function
    JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

    if (db_res != JusticeFlow::ResultCode::OK || results.empty()) {
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

JusticeFlow::ResultCode CaseManager::fetchCase(int64_t case_id, Case*& out_case) {
    std::string sql = "SELECT case_id, fir_number FROM public.cases WHERE case_id = " + std::to_string(case_id) + ";";
    
    std::vector<std::vector<std::string>> results;
    
    // Call our safe helper function
    JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

    if (db_res != JusticeFlow::ResultCode::OK || results.empty()) {
        return JusticeFlow::ResultCode::NOT_FOUND;
    }

    CaseDTO data;
    data.case_id = std::stoll(results[0][0]);
    data.fir_number = results[0][1];
    
    out_case = new Case(data);
    return JusticeFlow::ResultCode::OK;
}

} // namespace subsystem2