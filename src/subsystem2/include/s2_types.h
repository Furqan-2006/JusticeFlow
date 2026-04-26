#ifndef S2_TYPES_H
#define S2_TYPES_H

#include <string>
#include <vector>
#include <ctime>
#include <cstdint> // For int64_t
#include "../../common/constants.h" // For Enums

namespace subsystem2 {

// 1. Data Transfer Object for CASES table
struct CaseDTO {
    int64_t case_id;
    std::string fir_number;
    JusticeFlow::CaseType case_type;
    JusticeFlow::CaseStatus case_status;
    time_t incident_date;
    std::string incident_address;
    std::string incident_description;
    double incident_lat;
    double incident_lon;
    int64_t station_id;
    std::string primary_complainant_cnic;
    int64_t filed_by;
    time_t filed_at;
    time_t updated_at;
    int64_t lead_officer_id;
    int64_t parent_case_id;
    time_t closed_at;
    std::string closure_reason;
    JusticeFlow::ApprovalStatus approval_status;
    int64_t approved_by;
    time_t approved_at;
    int64_t reopened_by;
    time_t reopened_at;
    std::string reopen_reason;
};

// 2. Data Transfer Object for EVIDENCE table
struct EvidenceDTO {
    int64_t evidence_id;
    std::string evidence_number;
    int64_t case_id;
    JusticeFlow::EvidenceType evidence_type;
    JusticeFlow::EvidenceStatus evidence_status;
    std::string description;
    int16_t quantity; // Postgres smallint
    std::string file_path;
    int64_t collected_by;
    time_t collected_at;
    std::string collection_location;
    bool is_deleted;
    time_t deleted_at;
    int64_t deleted_by;
    std::string deletion_reason;
    time_t created_at;
    time_t updated_at;
};

// 3. Data Transfer Object for CHARGE_SHEETS table
struct ChargeSheetDTO {
    int64_t charge_sheet_id;
    std::string charge_sheet_number;
    int64_t case_id;
    int64_t parent_charge_sheet_id;
    JusticeFlow::SheetType sheet_type;
    JusticeFlow::ChargeSheetStatus charge_sheet_status;
    std::string court_name;
    std::string magistrate_name;
    std::vector<std::string> laws_invoked; // Postgres text[]
    int64_t filed_by;
    time_t filing_date;
    time_t submitted_to_court_at;
    int64_t submitted_by;
    time_t court_response_date;
    std::string court_remarks;
    bool is_locked;
    time_t locked_at;
    int64_t locked_by;
    time_t created_at;
    time_t updated_at;
};

// Helper struct for Use Case 1: Register FIR
// (The input data the IO provides from the UI, matching exactly what the DB needs)
struct FIRRegistrationRequest {
    int64_t filed_by_officer_id; // From SessionContext
    int64_t station_id;          // From SessionContext
    std::string complainant_cnic;
    JusticeFlow::CaseType type;
    std::string incident_address;
    std::string description;
    double lat;
    double lon;
    time_t incident_date;
};

} // namespace subsystem2

#endif // S2_TYPES_H
