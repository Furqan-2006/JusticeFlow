#include "forensic/include/forensic_repository.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"
#include <sstream>

using namespace JusticeFlow;

namespace forensic
{

    ResultCode ForensicRepository::insertRequest(int case_id,
                                                 const std::string &lab_name,
                                                 const std::string &examiner_name,
                                                 ExaminationPurpose examination_purpose,
                                                 const std::string &purpose_description,
                                                 const std::string &report_expected_date,
                                                 int authorized_by,
                                                 int &out_request_id)
    {
        // Convert enum to string
        std::string purpose_str;
        switch (examination_purpose)
        {
        case ExaminationPurpose::DNA_ANALYSIS:
            purpose_str = "DNA_ANALYSIS";
            break;
        case ExaminationPurpose::FINGERPRINT_ANALYSIS:
            purpose_str = "FINGERPRINT_ANALYSIS";
            break;
        case ExaminationPurpose::BALLISTICS_ANALYSIS:
            purpose_str = "BALLISTICS_ANALYSIS";
            break;
        case ExaminationPurpose::TOXICOLOGY_ANALYSIS:
            purpose_str = "TOXICOLOGY_ANALYSIS";
            break;
        case ExaminationPurpose::DIGITAL_FORENSICS:
            purpose_str = "DIGITAL_FORENSICS";
            break;
        case ExaminationPurpose::DOCUMENT_EXAMINATION:
            purpose_str = "DOCUMENT_EXAMINATION";
            break;
        case ExaminationPurpose::BLOOD_ANALYSIS:
            purpose_str = "BLOOD_ANALYSIS";
            break;
        case ExaminationPurpose::NARCOTICS_TESTING:
            purpose_str = "NARCOTICS_TESTING";
            break;
        case ExaminationPurpose::TRACE_EVIDENCE_ANALYSIS:
            purpose_str = "TRACE_EVIDENCE_ANALYSIS";
            break;
        case ExaminationPurpose::OTHER:
            purpose_str = "OTHER";
            break;
        default:
            purpose_str = "UNKNOWN";
        }

        // Generate request number
        std::stringstream req_num_ss;
        req_num_ss << "FR-" << std::time(nullptr) << "-" << authorized_by;
        std::string request_number = req_num_ss.str();

        // Construct INSERT query
        std::stringstream insert_query;
        insert_query << "INSERT INTO subsystem3.Forensic_Lab_Requests ("
                     << "request_number, case_id, lab_name, examiner_name, examination_purpose, "
                     << "purpose_description, sent_date, report_expected_date, request_status, "
                     << "authorized_by, created_at) VALUES ("
                     << "'" << request_number << "', " << case_id << ", "
                     << "'" << lab_name << "', '" << examiner_name << "', '" << purpose_str << "', "
                     << "'" << purpose_description << "', now(), '" << report_expected_date << "', "
                     << "'REQUESTED', " << authorized_by << ", now()) "
                     << "RETURNING request_id;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(insert_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("forensic_repository: Failed to insert forensic request");
            return db_result;
        }

        if (results.empty())
        {
            Logger::error("forensic_repository: Request insertion returned no ID");
            return ResultCode::DB_ERROR;
        }

        out_request_id = std::stoi(results[0][0]);
        Logger::info("forensic_repository: Forensic request inserted");
        return ResultCode::OK;
    }

    ResultCode ForensicRepository::updateRequestStatus(int request_id,
                                                       ForensicRequestStatus new_status,
                                                       const std::string &findings,
                                                       const std::string &report_file_path)
    {
        // Convert enum to string
        std::string status_str;
        switch (new_status)
        {
        case ForensicRequestStatus::REQUESTED:
            status_str = "REQUESTED";
            break;
        case ForensicRequestStatus::RECEIVED_BY_LAB:
            status_str = "RECEIVED_BY_LAB";
            break;
        case ForensicRequestStatus::UNDER_EXAMINATION:
            status_str = "UNDER_EXAMINATION";
            break;
        case ForensicRequestStatus::REPORT_READY:
            status_str = "REPORT_READY";
            break;
        case ForensicRequestStatus::REPORT_DELIVERED:
            status_str = "REPORT_DELIVERED";
            break;
        case ForensicRequestStatus::CONTESTED:
            status_str = "CONTESTED";
            break;
        default:
            status_str = "UNKNOWN";
        }

        // Construct UPDATE query
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.Forensic_Lab_Requests SET "
                     << "request_status = '" << status_str << "', ";

        if (!findings.empty())
        {
            update_query << "findings = '" << findings << "', ";
        }

        if (!report_file_path.empty())
        {
            update_query << "report_file_path = '" << report_file_path << "', ";
        }

        update_query << "updated_at = now() WHERE request_id = " << request_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("forensic_repository: Failed to update request status");
            return db_result;
        }

        Logger::info("forensic_repository: Request status updated");
        return ResultCode::OK;
    }

    ResultCode ForensicRepository::insertEvidenceLink(int request_id,
                                                      int evidence_id,
                                                      const std::string &notes)
    {
        // Construct INSERT query
        std::stringstream insert_query;
        insert_query << "INSERT INTO subsystem3.Forensic_Request_Evidence ("
                     << "request_id, evidence_id, notes, added_at) VALUES ("
                     << request_id << ", " << evidence_id << ", "
                     << "'" << notes << "', now());";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(insert_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("forensic_repository: Failed to link evidence");
            return db_result;
        }

        // Trigger automatically fires to update evidence status to SENT_TO_LAB
        Logger::info("forensic_repository: Evidence linked to forensic request");
        return ResultCode::OK;
    }

    ResultCode ForensicRepository::getRequestsByCase(int case_id,
                                                     std::vector<ForensicLabRequest> &out_requests)
    {
        // Construct SELECT query
        std::stringstream query;
        query << "SELECT request_id, request_number, case_id, lab_name, examiner_name, "
              << "examination_purpose, purpose_description, sent_date, received_by_lab_date, "
              << "report_expected_date, request_status, authorized_by, findings, report_file_path, "
              << "report_delivered_date, is_amended, amendment_notes, amended_at, amended_by, "
              << "is_contested, contest_reason, contested_by, contested_at, created_at, updated_at "
              << "FROM subsystem3.Forensic_Lab_Requests WHERE case_id = " << case_id << " "
              << "ORDER BY created_at DESC;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("forensic_repository: Failed to query requests by case");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("forensic_repository: No requests found for case");
            return ResultCode::NOT_FOUND;
        }

        // Parse results into ForensicLabRequest structs
        for (const auto &row : results)
        {
            ForensicLabRequest request;
            request.request_id = std::stoi(row[0]);
            request.request_number = row[1];
            request.case_id = std::stoi(row[2]);
            request.lab_name = row[3];
            request.examiner_name = row[4];

            // Parse examination_purpose enum
            const std::string &purpose_str = row[5];
            if (purpose_str == "DNA_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::DNA_ANALYSIS;
            else if (purpose_str == "FINGERPRINT_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::FINGERPRINT_ANALYSIS;
            else if (purpose_str == "BALLISTICS_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::BALLISTICS_ANALYSIS;
            else if (purpose_str == "TOXICOLOGY_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::TOXICOLOGY_ANALYSIS;
            else if (purpose_str == "DIGITAL_FORENSICS")
                request.examination_purpose = ExaminationPurpose::DIGITAL_FORENSICS;
            else if (purpose_str == "DOCUMENT_EXAMINATION")
                request.examination_purpose = ExaminationPurpose::DOCUMENT_EXAMINATION;
            else if (purpose_str == "BLOOD_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::BLOOD_ANALYSIS;
            else if (purpose_str == "NARCOTICS_TESTING")
                request.examination_purpose = ExaminationPurpose::NARCOTICS_TESTING;
            else if (purpose_str == "TRACE_EVIDENCE_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::TRACE_EVIDENCE_ANALYSIS;
            else if (purpose_str == "OTHER")
                request.examination_purpose = ExaminationPurpose::OTHER;

            request.purpose_description = row[6];
            request.sent_date = row[7];
            request.received_by_lab_date = row[8];
            request.report_expected_date = row[9];

            // Parse request_status enum
            const std::string &status_str = row[10];
            if (status_str == "REQUESTED")
                request.request_status = ForensicRequestStatus::REQUESTED;
            else if (status_str == "RECEIVED_BY_LAB")
                request.request_status = ForensicRequestStatus::RECEIVED_BY_LAB;
            else if (status_str == "UNDER_EXAMINATION")
                request.request_status = ForensicRequestStatus::UNDER_EXAMINATION;
            else if (status_str == "REPORT_READY")
                request.request_status = ForensicRequestStatus::REPORT_READY;
            else if (status_str == "REPORT_DELIVERED")
                request.request_status = ForensicRequestStatus::REPORT_DELIVERED;
            else if (status_str == "CONTESTED")
                request.request_status = ForensicRequestStatus::CONTESTED;

            request.authorized_by = std::stoi(row[11]);
            request.findings = row[12];
            request.report_file_path = row[13];
            request.report_delivered_date = row[14];
            request.is_amended = (row[15] == "true" || row[15] == "1");
            request.amendment_notes = row[16];
            request.amended_at = std::stol(row[17]);
            request.amended_by = std::stoi(row[18]);
            request.is_contested = (row[19] == "true" || row[19] == "1");
            request.contest_reason = row[20];
            request.contested_by = std::stoi(row[21]);
            request.contested_at = std::stol(row[22]);
            request.created_at = std::stol(row[23]);
            request.updated_at = std::stol(row[24]);

            out_requests.push_back(request);
        }

        Logger::info("forensic_repository: Retrieved forensic requests by case");
        return ResultCode::OK;
    }

    ResultCode ForensicRepository::getPendingRequests(std::vector<ForensicLabRequest> &out_requests)
    {
        // Construct SELECT query for pending requests
        std::stringstream query;
        query << "SELECT request_id, request_number, case_id, lab_name, examiner_name, "
              << "examination_purpose, purpose_description, sent_date, received_by_lab_date, "
              << "report_expected_date, request_status, authorized_by, findings, report_file_path, "
              << "report_delivered_date, is_amended, amendment_notes, amended_at, amended_by, "
              << "is_contested, contest_reason, contested_by, contested_at, created_at, updated_at "
              << "FROM subsystem3.Forensic_Lab_Requests "
              << "WHERE request_status IN ('REQUESTED', 'RECEIVED_BY_LAB', 'UNDER_EXAMINATION', 'REPORT_READY') "
              << "ORDER BY report_expected_date ASC;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("forensic_repository: Failed to query pending requests");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("forensic_repository: No pending requests found");
            return ResultCode::NOT_FOUND;
        }

        // Parse results (same as getRequestsByCase)
        for (const auto &row : results)
        {
            ForensicLabRequest request;
            request.request_id = std::stoi(row[0]);
            request.request_number = row[1];
            request.case_id = std::stoi(row[2]);
            request.lab_name = row[3];
            request.examiner_name = row[4];

            const std::string &purpose_str = row[5];
            if (purpose_str == "DNA_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::DNA_ANALYSIS;
            else if (purpose_str == "FINGERPRINT_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::FINGERPRINT_ANALYSIS;
            else if (purpose_str == "BALLISTICS_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::BALLISTICS_ANALYSIS;
            else if (purpose_str == "TOXICOLOGY_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::TOXICOLOGY_ANALYSIS;
            else if (purpose_str == "DIGITAL_FORENSICS")
                request.examination_purpose = ExaminationPurpose::DIGITAL_FORENSICS;
            else if (purpose_str == "DOCUMENT_EXAMINATION")
                request.examination_purpose = ExaminationPurpose::DOCUMENT_EXAMINATION;
            else if (purpose_str == "BLOOD_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::BLOOD_ANALYSIS;
            else if (purpose_str == "NARCOTICS_TESTING")
                request.examination_purpose = ExaminationPurpose::NARCOTICS_TESTING;
            else if (purpose_str == "TRACE_EVIDENCE_ANALYSIS")
                request.examination_purpose = ExaminationPurpose::TRACE_EVIDENCE_ANALYSIS;
            else if (purpose_str == "OTHER")
                request.examination_purpose = ExaminationPurpose::OTHER;

            request.purpose_description = row[6];
            request.sent_date = row[7];
            request.received_by_lab_date = row[8];
            request.report_expected_date = row[9];

            const std::string &status_str = row[10];
            if (status_str == "REQUESTED")
                request.request_status = ForensicRequestStatus::REQUESTED;
            else if (status_str == "RECEIVED_BY_LAB")
                request.request_status = ForensicRequestStatus::RECEIVED_BY_LAB;
            else if (status_str == "UNDER_EXAMINATION")
                request.request_status = ForensicRequestStatus::UNDER_EXAMINATION;
            else if (status_str == "REPORT_READY")
                request.request_status = ForensicRequestStatus::REPORT_READY;
            else if (status_str == "REPORT_DELIVERED")
                request.request_status = ForensicRequestStatus::REPORT_DELIVERED;
            else if (status_str == "CONTESTED")
                request.request_status = ForensicRequestStatus::CONTESTED;

            request.authorized_by = std::stoi(row[11]);
            request.findings = row[12];
            request.report_file_path = row[13];
            request.report_delivered_date = row[14];
            request.is_amended = (row[15] == "true" || row[15] == "1");
            request.amendment_notes = row[16];
            request.amended_at = std::stol(row[17]);
            request.amended_by = std::stoi(row[18]);
            request.is_contested = (row[19] == "true" || row[19] == "1");
            request.contest_reason = row[20];
            request.contested_by = std::stoi(row[21]);
            request.contested_at = std::stol(row[22]);
            request.created_at = std::stol(row[23]);
            request.updated_at = std::stol(row[24]);

            out_requests.push_back(request);
        }

        Logger::info("forensic_repository: Retrieved pending requests");
        return ResultCode::OK;
    }

    ResultCode ForensicRepository::getEvidenceByRequest(int request_id,
                                                        std::vector<Evidence> &out_evidence)
    {
        // Construct SELECT query joining Forensic_Request_Evidence and evidence
        std::stringstream query;
        query << "SELECT e.evidence_id, e.evidence_number, e.case_id, e.evidence_type, "
              << "e.evidence_status, e.description, e.quantity, e.file_path, e.collected_by, "
              << "e.collected_at, e.collection_location, e.is_deleted, e.deleted_at, e.deleted_by, "
              << "e.deletion_reason, e.created_at, e.updated_at "
              << "FROM subsystem2.evidence e "
              << "INNER JOIN subsystem3.Forensic_Request_Evidence fre ON e.evidence_id = fre.evidence_id "
              << "WHERE fre.request_id = " << request_id << " "
              << "ORDER BY fre.added_at ASC;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK)
        {
            Logger::error("forensic_repository: Failed to query evidence by request");
            return db_result;
        }

        if (results.empty())
        {
            Logger::debug("forensic_repository: No evidence found for request");
            return ResultCode::NOT_FOUND;
        }

        // Parse results into Evidence structs
        for (const auto &row : results)
        {
            Evidence evidence;
            evidence.evidence_id = std::stoi(row[0]);
            evidence.evidence_number = row[1];
            evidence.case_id = std::stoi(row[2]);

            const std::string &type_str = row[3];
            if (type_str == "PHYSICAL")
                evidence.evidence_type = EvidenceType::PHYSICAL;
            else if (type_str == "DIGITAL")
                evidence.evidence_type = EvidenceType::DIGITAL;
            else if (type_str == "TESTIMONIAL")
                evidence.evidence_type = EvidenceType::TESTIMONIAL;
            else if (type_str == "FORENSIC")
                evidence.evidence_type = EvidenceType::FORENSIC;
            else if (type_str == "DOCUMENTARY")
                evidence.evidence_type = EvidenceType::DOCUMENTARY;

            const std::string &status_str = row[4];
            if (status_str == "RECEIVED")
                evidence.evidence_status = EvidenceStatus::RECEIVED;
            else if (status_str == "SEALED")
                evidence.evidence_status = EvidenceStatus::SEALED;
            else if (status_str == "SENT_TO_LAB")
                evidence.evidence_status = EvidenceStatus::SENT_TO_LAB;
            else if (status_str == "RETURNED_FROM_LAB")
                evidence.evidence_status = EvidenceStatus::RETURNED_FROM_LAB;
            else if (status_str == "PRODUCED_IN_COURT")
                evidence.evidence_status = EvidenceStatus::PRODUCED_IN_COURT;
            else if (status_str == "DISPOSED")
                evidence.evidence_status = EvidenceStatus::DISPOSED;

            evidence.description = row[5];
            evidence.quantity = std::stoi(row[6]);
            evidence.file_path = row[7];
            evidence.collected_by = std::stoi(row[8]);
            evidence.collected_at = std::stol(row[9]);
            evidence.collection_location = row[10];
            evidence.is_deleted = (row[11] == "true" || row[11] == "1");
            evidence.deleted_at = std::stol(row[12]);
            evidence.deleted_by = std::stoi(row[13]);
            evidence.deletion_reason = row[14];
            evidence.created_at = std::stol(row[15]);
            evidence.updated_at = std::stol(row[16]);

            out_evidence.push_back(evidence);
        }

        Logger::info("forensic_repository: Retrieved evidence by request");
        return ResultCode::OK;
    }

} // namespace forensic