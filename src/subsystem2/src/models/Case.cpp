#include "../../include/models/Case.h"

namespace subsystem2 {

Case::Case(const CaseDTO& init_data) 
    : data(init_data), status_strategy(nullptr) {}

// --- Data Accessors ---

int64_t Case::getCaseId() const {
    return data.case_id;
}

std::string Case::getFirNumber() const {
    return data.fir_number;
}

JusticeFlow::CaseStatus Case::getStatus() const {
    return data.case_status;
}

int64_t Case::getLeadOfficerId() const {
    return data.lead_officer_id;
}

CaseDTO Case::getDTO() const {
    return data;
}

// --- Domain Logic ---

void Case::addEvidence(Evidence* ev) {
    if (ev) {
        evidence_list.push_back(ev);
    }
}

void Case::setLeadOfficer(int64_t officer_id) {
    data.lead_officer_id = officer_id;
    data.updated_at = std::time(nullptr);
}

void Case::setStatusEnum(JusticeFlow::CaseStatus new_status) {
    data.case_status = new_status;
    data.updated_at = std::time(nullptr);
}

// --- Strategy Pattern Implementation ---

void Case::setStatusStrategy(std::unique_ptr<ICaseStatusStrategy> new_strategy) {
    status_strategy = std::move(new_strategy);
}
JusticeFlow::ResultCode Case::transitionStatus(const JusticeFlow::SessionContext& session) {
    // If no strategy is set, we cannot transition
    if (!status_strategy) {
        return JusticeFlow::ResultCode::INVALID_STATE;
    }

    // Delegate the complex rank-checking and validation to the Strategy object
    return status_strategy->processTransition(this, session);
}

} // namespace subsystem2
