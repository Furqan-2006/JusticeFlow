#pragma once

// ============================================================================
// forensic_repository.h  —  Module 3: Forensic & Lab
// ============================================================================
//
// Repository pattern: ALL SQL for the forensic module lives here.
// ForensicManager never writes a query. ForensicRepository never owns a
// PGconn* — it receives one as a parameter on every call.
//
// Rules enforced by this layer:
//   • Every query uses PQexecParams ($N placeholders) — zero string
//     interpolation, zero SQL injection risk.
//   • PGresult* lifetime is managed internally. Callers never call PQclear.
//   • No std::string in ForensicRecord or EvidenceRef — fixed-size char
//     arrays so structs are safe to copy across module boundaries.
//   • Evidence status (SENT_TO_LAB / RETURNED_FROM_LAB) is NEVER updated
//     directly by this layer. Those updates are trigger-driven:
//       Trigger 1: fires on Forensic_Request_Evidence INSERT → SENT_TO_LAB
//       Trigger 2: fires on Forensic_Lab_Requests UPDATE to REPORT_DELIVERED
//                  → RETURNED_FROM_LAB for all linked evidence
//
// Design Pattern: Repository (encapsulates all SQL behind a typed interface)
//
// Dependencies: libpq-fe.h, common/constants.h
// ============================================================================

#include <vector>
#include <ctime>
#include "common/constants.h"

// Forward-declare libpq type to avoid pulling in libpq-fe.h in headers
struct pg_conn;
typedef struct pg_conn PGconn;

namespace forensic
{

    // ============================================================================
    // ForensicRecord  —  mirrors public.Forensic_Lab_Requests columns
    // Fixed-size char arrays throughout. No std::string.
    // ============================================================================
    struct ForensicRecord
    {
        int request_id;
        int case_id;
        char request_number[32]; // e.g. "FR-1745600000-42"
        char request_status[24]; // REQUESTED | RECEIVED_BY_LAB |
                                 // UNDER_EXAMINATION | REPORT_READY |
                                 // REPORT_DELIVERED  | CONTESTED
        char lab_name[128];
        char examiner_name[64];
        char examination_purpose[32]; // DNA_ANALYSIS | FINGERPRINT_ANALYSIS |
                                      // TOXICOLOGY | BALLISTICS | DIGITAL_FORENSICS |
                                      // DOCUMENT_EXAMINATION | OTHER
        char purpose_description[512];
        int authorized_by; // officer_id
        time_t created_at;
        time_t received_by_lab_date;   // 0 if not yet received
        time_t examination_start_date; // 0 if not yet started
        char findings[4096];           // lab analysis results
        char report_file_path[512];    // path to stored report file
        time_t report_ready_date;      // 0 if not ready
        time_t report_delivered_date;  // 0 if not delivered
        int is_contested;              // 0 = false, 1 = true
        char contest_reason[512];
        int contested_by; // officer_id, 0 if not contested
        time_t contested_at;
        time_t updated_at;
    };

    // ============================================================================
    // EvidenceRef  —  a lightweight reference to an evidence item linked to a
    // forensic request. Full evidence details live in public.Evidence (S2).
    // ============================================================================
    struct EvidenceRef
    {
        int evidence_id;
        int request_id;
        char evidence_number[32];
        char evidence_type[24];   // PHYSICAL | DIGITAL | TESTIMONIAL |
                                  // FORENSIC | DOCUMENTARY
        char evidence_status[32]; // current status from public.Evidence
        char description[256];
        char notes[512]; // notes recorded when linking to request
        time_t linked_at;
    };

    // ============================================================================
    // ForensicRepository  —  all SQL for forensic module
    // ============================================================================
    class ForensicRepository
    {
    public:
        // ----------------------------------------------------------------
        // insertRequest
        // Inserts a new row in public.Forensic_Lab_Requests with status
        // 'REQUESTED'. Generates a unique request_number internally.
        // Populates out_request_id with the RETURNING request_id value.
        //
        // @param conn               Active PGconn* (managed by caller)
        // @param case_id            Case this request belongs to
        // @param examination_purpose  Enum string: DNA_ANALYSIS etc.
        // @param purpose_description  Free-form description of goals
        // @param lab_name           Target forensic lab
        // @param examiner_name      Lead examiner
        // @param authorized_by      officer_id authorising the request
        // @param out_request_id     Set to new request_id on success
        // @return OK | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode insertRequest(
            PGconn *conn,
            int case_id,
            const char *examination_purpose,
            const char *purpose_description,
            const char *lab_name,
            const char *examiner_name,
            int authorized_by,
            int &out_request_id);

        // ----------------------------------------------------------------
        // insertEvidenceLink
        // Inserts a row in public.Forensic_Request_Evidence.
        // The DB trigger fires automatically on this INSERT and sets
        // public.Evidence.evidence_status = 'SENT_TO_LAB' for evidence_id.
        // This layer never updates Evidence directly.
        //
        // @param conn         Active PGconn*
        // @param request_id   The forensic request to link evidence to
        // @param evidence_id  The evidence item being submitted
        // @param notes        Optional notes about this submission
        // @return OK | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode insertEvidenceLink(
            PGconn *conn,
            int request_id,
            int evidence_id,
            const char *notes);

        // ----------------------------------------------------------------
        // updateStatus
        // Updates request_status on a Forensic_Lab_Requests row.
        // When new_status = 'REPORT_DELIVERED', the DB trigger fires and
        // sets evidence_status = 'RETURNED_FROM_LAB' for all linked evidence.
        // This layer never touches Evidence directly.
        //
        // Only the status column is changed here. Date fields (received_by_lab_date,
        // examination_start_date, report_ready_date) are updated by their
        // dedicated methods below.
        //
        // @param conn        Active PGconn*
        // @param request_id  Request being transitioned
        // @param new_status  Target status string (must match DB CHECK constraint)
        // @return OK | NOT_FOUND | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode updateStatus(
            PGconn *conn,
            int request_id,
            const char *new_status);

        // ----------------------------------------------------------------
        // updateReceivedDate
        // Sets received_by_lab_date; called when REQUESTED → RECEIVED_BY_LAB.
        // Status transition is handled separately by updateStatus.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode updateReceivedDate(
            PGconn *conn,
            int request_id,
            const char *received_date); // "YYYY-MM-DD"

        // ----------------------------------------------------------------
        // updateExaminationStartDate
        // Sets examination_start_date; called when RECEIVED_BY_LAB → UNDER_EXAMINATION.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode updateExaminationStartDate(
            PGconn *conn,
            int request_id); // uses NOW() internally

        // ----------------------------------------------------------------
        // updateFindings
        // Sets findings, report_file_path, report_ready_date, and
        // report_delivered_date atomically.
        // Called when the state machine reaches REPORT_READY and then
        // REPORT_DELIVERED in a two-step sequence.
        //
        // @param conn                Active PGconn*
        // @param request_id          Request being updated
        // @param findings            Full text of lab analysis
        // @param report_file_path    Storage path of the report file
        // @param delivery_date       Date report delivered (YYYY-MM-DD)
        // @return OK | NOT_FOUND | DB_ERROR
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode updateFindings(
            PGconn *conn,
            int request_id,
            const char *findings,
            const char *report_file_path,
            const char *delivery_date);

        // ----------------------------------------------------------------
        // updateAmendment
        // Updates the findings text only, without changing the status.
        // Used by recordAmendment when INSPECTOR+ officer corrects a report.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode updateAmendment(
            PGconn *conn,
            int request_id,
            const char *amended_findings);

        // ----------------------------------------------------------------
        // updateContest
        // Sets is_contested, contest_reason, contested_by, contested_at.
        // Called when REPORT_DELIVERED → CONTESTED.
        // Status transition handled separately by updateStatus.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode updateContest(
            PGconn *conn,
            int request_id,
            const char *contest_reason,
            int contested_by);

        // ----------------------------------------------------------------
        // selectByCase
        // Returns all ForensicRecord rows for a given case_id,
        // ordered by created_at DESC.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode selectByCase(
            PGconn *conn,
            int case_id,
            std::vector<ForensicRecord> &out);

        // ----------------------------------------------------------------
        // selectPending
        // Returns all requests at a station that are not yet REPORT_DELIVERED
        // or CONTESTED. Uses a JOIN to Cases to filter by station_id.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode selectPending(
            PGconn *conn,
            int station_id,
            std::vector<ForensicRecord> &out);

        // ----------------------------------------------------------------
        // selectEvidenceByRequest
        // JOINs Forensic_Request_Evidence with public.Evidence to return
        // full EvidenceRef records for a given request_id.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode selectEvidenceByRequest(
            PGconn *conn,
            int request_id,
            std::vector<EvidenceRef> &out);

        // ----------------------------------------------------------------
        // fetchCurrentStatus
        // Lightweight helper used by ForensicManager to read the current
        // request_status and receipt_date before a transition.
        // Populates out_status (max 23 chars + null) and out_receipt_epoch.
        // ----------------------------------------------------------------
        static JusticeFlow::ResultCode fetchCurrentStatus(
            PGconn *conn,
            int request_id,
            char out_status[24],
            time_t &out_receipt_epoch);

    private:
        // ----------------------------------------------------------------
        // _mapRecord  —  maps a PGresult row → ForensicRecord
        // Column order is fixed and documented in forensic_repository.cpp.
        // ----------------------------------------------------------------
        static void _mapRecord(struct pg_result *res, int row, ForensicRecord &out);

        // ----------------------------------------------------------------
        // _mapEvidenceRef  —  maps a PGresult row → EvidenceRef
        // ----------------------------------------------------------------
        static void _mapEvidenceRef(struct pg_result *res, int row, EvidenceRef &out);
    };

} // namespace forensic