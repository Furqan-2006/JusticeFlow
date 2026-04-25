#include "forensic/include/forensic_request.h"
#include "forensic/include/forensic_repository.h"
#include "security/include/access_control.h"
#include "legal/include/evidence_rules.h"
#include "integration/include/audit_bridge.h"
#include "integration/include/s2_bridge.h"
#include "utils/include/time_utils.h"
#include "common/logger.h"
#include <map>

using namespace JusticeFlow;

namespace forensic
{

    // State transition validation lookup table
    static std::map<std::string, std::set<std::string>> request_transitions = {
        {"REQUESTED", {"RECEIVED_BY_LAB"}},
        {"RECEIVED_BY_LAB", {"UNDER_EXAMINATION"}},
        {"UNDER_EXAMINATION", {"REPORT_READY"}},
        {"REPORT_READY", {"REPORT_DELIVERED", "CONTESTED"}},
        {"REPORT_DELIVERED", {}},
        {"CONTESTED", {"REPORT_READY", "REPORT_DELIVERED"}}};

    bool ForensicRequest::isValidTransition(const std::string &current_state,
                                            const std::string &new_state)
    {
        auto it = request_transitions.find(current_state);
        if (it == request_transitions.end())
        {
            std::string msg = "forensic_request: Invalid current state: " + current_state;
            Logger::error(msg.c_str());
            return false;
        }

        if (it->second.find(new_state) == it->second.end())
        {
            std::string msg = "forensic_request: Illegal transition from " + current_state +
                              " to " + new_state;
            Logger::debug(msg.c_str());
            return false;
        }

        return true;
    }

    bool ForensicRequest::requestForensicExamination(const SessionContext &session,
                                                     int case_id,
                                                     const std::string &lab_name,
                                                     const std::string &examiner_name,
                                                     ExaminationPurpose examination_purpose,
                                                     const std::string &purpose_description,
                                                     const std::string &report_expected_date,
                                                     int &out_request_id,
                                                     ResultCode &out_code)
    {
        // Pre-flight authorization
        if (!session.isValid)
        {
            out_code = ResultCode::SESSION_EXPIRED;
            Logger::debug("forensic_request: Session invalid");
            return false;
        }

        // Check officer duty status via S1 bridge
        bool is_active = false;
        ResultCode duty_check = integration::S1Bridge::getOfficerDutyStatus(session.officerId, is_active);
        if (duty_check != ResultCode::OK || !is_active)
        {
            out_code = ResultCode::DUTY_INACTIVE;
            Logger::debug("forensic_request: Officer not on active duty");
            return false;
        }

        // Check jurisdiction via S2 bridge
        out_code = integration::S2Bridge::validateCaseOwnership(case_id, session.stationId);
        if (out_code != ResultCode::OK)
        {
            Logger::debug("forensic_request: Case ownership validation failed");
            return false;
        }

        // Check policy engine for rank authorization
        ResultCode policy_code;
        if (!security::PolicyEngine::getInstance().evaluate(
                "FORENSIC_REQUEST",
                session.rank,
                "Forensic examination request",
                policy_code))
        {
            out_code = policy_code;
            Logger::debug("forensic_request: Policy engine denied request");
            return false;
        }

        // Insert forensic request
        ResultCode insert_result = ForensicRepository::insertRequest(
            case_id, lab_name, examiner_name, examination_purpose,
            purpose_description, report_expected_date, session.officerId, out_request_id);

        if (insert_result != ResultCode::OK)
        {
            out_code = insert_result;
            Logger::error("forensic_request: Failed to insert forensic request");
            return false;
        }

        // Notify audit bridge
        std::stringstream msg;
        msg << "Forensic examination requested - " << examiner_name << " at " << lab_name;
        integration::AuditBridge::getInstance().log(
            "INSERT INTO subsystem3.Forensic_Lab_Requests",
            AuditedTable::EVIDENCE, // Forensic requests are evidence-related
            out_request_id,
            msg.str());

        out_code = ResultCode::OK;
        Logger::info("forensic_request: Forensic examination request created");
        return true;
    }

    bool ForensicRequest::submitToLab(const SessionContext &session,
                                      int request_id,
                                      const std::string &received_date,
                                      ResultCode &out_code)
    {
        // Query request to get current status
        std::vector<ForensicLabRequest> requests;
        ResultCode query_result = ForensicRepository::getRequestsByCase(0, requests);

        // Simplified: get request status from DB
        std::stringstream query;
        query << "SELECT request_status FROM subsystem3.Forensic_Lab_Requests WHERE request_id = "
              << request_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("forensic_request: Request not found");
            return false;
        }

        std::string current_status = results[0][0];

        // State transition validation: REQUESTED → RECEIVED_BY_LAB
        if (!isValidTransition(current_status, "RECEIVED_BY_LAB"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("forensic_request: Invalid state transition");
            return false;
        }

        // Update request status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.Forensic_Lab_Requests SET "
                     << "request_status = 'RECEIVED_BY_LAB', "
                     << "received_by_lab_date = '" << received_date << "', "
                     << "updated_at = now() WHERE request_id = " << request_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("forensic_request: Failed to update status");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::EVIDENCE,
            request_id,
            "Request received by lab");

        out_code = ResultCode::OK;
        Logger::info("forensic_request: Request submitted to lab");
        return true;
    }

    bool ForensicRequest::recordExaminationStart(const SessionContext &session,
                                                 int request_id,
                                                 ResultCode &out_code)
    {
        // Query request status
        std::stringstream query;
        query << "SELECT request_status FROM subsystem3.Forensic_Lab_Requests WHERE request_id = "
              << request_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            return false;
        }

        std::string current_status = results[0][0];

        // State transition: RECEIVED_BY_LAB → UNDER_EXAMINATION
        if (!isValidTransition(current_status, "UNDER_EXAMINATION"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("forensic_request: Invalid state transition");
            return false;
        }

        // Update status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.Forensic_Lab_Requests SET "
                     << "request_status = 'UNDER_EXAMINATION', updated_at = now() "
                     << "WHERE request_id = " << request_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            return false;
        }

        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::EVIDENCE,
            request_id,
            "Examination started");

        out_code = ResultCode::OK;
        Logger::info("forensic_request: Examination started");
        return true;
    }

    bool ForensicRequest::recordFindings(const SessionContext &session,
                                         int request_id,
                                         const std::string &findings,
                                         const std::string &report_file_path,
                                         ResultCode &out_code)
    {
        // Query request to validate timeline
        std::stringstream query;
        query << "SELECT request_status, received_by_lab_date FROM subsystem3.Forensic_Lab_Requests "
              << "WHERE request_id = " << request_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            return false;
        }

        std::string current_status = results[0][0];
        std::string received_date_str = results[0][1];

        // State transition: UNDER_EXAMINATION → REPORT_READY
        if (!isValidTransition(current_status, "REPORT_READY"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("forensic_request: Invalid state transition");
            return false;
        }

        // Validate timeline: report_generated >= received_date
        time_t now = std::time(nullptr);
        time_t received_time = std::stol(received_date_str);

        if (now < received_time)
        {
            out_code = ResultCode::INVALID_INPUT;
            Logger::error("forensic_request: Report date before received date");
            return false;
        }

        // Validate findings not empty
        if (findings.empty())
        {
            out_code = ResultCode::INVALID_INPUT;
            Logger::error("forensic_request: Findings cannot be empty");
            return false;
        }

        // Update status with findings
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.Forensic_Lab_Requests SET "
                     << "request_status = 'REPORT_READY', "
                     << "findings = '" << findings << "', "
                     << "report_file_path = '" << report_file_path << "', "
                     << "updated_at = now() WHERE request_id = " << request_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            return false;
        }

        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::EVIDENCE,
            request_id,
            "Findings recorded");

        out_code = ResultCode::OK;
        Logger::info("forensic_request: Findings recorded");
        return true;
    }

    bool ForensicRequest::deliverReport(const SessionContext &session,
                                        int request_id,
                                        const std::string &delivered_date,
                                        ResultCode &out_code)
    {
        // Query request status
        std::stringstream query;
        query << "SELECT request_status FROM subsystem3.Forensic_Lab_Requests WHERE request_id = "
              << request_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            return false;
        }

        std::string current_status = results[0][0];

        // State transition: REPORT_READY → REPORT_DELIVERED
        if (!isValidTransition(current_status, "REPORT_DELIVERED"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("forensic_request: Invalid state transition");
            return false;
        }

        // Update status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.Forensic_Lab_Requests SET "
                     << "request_status = 'REPORT_DELIVERED', "
                     << "report_delivered_date = '" << delivered_date << "', "
                     << "updated_at = now() WHERE request_id = " << request_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            return false;
        }

        // Trigger automatically updates evidence status to PRODUCED_IN_COURT
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::EVIDENCE,
            request_id,
            "Report delivered");

        out_code = ResultCode::OK;
        Logger::info("forensic_request: Report delivered");
        return true;
    }

    bool ForensicRequest::contestReport(const SessionContext &session,
                                        int request_id,
                                        const std::string &contest_reason,
                                        ResultCode &out_code)
    {
        // Query request status
        std::stringstream query;
        query << "SELECT request_status FROM subsystem3.Forensic_Lab_Requests WHERE request_id = "
              << request_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            return false;
        }

        std::string current_status = results[0][0];

        // State transition: REPORT_DELIVERED → CONTESTED
        if (!isValidTransition(current_status, "CONTESTED"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("forensic_request: Invalid state transition");
            return false;
        }

        // Update status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.Forensic_Lab_Requests SET "
                     << "request_status = 'CONTESTED', "
                     << "is_contested = true, "
                     << "contest_reason = '" << contest_reason << "', "
                     << "contested_by = " << session.officerId << ", "
                     << "contested_at = now(), updated_at = now() "
                     << "WHERE request_id = " << request_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            return false;
        }

        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::EVIDENCE,
            request_id,
            "Report contested: " + contest_reason);

        out_code = ResultCode::OK;
        Logger::info("forensic_request: Report contested");
        return true;
    }

    bool ForensicRequest::linkEvidence(const SessionContext &session,
                                       int request_id,
                                       int evidence_id,
                                       const std::string &notes,
                                       ResultCode &out_code)
    {
        // Check evidence admissibility
        if (!legal::EvidenceRules::isAdmissible(evidence_id, out_code))
        {
            Logger::error("forensic_request: Evidence not admissible");
            return false;
        }

        // Get case_id from request to validate evidence ownership
        std::stringstream query;
        query << "SELECT case_id FROM subsystem3.Forensic_Lab_Requests WHERE request_id = "
              << request_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("forensic_request: Request not found");
            return false;
        }

        int case_id = std::stoi(results[0][0]);

        // Validate evidence ownership
        if (!legal::EvidenceRules::validateEvidenceOwnership(evidence_id, case_id, out_code))
        {
            Logger::error("forensic_request: Evidence ownership validation failed");
            return false;
        }

        // Link evidence to request
        ResultCode link_result = ForensicRepository::insertEvidenceLink(request_id, evidence_id, notes);

        if (link_result != ResultCode::OK)
        {
            out_code = link_result;
            Logger::error("forensic_request: Failed to link evidence");
            return false;
        }

        // Notify S2 bridge of forensic submission
        integration::S2Bridge::notifyForensicSubmission(evidence_id, request_id);

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            "INSERT INTO subsystem3.Forensic_Request_Evidence",
            AuditedTable::EVIDENCE,
            evidence_id,
            "Evidence linked to forensic request");

        out_code = ResultCode::OK;
        Logger::info("forensic_request: Evidence linked to request");
        return true;
    }

} // namespace forensic