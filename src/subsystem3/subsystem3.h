#pragma once

/**
 * @file subsystem3.h
 * @brief Public API facade for Subsystem 3 (Security & Enforcement).
 *
 * This is the only header that the API gateway / router should include for S3.
 *
 * Subsystem 3 modules:
 *   - Audit & Compliance (read-only, dedicated connection, singleton)
 *   - Enforcement (Warrants / Arrests / Bail) — stateless managers
 *   - Forensic & Lab — token-authenticated manager + repository layer
 *
 * Design pattern: Facade
 *   This wraps internal managers and presents a stable S3 API surface.
 *
 * Notes:
 *   - AuditManager has its own lifecycle: init/shutdown methods are exposed here.
 *   - Enforcement operations require the caller to provide a PGconn* and a
 *     JusticeFlow::SessionContext (already validated by the auth layer).
 *   - ForensicManager performs token validation itself (it obtains its own conn
 *     via IpcManager), so its methods take a session token string.
 */

#include <vector>
#include <ctime>
#include <cstdint>

#include <postgresql/libpq-fe.h>

#include "common/constants.h"
#include "common/common.h"

// --- Subsystem 3 internals (facade targets) ---
#include "audit/include/audit_manager.h"
#include "enforcement/include/warrant_manager.h"
#include "enforcement/include/arrest_manager.h"
#include "enforcement/include/bail_manager.h"
#include "forensic/include/forensic_manager.h"

namespace subsystem3
{
    class Subsystem3
    {
    public:
        // ============================================================
        // Audit (lifecycle + queries)
        // ============================================================

        /**
         * @brief Initialise the dedicated read-only audit connection.
         * Must be called once during process startup.
         */
        static JusticeFlow::ResultCode initAudit(const char *conninfo);

        /**
         * @brief Shutdown the dedicated audit connection (clean shutdown).
         */
        static void shutdownAudit();

        static JusticeFlow::ResultCode getAuditChangeHistory(
            int case_id,
            std::vector<audit::AuditRecord> &out);

        static JusticeFlow::ResultCode getAuditOfficerActions(
            int officer_id,
            time_t from,
            time_t to,
            std::vector<audit::AuditRecord> &out);

        static JusticeFlow::ResultCode getAuditTableChanges(
            const char *table_name,
            int record_id,
            std::vector<audit::AuditRecord> &out);

        static JusticeFlow::ResultCode auditQueryByTimeWindow(
            time_t from,
            time_t to,
            std::vector<audit::AuditRecord> &out);

        static JusticeFlow::ResultCode detectSuspiciousActivity(
            int station_id,
            std::vector<audit::AuditRecord> &out);

        // ============================================================
        // Enforcement: Warrants
        // ============================================================

        static bool requestWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *accused_cnic,
            JusticeFlow::WarrantType warrant_type,
            const char *magistrate_name,
            const char *issuing_court,
            const char *valid_until,    // "YYYY-MM-DD"
            const char *target_address, // optional (SEARCH)
            int &out_warrant_id,
            JusticeFlow::ResultCode &out_code);

        static bool executeWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int warrant_id,
            JusticeFlow::ResultCode &out_code);

        static bool cancelWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int warrant_id,
            const char *cancellation_reason,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getWarrantsByCase(
            PGconn *conn,
            int case_id,
            std::vector<enforcement::WarrantRecord> &out);

        static JusticeFlow::ResultCode getActiveWarrants(
            PGconn *conn,
            int station_id,
            std::vector<enforcement::WarrantRecord> &out);

        // ============================================================
        // Enforcement: Arrests
        // ============================================================

        static bool recordArrest(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *accused_cnic,
            const char *arrest_location,
            int warrant_id, // -1 / 0 indicates warrantless
            int &out_arrest_id,
            JusticeFlow::ResultCode &out_code);

        static bool updateCustodyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            JusticeFlow::CustodyStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code);

        static bool markArrestAsDisputed(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            const char *dispute_reason,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getArrestsByCase(
            PGconn *conn,
            int case_id,
            std::vector<enforcement::ArrestRecord> &out);

        // ============================================================
        // Enforcement: Bail
        // ============================================================

        static bool recordBail(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            JusticeFlow::BailType bail_type,
            uint64_t bail_amount_paise,
            const char *court_name,
            const char *magistrate_name,
            const char *valid_until, // ISO date or NULL/"" for no expiry
            const char *surety_name,
            const char *surety_cnic,
            const char *surety_contact,
            int &out_bail_id,
            JusticeFlow::ResultCode &out_code);

        static bool revokeBail(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int bail_id,
            const char *revocation_reason,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getBailByArrest(
            PGconn *conn,
            int arrest_id,
            enforcement::BailRecord &out);

        // ============================================================
        // Forensic & Lab (token-authenticated facade)
        // ============================================================

        static JusticeFlow::ResultCode createForensicRequest(
            const char *token,
            int case_id,
            const char *examination_purpose,
            const char *purpose_description,
            const char *lab_name,
            const char *examiner_name,
            int &out_request_id);

        static JusticeFlow::ResultCode linkEvidence(
            const char *token,
            int request_id,
            int evidence_id,
            const char *notes);

        static JusticeFlow::ResultCode recordLabReceipt(
            const char *token,
            int request_id,
            const char *received_date);

        static JusticeFlow::ResultCode recordExaminationStart(
            const char *token,
            int request_id);

        static JusticeFlow::ResultCode recordFindings(
            const char *token,
            int request_id,
            const char *findings,
            const char *report_file_path,
            const char *delivery_date);

        static JusticeFlow::ResultCode recordAmendment(
            const char *token,
            int request_id,
            const char *amended_findings);

        static JusticeFlow::ResultCode contestReport(
            const char *token,
            int request_id,
            const char *contest_reason);

        static JusticeFlow::ResultCode getForensicRequestsByCase(
            const char *token,
            int case_id,
            std::vector<forensic::ForensicRecord> &out);

        static JusticeFlow::ResultCode getPendingForensicRequests(
            const char *token,
            int station_id,
            std::vector<forensic::ForensicRecord> &out);

        static JusticeFlow::ResultCode getEvidenceByForensicRequest(
            const char *token,
            int request_id,
            std::vector<forensic::EvidenceRef> &out);
    };

} // namespace subsystem3