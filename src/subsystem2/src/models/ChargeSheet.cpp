#include "../../include/models/ChargeSheet.h"
#include <ctime>

namespace subsystem2 {

ChargeSheet::ChargeSheet(const ChargeSheetDTO& init_data) 
    : data(init_data) {}

// --- Data Accessors ---

int64_t ChargeSheet::getSheetId() const {
    return data.charge_sheet_id;
}

std::string ChargeSheet::getSheetNumber() const {
    return data.charge_sheet_number;
}

JusticeFlow::ChargeSheetStatus ChargeSheet::getStatus() const {
    return data.charge_sheet_status;
}

bool ChargeSheet::isLocked() const {
    return data.is_locked;
}

ChargeSheetDTO ChargeSheet::getDTO() const {
    return data;
}

// --- Domain Logic ---

JusticeFlow::ResultCode ChargeSheet::addLawInvoked(const std::string& law) {
    // Contract Rule: Cannot edit a locked charge sheet
    if (data.is_locked) {
        return JusticeFlow::ResultCode::RECORD_LOCKED;
    }

    if (law.empty()) {
        return JusticeFlow::ResultCode::INVALID_INPUT;
    }

    data.laws_invoked.push_back(law);
    data.updated_at = std::time(nullptr);
    
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode ChargeSheet::submitToMagistrate(int64_t officer_id) {
    if (data.is_locked) {
        return JusticeFlow::ResultCode::RECORD_LOCKED;
    }

    // Business Rule: Must have at least one law invoked before submission
    if (data.laws_invoked.empty()) {
        return JusticeFlow::ResultCode::INVALID_STATE;
    }

    // Update status to reflect court submission
    data.charge_sheet_status = JusticeFlow::ChargeSheetStatus::SUBMITTED_TO_COURT;
    data.submitted_to_court_at = std::time(nullptr);
    data.submitted_by = officer_id;
    data.updated_at = std::time(nullptr);

    // Lock the document permanently (mirrors the auto_lock_charge_sheet DB trigger)
    data.is_locked = true;
    data.locked_at = std::time(nullptr);
    data.locked_by = officer_id;

    return JusticeFlow::ResultCode::OK;
}

} // namespace subsystem2
