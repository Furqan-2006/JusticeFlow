#include "../../include/controllers/InvestigationManager.h"
#include "../../../common/logger.h"

// Failsafe DB include using the exact compiler path
#include "ipc_manager.h" 

#include <string>
#include <vector>

namespace subsystem2 {

// Safe DB Helper (Bypasses namespace resolution issues)
static ipc::IpcManager& getDB() {
    return ipc::IpcManager::getInstance();
}

JusticeFlow::ResultCode InvestigationManager::draftChargeSheet(int64_t case_id, 
                                                               const JusticeFlow::SessionContext& session, 
                                                               ChargeSheet*& out_sheet) {
    if (!session.isValid) return JusticeFlow::ResultCode::AUTH_FAILED;

    // 1. Prepare SQL Insert
    // We only provide case_id and filed_by. DB Defaults handle the rest ('DRAFT', 'ORIGINAL', etc.)
    std::string sql = "INSERT INTO public.charge_sheets (case_id, filed_by) VALUES ("
                      + std::to_string(case_id) + ", "
                      + std::to_string(session.officerId) + ") RETURNING charge_sheet_id, charge_sheet_number;";

    // 2. Execute
    std::vector<std::vector<std::string>> results;
    JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

    if (db_res != JusticeFlow::ResultCode::OK || results.empty()) {
        Logger::error("[S2][InvestigationManager] Failed to draft Charge Sheet in DB.");
        return JusticeFlow::ResultCode::DB_ERROR;
    }

    // 3. Build the Entity
    ChargeSheetDTO data;
    data.charge_sheet_id = std::stoll(results[0][0]);
    data.charge_sheet_number = results[0][1];
    data.case_id = case_id;
    data.charge_sheet_status = JusticeFlow::ChargeSheetStatus::DRAFT;
    data.sheet_type = JusticeFlow::SheetType::ORIGINAL;
    data.filed_by = session.officerId;
    data.is_locked = false;

    out_sheet = new ChargeSheet(data);
    Logger::info("[S2][InvestigationManager] Drafted new Charge Sheet.");
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode InvestigationManager::submitChargeSheet(ChargeSheet* sheet, 
                                                                const JusticeFlow::SessionContext& session) {
    if (!session.isValid) return JusticeFlow::ResultCode::AUTH_FAILED;
    if (sheet == nullptr) return JusticeFlow::ResultCode::INVALID_INPUT;

    // 1. Execute Domain Logic FIRST (Check if it has laws invoked, check if already locked)
    JusticeFlow::ResultCode domain_res = sheet->submitToMagistrate(session.officerId);
    if (domain_res != JusticeFlow::ResultCode::OK) {
        Logger::error("[S2][InvestigationManager] Domain validation failed. Cannot submit.");
        return domain_res; // Returns RECORD_LOCKED or INVALID_STATE
    }

    // 2. If domain passes, persist the state to the DB
    std::string sql = "UPDATE public.charge_sheets SET "
                      "charge_sheet_status = 'SUBMITTED_TO_COURT', "
                      "is_locked = true, "
                      "submitted_to_court_at = now(), "
                      "submitted_by = " + std::to_string(session.officerId) + ", "
                      "locked_at = now(), "
                      "locked_by = " + std::to_string(session.officerId) + " "
                      "WHERE charge_sheet_id = " + std::to_string(sheet->getSheetId()) + ";";

    std::vector<std::vector<std::string>> results;
    JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

    if (db_res != JusticeFlow::ResultCode::OK) {
        Logger::error("[S2][InvestigationManager] Failed to update DB during submission.");
        return JusticeFlow::ResultCode::DB_ERROR;
    }

    Logger::info("[S2][InvestigationManager] Charge Sheet securely submitted to Magistrate and locked.");
    return JusticeFlow::ResultCode::OK;
}

} // namespace subsystem2
