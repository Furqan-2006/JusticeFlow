/**
 * @file subsystem3.cpp
 * @brief Subsystem 3 public facade implementation (routing to managers).
 */

#include "subsystem3/subsystem3.h"

namespace subsystem3
{
    // ============================================================
    // Audit
    // ============================================================

    JusticeFlow::ResultCode Subsystem3::initAudit(const char *conninfo)
    {
        return audit::AuditManager::getInstance().connect(conninfo);
    }

    void Subsystem3::shutdownAudit()
    {
        audit::AuditManager::getInstance().disconnect();
    }

    JusticeFlow::ResultCode Subsystem3::getAuditChangeHistory(
        int case_id,
        std::vector<audit::AuditRecord> &out)
    {
        return audit::AuditManager::getInstance().getChangeHistory(case_id, out);
    }

    JusticeFlow::ResultCode Subsystem3::getAuditOfficerActions(
        int officer_id,
        time_t from,
        time_t to,
        std::vector<audit::AuditRecord> &out)
    {
        return audit::AuditManager::getInstance().getOfficerActions(officer_id, from, to, out);
    }

    JusticeFlow::ResultCode Subsystem3::getAuditTableChanges(
        const char *table_name,
        int record_id,
        std::vector<audit::AuditRecord> &out)
    {
        return audit::AuditManager::getInstance().getTableChanges(table_name, record_id, out);
    }

    JusticeFlow::ResultCode Subsystem3::auditQueryByTimeWindow(
        time_t from,
        time_t to,
        std::vector<audit::AuditRecord> &out)
    {
        return audit::AuditManager::getInstance().queryByTimeWindow(from, to, out);
    }

    JusticeFlow::ResultCode Subsystem3::detectSuspiciousActivity(
        int station_id,
        std::vector<audit::AuditRecord> &out)
    {
        return audit::AuditManager::getInstance().detectSuspiciousActivity(station_id, out);
    }

    // ============================================================
    // Enforcement: Warrants
    // ============================================================

    bool Subsystem3::requestWarrant(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        const char *accused_cnic,
        JusticeFlow::WarrantType warrant_type,
        const char *magistrate_name,
        const char *issuing_court,
        const char *valid_until,
        const char *target_address,
        int &out_warrant_id,
        JusticeFlow::ResultCode &out_code)
    {
        return enforcement::WarrantManager::requestWarrant(
            conn, session, case_id, accused_cnic, warrant_type,
            magistrate_name, issuing_court, valid_until, target_address,
            out_warrant_id, out_code);
    }

    bool Subsystem3::executeWarrant(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int warrant_id,
        JusticeFlow::ResultCode &out_code)
    {
        return enforcement::WarrantManager::executeWarrant(conn, session, warrant_id, out_code);
    }

    bool Subsystem3::cancelWarrant(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int warrant_id,
        const char *cancellation_reason,
        JusticeFlow::ResultCode &out_code)
    {
        return enforcement::WarrantManager::cancelWarrant(
            conn, session, warrant_id, cancellation_reason, out_code);
    }

    JusticeFlow::ResultCode Subsystem3::getWarrantsByCase(
        PGconn *conn,
        int case_id,
        std::vector<enforcement::WarrantRecord> &out)
    {
        return enforcement::WarrantManager::getWarrantsByCase(conn, case_id, out);
    }

    JusticeFlow::ResultCode Subsystem3::getActiveWarrants(
        PGconn *conn,
        int station_id,
        std::vector<enforcement::WarrantRecord> &out)
    {
        return enforcement::WarrantManager::getActiveWarrants(conn, station_id, out);
    }

    // ============================================================
    // Enforcement: Arrests
    // ============================================================

    bool Subsystem3::recordArrest(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        const char *accused_cnic,
        const char *arrest_location,
        int warrant_id,
        int &out_arrest_id,
        JusticeFlow::ResultCode &out_code)
    {
        return enforcement::ArrestManager::recordArrest(
            conn, session, case_id, accused_cnic, arrest_location,
            warrant_id, out_arrest_id, out_code);
    }

    bool Subsystem3::updateCustodyStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int arrest_id,
        JusticeFlow::CustodyStatus new_status,
        const char *reason,
        JusticeFlow::ResultCode &out_code)
    {
        return enforcement::ArrestManager::updateCustodyStatus(
            conn, session, arrest_id, new_status, reason, out_code);
    }

    bool Subsystem3::markArrestAsDisputed(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int arrest_id,
        const char *dispute_reason,
        JusticeFlow::ResultCode &out_code)
    {
        return enforcement::ArrestManager::markAsDisputed(
            conn, session, arrest_id, dispute_reason, out_code);
    }

    JusticeFlow::ResultCode Subsystem3::getArrestsByCase(
        PGconn *conn,
        int case_id,
        std::vector<enforcement::ArrestRecord> &out)
    {
        return enforcement::ArrestManager::getArrestsByCase(conn, case_id, out);
    }

    // ============================================================
    // Enforcement: Bail
    // ============================================================

    bool Subsystem3::recordBail(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int arrest_id,
        JusticeFlow::BailType bail_type,
        uint64_t bail_amount_paise,
        const char *court_name,
        const char *magistrate_name,
        const char *valid_until,
        const char *surety_name,
        const char *surety_cnic,
        const char *surety_contact,
        int &out_bail_id,
        JusticeFlow::ResultCode &out_code)
    {
        return enforcement::BailManager::recordBail(
            conn, session, arrest_id, bail_type, bail_amount_paise,
            court_name, magistrate_name, valid_until,
            surety_name, surety_cnic, surety_contact,
            out_bail_id, out_code);
    }

    bool Subsystem3::revokeBail(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int bail_id,
        const char *revocation_reason,
        JusticeFlow::ResultCode &out_code)
    {
        return enforcement::BailManager::revokeBail(
            conn, session, bail_id, revocation_reason, out_code);
    }

    JusticeFlow::ResultCode Subsystem3::getBailByArrest(
        PGconn *conn,
        int arrest_id,
        enforcement::BailRecord &out)
    {
        return enforcement::BailManager::getBailByArrest(conn, arrest_id, out);
    }

    // ============================================================
    // Forensic
    // ============================================================

    JusticeFlow::ResultCode Subsystem3::createForensicRequest(
        const char *token,
        int case_id,
        const char *examination_purpose,
        const char *purpose_description,
        const char *lab_name,
        const char *examiner_name,
        int &out_request_id)
    {
        return forensic::ForensicManager::createForensicRequest(
            token, case_id, examination_purpose, purpose_description,
            lab_name, examiner_name, out_request_id);
    }

    JusticeFlow::ResultCode Subsystem3::linkEvidence(
        const char *token,
        int request_id,
        int evidence_id,
        const char *notes)
    {
        return forensic::ForensicManager::linkEvidence(token, request_id, evidence_id, notes);
    }

    JusticeFlow::ResultCode Subsystem3::recordLabReceipt(
        const char *token,
        int request_id,
        const char *received_date)
    {
        return forensic::ForensicManager::recordLabReceipt(token, request_id, received_date);
    }

    JusticeFlow::ResultCode Subsystem3::recordExaminationStart(
        const char *token,
        int request_id)
    {
        return forensic::ForensicManager::recordExaminationStart(token, request_id);
    }

    JusticeFlow::ResultCode Subsystem3::recordFindings(
        const char *token,
        int request_id,
        const char *findings,
        const char *report_file_path,
        const char *delivery_date)
    {
        return forensic::ForensicManager::recordFindings(
            token, request_id, findings, report_file_path, delivery_date);
    }

    JusticeFlow::ResultCode Subsystem3::recordAmendment(
        const char *token,
        int request_id,
        const char *amended_findings)
    {
        return forensic::ForensicManager::recordAmendment(token, request_id, amended_findings);
    }

    JusticeFlow::ResultCode Subsystem3::contestReport(
        const char *token,
        int request_id,
        const char *contest_reason)
    {
        return forensic::ForensicManager::contestReport(token, request_id, contest_reason);
    }

    JusticeFlow::ResultCode Subsystem3::getForensicRequestsByCase(
        const char *token,
        int case_id,
        std::vector<forensic::ForensicRecord> &out)
    {
        return forensic::ForensicManager::getRequestsByCase(token, case_id, out);
    }

    JusticeFlow::ResultCode Subsystem3::getPendingForensicRequests(
        const char *token,
        int station_id,
        std::vector<forensic::ForensicRecord> &out)
    {
        return forensic::ForensicManager::getPendingRequests(token, station_id, out);
    }

    JusticeFlow::ResultCode Subsystem3::getEvidenceByForensicRequest(
        const char *token,
        int request_id,
        std::vector<forensic::EvidenceRef> &out)
    {
        return forensic::ForensicManager::getEvidenceByRequest(token, request_id, out);
    }

} // namespace subsystem3