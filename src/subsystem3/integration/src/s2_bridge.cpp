#include "integration/include/s2_bridge.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"
#include <sstream>

using namespace JusticeFlow;

namespace integration
{

    ResultCode S2Bridge::getCaseRecord(int case_id, Case &out_case)
    {
        // Query: SELECT * FROM subsystem2.cases WHERE case_id = ?
        std::stringstream query;
        query << "SELECT case_id, fir_number, case_type, case_status, incident_date, "
              << "incident_address, incident_description, incident_lat, incident_lon, "
              << "station_id, primary_complainant_cnic, filed_by, filed_at, updated_at, "
              << "lead_officer_id, parent_case_id, closed_at, closure_reason, approval_status, "
              << "approved_by, approved_at, reopened_by, reopened_at, reopen_reason "
              << "FROM subsystem2.cases WHERE case_id = " << case_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("s2_bridge: Database query failed for case record");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("s2_bridge: Case not found");
            return ResultCode::NOT_FOUND;
        }

        const auto &row = results[0];
        out_case.case_id = std::stoi(row[0]);
        out_case.fir_number = row[1];

        // Parse case_type enum
        const std::string &case_type_str = row[2];
        if (case_type_str == "MURDER")
            out_case.case_type = CaseType::MURDER;
        else if (case_type_str == "ROBBERY")
            out_case.case_type = CaseType::ROBBERY;
        else if (case_type_str == "KIDNAPPING")
            out_case.case_type = CaseType::KIDNAPPING;
        // ... (add all case types as needed)

        // Parse case_status enum
        const std::string &status_str = row[3];
        if (status_str == "REGISTERED")
            out_case.case_status = CaseStatus::REGISTERED;
        else if (status_str == "UNDER_INVESTIGATION")
            out_case.case_status = CaseStatus::UNDER_INVESTIGATION;
        else if (status_str == "EVIDENCE_COLLECTED")
            out_case.case_status = CaseStatus::EVIDENCE_COLLECTED;
        else if (status_str == "PENDING_TRIAL")
            out_case.case_status = CaseStatus::PENDING_TRIAL;
        else if (status_str == "CLOSED")
            out_case.case_status = CaseStatus::CLOSED;
        else if (status_str == "REOPENED")
            out_case.case_status = CaseStatus::REOPENED;

        out_case.incident_date = std::stol(row[4]);
        out_case.incident_address = row[5];
        out_case.incident_description = row[6];
        out_case.incident_lat = std::stod(row[7]);
        out_case.incident_lon = std::stod(row[8]);
        out_case.station_id = std::stoi(row[9]);
        out_case.primary_complainant_cnic = row[10];
        out_case.filed_by = std::stoi(row[11]);
        out_case.filed_at = std::stol(row[12]);
        out_case.updated_at = std::stol(row[13]);
        out_case.lead_officer_id = std::stoi(row[14]);
        out_case.parent_case_id = std::stoi(row[15]);
        out_case.closed_at = std::stol(row[16]);
        out_case.closure_reason = row[17];

        // Parse approval_status enum
        const std::string &approval_str = row[18];
        if (approval_str == "NOT_REQUIRED")
            out_case.approval_status = ApprovalStatus::NOT_REQUIRED;
        else if (approval_str == "PENDING_APPROVAL")
            out_case.approval_status = ApprovalStatus::PENDING_APPROVAL;
        else if (approval_str == "APPROVED")
            out_case.approval_status = ApprovalStatus::APPROVED;
        else if (approval_str == "REJECTED")
            out_case.approval_status = ApprovalStatus::REJECTED;

        out_case.approved_by = std::stoi(row[19]);
        out_case.approved_at = std::stol(row[20]);
        out_case.reopened_by = std::stoi(row[21]);
        out_case.reopened_at = std::stol(row[22]);
        out_case.reopen_reason = row[23];

        Logger::info("s2_bridge: Retrieved case record");
        return ResultCode::OK;
    }

    ResultCode S2Bridge::getEvidenceRecord(int evidence_id, Evidence &out_evidence)
    {
        // Query: SELECT * FROM subsystem2.evidence WHERE evidence_id = ?
        std::stringstream query;
        query << "SELECT evidence_id, evidence_number, case_id, evidence_type, evidence_status, "
              << "description, quantity, file_path, collected_by, collected_at, collection_location, "
              << "is_deleted, deleted_at, deleted_by, deletion_reason, created_at, updated_at "
              << "FROM subsystem2.evidence WHERE evidence_id = " << evidence_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("s2_bridge: Database query failed for evidence record");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("s2_bridge: Evidence not found");
            return ResultCode::NOT_FOUND;
        }

        const auto &row = results[0];
        out_evidence.evidence_id = std::stoi(row[0]);
        out_evidence.evidence_number = row[1];
        out_evidence.case_id = std::stoi(row[2]);

        // Parse evidence_type enum
        const std::string &type_str = row[3];
        if (type_str == "PHYSICAL")
            out_evidence.evidence_type = EvidenceType::PHYSICAL;
        else if (type_str == "DIGITAL")
            out_evidence.evidence_type = EvidenceType::DIGITAL;
        else if (type_str == "TESTIMONIAL")
            out_evidence.evidence_type = EvidenceType::TESTIMONIAL;
        else if (type_str == "FORENSIC")
            out_evidence.evidence_type = EvidenceType::FORENSIC;
        else if (type_str == "DOCUMENTARY")
            out_evidence.evidence_type = EvidenceType::DOCUMENTARY;

        // Parse evidence_status enum
        const std::string &status_str = row[4];
        if (status_str == "RECEIVED")
            out_evidence.evidence_status = EvidenceStatus::RECEIVED;
        else if (status_str == "SEALED")
            out_evidence.evidence_status = EvidenceStatus::SEALED;
        else if (status_str == "SENT_TO_LAB")
            out_evidence.evidence_status = EvidenceStatus::SENT_TO_LAB;
        else if (status_str == "RETURNED_FROM_LAB")
            out_evidence.evidence_status = EvidenceStatus::RETURNED_FROM_LAB;
        else if (status_str == "PRODUCED_IN_COURT")
            out_evidence.evidence_status = EvidenceStatus::PRODUCED_IN_COURT;
        else if (status_str == "DISPOSED")
            out_evidence.evidence_status = EvidenceStatus::DISPOSED;

        out_evidence.description = row[5];
        out_evidence.quantity = std::stoi(row[6]);
        out_evidence.file_path = row[7];
        out_evidence.collected_by = std::stoi(row[8]);
        out_evidence.collected_at = std::stol(row[9]);
        out_evidence.collection_location = row[10];
        out_evidence.is_deleted = (row[11] == "true" || row[11] == "1");
        out_evidence.deleted_at = std::stol(row[12]);
        out_evidence.deleted_by = std::stoi(row[13]);
        out_evidence.deletion_reason = row[14];
        out_evidence.created_at = std::stol(row[15]);
        out_evidence.updated_at = std::stol(row[16]);

        Logger::info("s2_bridge: Retrieved evidence record");
        return ResultCode::OK;
    }

    ResultCode S2Bridge::notifyForensicSubmission(int evidence_id, int request_id)
    {
        // Update: evidence_status = 'SENT_TO_LAB' for the evidence
        std::stringstream update_query;
        update_query << "UPDATE subsystem2.evidence SET evidence_status = 'SENT_TO_LAB' "
                     << "WHERE evidence_id = " << evidence_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("s2_bridge: Failed to notify forensic submission");
            return db_result;
        }

        Logger::info("s2_bridge: Notified forensic submission");
        return ResultCode::OK;
    }

    ResultCode S2Bridge::validateCaseOwnership(int case_id, int station_id)
    {
        // Query: SELECT station_id FROM subsystem2.cases WHERE case_id = ?
        std::stringstream query;
        query << "SELECT station_id FROM subsystem2.cases WHERE case_id = " << case_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("s2_bridge: Database query failed for case ownership");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("s2_bridge: Case not found");
            return ResultCode::NOT_FOUND;
        }

        int case_station_id = std::stoi(results[0][0]);

        if (case_station_id != station_id)
        {
            Logger::debug("s2_bridge: Case ownership validation failed");
            return ResultCode::JURISDICTION_DENIED;
        }

        Logger::info("s2_bridge: Case ownership validated");
        return ResultCode::OK;
    }

} // namespace integration