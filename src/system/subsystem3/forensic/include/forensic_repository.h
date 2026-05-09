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
//
// Design Pattern: Repository (encapsulates all SQL behind a typed interface)
// ============================================================================

#include <vector>
#include <ctime>
#include "common/constants.h"

// --- FIX: Global forward declarations for libpq types ---
extern "C"
{
    struct pg_conn;
    struct pg_result;
}
typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;

namespace forensic
{

    struct ForensicRecord
    {
        int request_id;
        int case_id;
        char request_number[32];
        char request_status[24];
        char lab_name[128];
        char examiner_name[64];
        char examination_purpose[32];
        char purpose_description[512];
        int authorized_by;
        time_t created_at;
        time_t received_by_lab_date;
        time_t examination_start_date;
        char findings[4096];
        char report_file_path[512];
        time_t report_ready_date;
        time_t report_delivered_date;
        int is_contested;
        char contest_reason[512];
        int contested_by;
        time_t contested_at;
        time_t updated_at;
    };

    struct EvidenceRef
    {
        int evidence_id;
        int request_id;
        char evidence_number[32];
        char evidence_type[24];
        char evidence_status[32];
        char description[256];
        char notes[512];
        time_t linked_at;
    };

    class ForensicRepository
    {
    public:
        static JusticeFlow::ResultCode insertRequest(
            PGconn *conn,
            int case_id,
            const char *examination_purpose,
            const char *purpose_description,
            const char *lab_name,
            const char *examiner_name,
            int authorized_by,
            int &out_request_id);

        static JusticeFlow::ResultCode insertEvidenceLink(
            PGconn *conn,
            int request_id,
            int evidence_id,
            const char *notes);

        static JusticeFlow::ResultCode updateStatus(
            PGconn *conn,
            int request_id,
            const char *new_status);

        static JusticeFlow::ResultCode updateReceivedDate(
            PGconn *conn,
            int request_id,
            const char *received_date);

        static JusticeFlow::ResultCode updateExaminationStartDate(
            PGconn *conn,
            int request_id);

        static JusticeFlow::ResultCode updateFindings(
            PGconn *conn,
            int request_id,
            const char *findings,
            const char *report_file_path,
            const char *delivery_date);

        static JusticeFlow::ResultCode updateAmendment(
            PGconn *conn,
            int request_id,
            const char *amended_findings);

        static JusticeFlow::ResultCode updateContest(
            PGconn *conn,
            int request_id,
            const char *contest_reason,
            int contested_by);

        static JusticeFlow::ResultCode selectByCase(
            PGconn *conn,
            int case_id,
            std::vector<ForensicRecord> &out);

        static JusticeFlow::ResultCode selectPending(
            PGconn *conn,
            int station_id,
            std::vector<ForensicRecord> &out);

        static JusticeFlow::ResultCode selectEvidenceByRequest(
            PGconn *conn,
            int request_id,
            std::vector<EvidenceRef> &out);

        static JusticeFlow::ResultCode fetchCurrentStatus(
            PGconn *conn,
            int request_id,
            char out_status[24],
            time_t &out_receipt_epoch);

    private:
        // --- FIX: Signature updated to use global PGresult ---
        static void _mapRecord(PGresult *res, int row, ForensicRecord &out);
        static void _mapEvidenceRef(PGresult *res, int row, EvidenceRef &out);
    };

} // namespace forensic