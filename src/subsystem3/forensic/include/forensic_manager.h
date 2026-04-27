#pragma once

// ============================================================================
// forensic_manager.h  —  Module 3: Forensic & Lab
// ============================================================================
//
// Owns the forensic request state machine and all business operations.
// ForensicRepository owns all SQL. ForensicManager never writes a query.
//
// State Machine (strictly linear with one branch):
//
//   REQUESTED
//       │  recordLabReceipt()
//       ▼
//   RECEIVED_BY_LAB
//       │  recordExaminationStart()
//       ▼
//   UNDER_EXAMINATION
//       │  recordFindings()
//       ▼
//   REPORT_READY
//       │  deliverReport()
//       ▼
//   REPORT_DELIVERED ──→ CONTESTED   (contestReport())
//
//   No state skipping. CONTESTED is terminal — it cannot transition further
//   without a new forensic request being created.
//
// Every write operation follows this chain (short-circuit on first failure):
//   1. AuthManager::validateToken(token)    — session must be valid
//   2. AuthManager::isDutyActive(officer)   — officer must be on duty
//   3. AuthManager::validateRank(officer)   — where INSPECTOR+ is required
//   4. _validateTransition(from, to)        — state machine enforcement
//   5. Input validation (dates, non-empty)  — domain rules
//   6. ForensicRepository::xyz()            — DB write (triggers fire here)
//
// The DB trigger is the Observer for evidence status sync:
//   • Trigger 1 (INSERT on Forensic_Request_Evidence) → SENT_TO_LAB
//   • Trigger 2 (UPDATE Forensic_Lab_Requests to REPORT_DELIVERED)
//              → RETURNED_FROM_LAB for all linked evidence
// This module NEVER updates Evidence.evidence_status directly.
//
// Design Patterns:
//   State         — _validateTransition() enforces legal state progression
//   Observer      — DB triggers fire automatically (decoupled from C++)
//   Repository    — all SQL delegated to ForensicRepository
//   Command       — each operation is a discrete audited command
//
// Thread Safety: All methods are stateless. Connection managed by IpcManager.
// Dependencies: forensic_repository.h, shr_infra/auth/auth_module.h,
//               common/constants.h, common/common.h
// ============================================================================

#include <vector>
#include <ctime>
#include "common/constants.h"
#include "common/common.h"
#include "forensic/include/forensic_repository.h"

namespace forensic
{

    class ForensicManager
    {
    public:
        // ================================================================
        // WRITE OPERATIONS  (6 total)
        // ================================================================

        // ----------------------------------------------------------------
        // createForensicRequest
        // Creates a new forensic request in REQUESTED state.
        //
        // Pre-flight chain:
        //   1. validateToken(token)           — session valid
        //   2. isDutyActive(officer_id)       — officer on duty
        //   3. validateRank(INSPECTOR)        — INSPECTOR+ required
        //   4. Case must be REGISTERED, UNDER_INVESTIGATION, or
        //      EVIDENCE_COLLECTED (evidence must exist to send to lab)
        //   5. examination_purpose must be a known value
        //   6. lab_name, examiner_name must be non-empty
        //
        // On success: delegates INSERT to ForensicRepository::insertRequest().
        // Audit trigger fires automatically on the INSERT.
        //
        // @param token             Session token from caller
        // @param case_id           Case requiring forensic examination
        // @param examination_purpose  e.g. DNA_ANALYSIS, FINGERPRINT_ANALYSIS
        // @param purpose_description  Detailed goals (non-empty)
        // @param lab_name          Receiving lab name
        // @param examiner_name     Lead examiner
        // @param out_request_id    Populated with new request_id on success
        // @return OK | SESSION_EXPIRED | DUTY_INACTIVE | RANK_INSUFFICIENT |
        //         NOT_FOUND | INVALID_STATE | INVALID_INPUT | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode createForensicRequest(
            const char *token,
            int case_id,
            const char *examination_purpose,
            const char *purpose_description,
            const char *lab_name,
            const char *examiner_name,
            int &out_request_id);

        // ----------------------------------------------------------------
        // linkEvidence
        // Links an existing evidence item to a forensic request.
        // Requires the request to be in REQUESTED or RECEIVED_BY_LAB state
        // (evidence can be added until examination actually starts).
        //
        // Pre-flight chain:
        //   1. validateToken(token)
        //   2. isDutyActive(officer_id from session)
        //   3. Request must be in REQUESTED or RECEIVED_BY_LAB state
        //   4. Evidence must not be is_deleted
        //   5. Evidence status must be RECEIVED, SEALED, or RETURNED_FROM_LAB
        //      (already-sent evidence cannot be linked again)
        //   6. Evidence must belong to the same case as the request
        //      (chain-of-custody violation otherwise)
        //
        // On success: delegates INSERT to ForensicRepository::insertEvidenceLink().
        // DB Trigger 1 fires: evidence_status → SENT_TO_LAB (automatically).
        //
        // @param token        Session token
        // @param request_id   Target forensic request
        // @param evidence_id  Evidence item to link
        // @param notes        Optional submission notes (may be empty)
        // @return OK | SESSION_EXPIRED | DUTY_INACTIVE | NOT_FOUND |
        //         INVALID_STATE | INVALID_INPUT | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode linkEvidence(
            const char *token,
            int request_id,
            int evidence_id,
            const char *notes);

        // ----------------------------------------------------------------
        // recordLabReceipt
        // Transition: REQUESTED → RECEIVED_BY_LAB
        // Records the physical date the lab received the evidence package.
        //
        // Pre-flight chain:
        //   1. validateToken(token)
        //   2. isDutyActive(officer_id from session)
        //   3. _validateTransition("REQUESTED", "RECEIVED_BY_LAB")
        //   4. received_date must not be in the future
        //      (lab cannot receive before today)
        //
        // @param token          Session token
        // @param request_id     Request being received
        // @param received_date  Date lab received evidence (YYYY-MM-DD)
        // @return OK | SESSION_EXPIRED | DUTY_INACTIVE | NOT_FOUND |
        //         INVALID_STATE | INVALID_INPUT | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode recordLabReceipt(
            const char *token,
            int request_id,
            const char *received_date);

        // ----------------------------------------------------------------
        // recordExaminationStart
        // Transition: RECEIVED_BY_LAB → UNDER_EXAMINATION
        // Records that the lab has begun active examination of the evidence.
        // Timestamp is captured as NOW() inside the repository.
        //
        // Pre-flight chain:
        //   1. validateToken(token)
        //   2. isDutyActive(officer_id from session)
        //   3. _validateTransition("RECEIVED_BY_LAB", "UNDER_EXAMINATION")
        //
        // @param token       Session token
        // @param request_id  Request being examined
        // @return OK | SESSION_EXPIRED | DUTY_INACTIVE | NOT_FOUND |
        //         INVALID_STATE | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode recordExaminationStart(
            const char *token,
            int request_id);

        // ----------------------------------------------------------------
        // recordFindings
        // Two-step transition: UNDER_EXAMINATION → REPORT_READY → REPORT_DELIVERED
        //
        // Step 1 (REPORT_READY):   findings and file_path are written.
        // Step 2 (REPORT_DELIVERED): delivery_date is written and status advanced.
        // Both steps execute atomically via a single transaction in the repository.
        //
        // DB Trigger 2 fires on REPORT_DELIVERED update:
        //   → evidence_status = 'RETURNED_FROM_LAB' for all linked evidence.
        //   This module NEVER updates Evidence directly.
        //
        // Pre-flight chain:
        //   1. validateToken(token)
        //   2. isDutyActive(officer_id from session)
        //   3. _validateTransition("UNDER_EXAMINATION", "REPORT_READY")
        //      (REPORT_DELIVERED follows in same call)
        //   4. findings must be non-empty
        //   5. delivery_date must be >= receipt_date (fetched from DB)
        //      Prevents recording delivery before the lab even received evidence.
        //   6. report_file_path must be non-empty
        //
        // @param token            Session token
        // @param request_id       Request with completed examination
        // @param findings         Full text of lab analysis results
        // @param report_file_path Storage path of signed report file
        // @param delivery_date    Date report delivered (YYYY-MM-DD)
        // @return OK | SESSION_EXPIRED | DUTY_INACTIVE | NOT_FOUND |
        //         INVALID_STATE | INVALID_INPUT | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode recordFindings(
            const char *token,
            int request_id,
            const char *findings,
            const char *report_file_path,
            const char *delivery_date);

        // ----------------------------------------------------------------
        // recordAmendment
        // Updates the findings text on a REPORT_DELIVERED or CONTESTED request.
        // Does NOT change the status. Used when INSPECTOR+ officer corrects
        // a clerical error in a delivered report.
        //
        // Pre-flight chain:
        //   1. validateToken(token)
        //   2. isDutyActive(officer_id from session)
        //   3. validateRank(INSPECTOR)  — INSPECTOR+ required
        //   4. Status must be REPORT_DELIVERED or CONTESTED
        //      (cannot amend before delivery)
        //   5. amended_findings must be non-empty
        //
        // @param token             Session token
        // @param request_id        Request being amended
        // @param amended_findings  Corrected findings text
        // @return OK | SESSION_EXPIRED | DUTY_INACTIVE | RANK_INSUFFICIENT |
        //         NOT_FOUND | INVALID_STATE | INVALID_INPUT | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode recordAmendment(
            const char *token,
            int request_id,
            const char *amended_findings);

        // ----------------------------------------------------------------
        // contestReport
        // Transition: REPORT_DELIVERED → CONTESTED
        // Initiates a legal challenge of the forensic findings.
        // CONTESTED is a terminal state — a new request must be created
        // to restart the pipeline after contestation.
        //
        // Pre-flight chain:
        //   1. validateToken(token)
        //   2. isDutyActive(officer_id from session)
        //   3. _validateTransition("REPORT_DELIVERED", "CONTESTED")
        //   4. contest_reason must be non-empty
        //
        // @param token          Session token
        // @param request_id     Report being contested
        // @param contest_reason Legal basis for challenge
        // @return OK | SESSION_EXPIRED | DUTY_INACTIVE | NOT_FOUND |
        //         INVALID_STATE | INVALID_INPUT | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode contestReport(
            const char *token,
            int request_id,
            const char *contest_reason);

        // ================================================================
        // QUERY OPERATIONS  (3 total)
        // ================================================================

        // ----------------------------------------------------------------
        // getRequestsByCase
        // Returns all forensic requests for a case, all statuses, DESC order.
        // Requires valid session token — no rank restriction.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode getRequestsByCase(
            const char *token,
            int case_id,
            std::vector<ForensicRecord> &out);

        // ----------------------------------------------------------------
        // getPendingRequests
        // Returns all requests at a station not yet REPORT_DELIVERED.
        // Statuses included: REQUESTED, RECEIVED_BY_LAB, UNDER_EXAMINATION,
        // REPORT_READY.
        // Requires valid session token — no rank restriction.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode getPendingRequests(
            const char *token,
            int station_id,
            std::vector<ForensicRecord> &out);

        // ----------------------------------------------------------------
        // getEvidenceByRequest
        // Returns all evidence items linked to a forensic request.
        // Requires valid session token — no rank restriction.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode getEvidenceByRequest(
            const char *token,
            int request_id,
            std::vector<EvidenceRef> &out);

    private:
        // ----------------------------------------------------------------
        // _validateTransition
        // Enforces the legal state machine.
        // Returns true only if from → to is a defined legal edge.
        //
        // Legal edges:
        //   REQUESTED        → RECEIVED_BY_LAB
        //   RECEIVED_BY_LAB  → UNDER_EXAMINATION
        //   UNDER_EXAMINATION→ REPORT_READY
        //   REPORT_READY     → REPORT_DELIVERED
        //   REPORT_DELIVERED → CONTESTED
        //
        // All other transitions return false. No skipping states.
        // CONTESTED is terminal — nothing transitions out of it.
        // ----------------------------------------------------------------
        static bool _validateTransition(const char *from, const char *to);

        // ----------------------------------------------------------------
        // _knownPurpose
        // Returns true if examination_purpose is one of the known values:
        //   DNA_ANALYSIS | FINGERPRINT_ANALYSIS | TOXICOLOGY | BALLISTICS |
        //   DIGITAL_FORENSICS | DOCUMENT_EXAMINATION | OTHER
        // ----------------------------------------------------------------
        static bool _knownPurpose(const char *purpose);

        // ----------------------------------------------------------------
        // _parseDate
        // Parses a "YYYY-MM-DD" string into a time_t (UTC midnight).
        // Returns (time_t)-1 on parse failure.
        // ----------------------------------------------------------------
        static time_t _parseDate(const char *date_str);
    };

} // namespace forensic