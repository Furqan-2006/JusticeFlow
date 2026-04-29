// ============================================================================
// forensic_repository.cpp  —  ForensicRepository implementation
// ============================================================================
//
// ALL SQL lives here. Zero queries exist anywhere else in the forensic module.
// All queries use PQexecParams with $N placeholders — no string interpolation.
// PGresult* is always PQclear'd internally — callers never manage it.
//
// Column order contract for ForensicRecord SELECT (used in _mapRecord):
//   0  request_id
//   1  case_id
//   2  request_number
//   3  request_status
//   4  lab_name
//   5  examiner_name
//   6  examination_purpose
//   7  purpose_description
//   8  authorized_by
//   9  created_at (epoch)
//  10  received_by_lab_date (epoch, NULL → 0)
//  11  examination_start_date (epoch, NULL → 0)
//  12  findings
//  13  report_file_path
//  14  report_ready_date (epoch, NULL → 0)
//  15  report_delivered_date (epoch, NULL → 0)
//  16  is_contested (0/1)
//  17  contest_reason
//  18  contested_by (NULL → 0)
//  19  contested_at (epoch, NULL → 0)
//  20  updated_at (epoch)
//
// Column order contract for EvidenceRef SELECT (used in _mapEvidenceRef):
//   0  fre.evidence_id
//   1  fre.request_id
//   2  e.evidence_number
//   3  e.evidence_type
//   4  e.evidence_status
//   5  e.description
//   6  fre.notes
//   7  fre.linked_at (epoch)
// ============================================================================

#include "forensic/include/forensic_repository.h"
#include "common/logger.h"
#include <libpq-fe.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace JusticeFlow;

namespace forensic
{

    // ============================================================================
    // Internal helpers
    // ============================================================================

    // Safe copy into a fixed-size buffer — truncates if source is longer.
    static void _scopy(char *dst, int dst_size, const char *src)
    {
        if (!src || src[0] == '\0')
        {
            dst[0] = '\0';
            return;
        }
        std::strncpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }

    // Parse a PostgreSQL epoch string (may be NULL/empty) into time_t.
    static time_t _epoch(const char *s)
    {
        if (!s || s[0] == '\0')
            return 0;
        return static_cast<time_t>(std::atol(s));
    }

    // ============================================================================
    // _mapRecord  —  PGresult row → ForensicRecord
    // ============================================================================
    void ForensicRepository::_mapRecord(pg_result *res, int row, ForensicRecord &r)
    {
        r = ForensicRecord{};

        r.request_id = std::atoi(PQgetvalue(res, row, 0));
        r.case_id = std::atoi(PQgetvalue(res, row, 1));
        _scopy(r.request_number, sizeof(r.request_number), PQgetvalue(res, row, 2));
        _scopy(r.request_status, sizeof(r.request_status), PQgetvalue(res, row, 3));
        _scopy(r.lab_name, sizeof(r.lab_name), PQgetvalue(res, row, 4));
        _scopy(r.examiner_name, sizeof(r.examiner_name), PQgetvalue(res, row, 5));
        _scopy(r.examination_purpose, sizeof(r.examination_purpose), PQgetvalue(res, row, 6));
        _scopy(r.purpose_description, sizeof(r.purpose_description), PQgetvalue(res, row, 7));
        r.authorized_by = std::atoi(PQgetvalue(res, row, 8));
        r.created_at = _epoch(PQgetvalue(res, row, 9));
        r.received_by_lab_date = _epoch(PQgetvalue(res, row, 10));
        r.examination_start_date = _epoch(PQgetvalue(res, row, 11));
        _scopy(r.findings, sizeof(r.findings), PQgetvalue(res, row, 12));
        _scopy(r.report_file_path, sizeof(r.report_file_path), PQgetvalue(res, row, 13));
        r.report_ready_date = _epoch(PQgetvalue(res, row, 14));
        r.report_delivered_date = _epoch(PQgetvalue(res, row, 15));
        r.is_contested = std::atoi(PQgetvalue(res, row, 16));
        _scopy(r.contest_reason, sizeof(r.contest_reason), PQgetvalue(res, row, 17));
        r.contested_by = std::atoi(PQgetvalue(res, row, 18));
        r.contested_at = _epoch(PQgetvalue(res, row, 19));
        r.updated_at = _epoch(PQgetvalue(res, row, 20));
    }

    // ============================================================================
    // _mapEvidenceRef  —  PGresult row → EvidenceRef
    // ============================================================================
    void ForensicRepository::_mapEvidenceRef(pg_result *res, int row, EvidenceRef &e)
    {
        e = EvidenceRef{};
        e.evidence_id = std::atoi(PQgetvalue(res, row, 0));
        e.request_id = std::atoi(PQgetvalue(res, row, 1));
        _scopy(e.evidence_number, sizeof(e.evidence_number), PQgetvalue(res, row, 2));
        _scopy(e.evidence_type, sizeof(e.evidence_type), PQgetvalue(res, row, 3));
        _scopy(e.evidence_status, sizeof(e.evidence_status), PQgetvalue(res, row, 4));
        _scopy(e.description, sizeof(e.description), PQgetvalue(res, row, 5));
        _scopy(e.notes, sizeof(e.notes), PQgetvalue(res, row, 6));
        e.linked_at = _epoch(PQgetvalue(res, row, 7));
    }

    // ============================================================================
    // SELECT fragment reused in selectByCase and selectPending
    // ============================================================================
    static const char *kSelectCols =
        "SELECT flr.request_id, flr.case_id, flr.request_number, flr.request_status, "
        "       flr.lab_name, flr.examiner_name, flr.examination_purpose, "
        "       flr.purpose_description, flr.authorized_by, "
        "       EXTRACT(EPOCH FROM flr.created_at)::bigint, "
        "       EXTRACT(EPOCH FROM flr.received_by_lab_date)::bigint, "
        "       EXTRACT(EPOCH FROM flr.examination_start_date)::bigint, "
        "       COALESCE(flr.findings, ''), "
        "       COALESCE(flr.report_file_path, ''), "
        "       EXTRACT(EPOCH FROM flr.report_ready_date)::bigint, "
        "       EXTRACT(EPOCH FROM flr.report_delivered_date)::bigint, "
        "       flr.is_contested::int, "
        "       COALESCE(flr.contest_reason, ''), "
        "       COALESCE(flr.contested_by, 0), "
        "       EXTRACT(EPOCH FROM flr.contested_at)::bigint, "
        "       EXTRACT(EPOCH FROM flr.updated_at)::bigint "
        "FROM public.Forensic_Lab_Requests flr ";

    // ============================================================================
    // insertRequest
    // ============================================================================
    ResultCode ForensicRepository::insertRequest(
        PGconn *conn,
        int case_id,
        const char *examination_purpose,
        const char *purpose_description,
        const char *lab_name,
        const char *examiner_name,
        int authorized_by,
        int &out_request_id)
    {
        // Generate a unique request number: FR-<epoch>-<case_id>
        char req_number[32];
        std::snprintf(req_number, sizeof(req_number),
                      "FR-%ld-%d", static_cast<long>(std::time(nullptr)), case_id);

        char p_case[16], p_auth[16];
        std::snprintf(p_case, sizeof(p_case), "%d", case_id);
        std::snprintf(p_auth, sizeof(p_auth), "%d", authorized_by);

        const char *params[7] = {
            req_number,
            p_case,
            examination_purpose,
            purpose_description,
            lab_name,
            examiner_name,
            p_auth};

        const char *sql =
            "INSERT INTO public.Forensic_Lab_Requests "
            "  (request_number, case_id, request_status, examination_purpose, "
            "   purpose_description, lab_name, examiner_name, authorized_by, "
            "   is_contested, created_at, updated_at) "
            "VALUES ($1, $2::int, 'REQUESTED', $3, $4, $5, $6, $7::int, "
            "        false, NOW(), NOW()) "
            "RETURNING request_id;";

        PGresult *res = PQexecParams(conn, sql,
                                     7, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("forensic_repository: insertRequest failed");
            Logger::error(PQresultErrorMessage(res));
            PQclear(res);
            return ResultCode::DB_ERROR;
        }

        out_request_id = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        char msg[64];
        std::snprintf(msg, sizeof(msg),
                      "forensic_repository: Request %d created", out_request_id);
        Logger::info(msg);
        return ResultCode::OK;
    }

    // ============================================================================
    // insertEvidenceLink
    // DB Trigger 1 fires on this INSERT:
    //   → UPDATE public.Evidence SET evidence_status = 'SENT_TO_LAB'
    //      WHERE evidence_id = NEW.evidence_id
    // This function NEVER updates Evidence directly.
    // ============================================================================
    ResultCode ForensicRepository::insertEvidenceLink(
        PGconn *conn,
        int request_id,
        int evidence_id,
        const char *notes)
    {
        char p0[16], p1[16];
        std::snprintf(p0, sizeof(p0), "%d", request_id);
        std::snprintf(p1, sizeof(p1), "%d", evidence_id);

        const char *params[3] = {p0, p1, notes ? notes : ""};

        const char *sql =
            "INSERT INTO public.Forensic_Request_Evidence "
            "  (request_id, evidence_id, notes, linked_at) "
            "VALUES ($1::int, $2::int, $3, NOW()) "
            "ON CONFLICT (request_id, evidence_id) DO NOTHING;";
        // Composite PK — duplicate link is silently ignored.
        // The trigger only fires on an actual INSERT (not DO NOTHING).

        PGresult *res = PQexecParams(conn, sql,
                                     3, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("forensic_repository: insertEvidenceLink failed");
            Logger::error(PQresultErrorMessage(res));
            PQclear(res);
            return ResultCode::DB_ERROR;
        }

        PQclear(res);
        Logger::info("forensic_repository: Evidence linked — trigger will set SENT_TO_LAB");
        return ResultCode::OK;
    }

    // ============================================================================
    // updateStatus
    // When new_status = 'REPORT_DELIVERED', DB Trigger 2 fires:
    //   → UPDATE public.Evidence SET evidence_status = 'RETURNED_FROM_LAB'
    //      WHERE evidence_id IN (SELECT evidence_id FROM Forensic_Request_Evidence
    //                            WHERE request_id = NEW.request_id)
    // This function NEVER updates Evidence directly.
    // ============================================================================
    ResultCode ForensicRepository::updateStatus(
        PGconn *conn,
        int request_id,
        const char *new_status)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", request_id);

        const char *params[2] = {new_status, p0};

        const char *sql =
            "UPDATE public.Forensic_Lab_Requests "
            "SET request_status = $1, updated_at = NOW() "
            "WHERE request_id = $2::int;";

        PGresult *res = PQexecParams(conn, sql,
                                     2, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("forensic_repository: updateStatus failed");
            Logger::error(PQresultErrorMessage(res));
            PQclear(res);
            return ResultCode::DB_ERROR;
        }

        if (std::atoi(PQcmdTuples(res)) == 0)
        {
            PQclear(res);
            Logger::debug("forensic_repository: updateStatus — request_id not found");
            return ResultCode::NOT_FOUND;
        }

        char msg[80];
        std::snprintf(msg, sizeof(msg),
                      "forensic_repository: request %d → %s", request_id, new_status);
        Logger::info(msg);
        PQclear(res);
        return ResultCode::OK;
    }

    // ============================================================================
    // updateReceivedDate  —  sets received_by_lab_date; called with REQUESTED→RECEIVED_BY_LAB
    // ============================================================================
    ResultCode ForensicRepository::updateReceivedDate(
        PGconn *conn,
        int request_id,
        const char *received_date)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", request_id);
        const char *params[2] = {received_date, p0};

        const char *sql =
            "UPDATE public.Forensic_Lab_Requests "
            "SET received_by_lab_date = $1::date, updated_at = NOW() "
            "WHERE request_id = $2::int;";

        PGresult *res = PQexecParams(conn, sql, 2, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("forensic_repository: updateReceivedDate failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }
        PQclear(res);
        return ResultCode::OK;
    }

    // ============================================================================
    // updateExaminationStartDate  —  captures NOW() when examination starts
    // ============================================================================
    ResultCode ForensicRepository::updateExaminationStartDate(
        PGconn *conn,
        int request_id)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", request_id);
        const char *params[1] = {p0};

        const char *sql =
            "UPDATE public.Forensic_Lab_Requests "
            "SET examination_start_date = NOW(), updated_at = NOW() "
            "WHERE request_id = $1::int;";

        PGresult *res = PQexecParams(conn, sql, 1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("forensic_repository: updateExaminationStartDate failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }
        PQclear(res);
        return ResultCode::OK;
    }

    // ============================================================================
    // updateFindings
    // Sets findings, report_file_path, report_ready_date, and
    // report_delivered_date in a single UPDATE.
    // Called after the two-step REPORT_READY → REPORT_DELIVERED transition.
    // DB Trigger 2 will fire when updateStatus is subsequently called with
    // 'REPORT_DELIVERED', updating all linked evidence to RETURNED_FROM_LAB.
    // ============================================================================
    ResultCode ForensicRepository::updateFindings(
        PGconn *conn,
        int request_id,
        const char *findings,
        const char *report_file_path,
        const char *delivery_date)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", request_id);

        const char *params[4] = {findings, report_file_path, delivery_date, p0};

        const char *sql =
            "UPDATE public.Forensic_Lab_Requests "
            "SET findings             = $1, "
            "    report_file_path     = $2, "
            "    report_ready_date    = NOW(), "
            "    report_delivered_date = $3::date, "
            "    updated_at           = NOW() "
            "WHERE request_id = $4::int;";

        PGresult *res = PQexecParams(conn, sql, 4, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("forensic_repository: updateFindings failed");
            Logger::error(PQresultErrorMessage(res));
            PQclear(res);
            return ResultCode::DB_ERROR;
        }
        PQclear(res);
        Logger::info("forensic_repository: Findings recorded");
        return ResultCode::OK;
    }

    // ============================================================================
    // updateAmendment  —  corrects findings text, does NOT change status
    // ============================================================================
    ResultCode ForensicRepository::updateAmendment(
        PGconn *conn,
        int request_id,
        const char *amended_findings)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", request_id);
        const char *params[2] = {amended_findings, p0};

        const char *sql =
            "UPDATE public.Forensic_Lab_Requests "
            "SET findings = $1, updated_at = NOW() "
            "WHERE request_id = $2::int;";

        PGresult *res = PQexecParams(conn, sql, 2, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("forensic_repository: updateAmendment failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }
        PQclear(res);
        Logger::info("forensic_repository: Findings amended");
        return ResultCode::OK;
    }

    // ============================================================================
    // updateContest  —  sets contestation fields, status transition is separate
    // ============================================================================
    ResultCode ForensicRepository::updateContest(
        PGconn *conn,
        int request_id,
        const char *contest_reason,
        int contested_by)
    {
        char p0[16], p1[16];
        std::snprintf(p0, sizeof(p0), "%d", contested_by);
        std::snprintf(p1, sizeof(p1), "%d", request_id);

        const char *params[3] = {contest_reason, p0, p1};

        const char *sql =
            "UPDATE public.Forensic_Lab_Requests "
            "SET is_contested   = true, "
            "    contest_reason = $1, "
            "    contested_by   = $2::int, "
            "    contested_at   = NOW(), "
            "    updated_at     = NOW() "
            "WHERE request_id = $3::int;";

        PGresult *res = PQexecParams(conn, sql, 3, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("forensic_repository: updateContest failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }
        PQclear(res);
        Logger::info("forensic_repository: Report contested");
        return ResultCode::OK;
    }

    // ============================================================================
    // selectByCase  —  all requests for a case, DESC
    // ============================================================================
    ResultCode ForensicRepository::selectByCase(
        PGconn *conn,
        int case_id,
        std::vector<ForensicRecord> &out)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", case_id);
        const char *params[1] = {p0};

        char sql[2048];
        std::snprintf(sql, sizeof(sql),
                      "%sWHERE flr.case_id = $1::int ORDER BY flr.created_at DESC;",
                      kSelectCols);

        PGresult *res = PQexecParams(conn, sql, 1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("forensic_repository: selectByCase failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }

        int n = PQntuples(res);
        if (n == 0)
        {
            PQclear(res);
            return ResultCode::NOT_FOUND;
        }

        for (int i = 0; i < n; ++i)
        {
            ForensicRecord r;
            _mapRecord(res, i, r);
            out.push_back(r);
        }
        PQclear(res);
        return ResultCode::OK;
    }

    // ============================================================================
    // selectPending  —  non-delivered requests at a station
    // ============================================================================
    ResultCode ForensicRepository::selectPending(
        PGconn *conn,
        int station_id,
        std::vector<ForensicRecord> &out)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", station_id);
        const char *params[1] = {p0};

        char sql[2048];
        std::snprintf(sql, sizeof(sql),
                      "%s"
                      "JOIN public.Cases c ON flr.case_id = c.case_id "
                      "WHERE c.station_id = $1::int "
                      "  AND flr.request_status NOT IN ('REPORT_DELIVERED', 'CONTESTED') "
                      "ORDER BY flr.created_at ASC;",
                      kSelectCols);

        PGresult *res = PQexecParams(conn, sql, 1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("forensic_repository: selectPending failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }

        int n = PQntuples(res);
        if (n == 0)
        {
            PQclear(res);
            return ResultCode::NOT_FOUND;
        }

        for (int i = 0; i < n; ++i)
        {
            ForensicRecord r;
            _mapRecord(res, i, r);
            out.push_back(r);
        }
        PQclear(res);
        return ResultCode::OK;
    }

    // ============================================================================
    // selectEvidenceByRequest  —  JOIN Forensic_Request_Evidence with Evidence
    // ============================================================================
    ResultCode ForensicRepository::selectEvidenceByRequest(
        PGconn *conn,
        int request_id,
        std::vector<EvidenceRef> &out)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", request_id);
        const char *params[1] = {p0};

        const char *sql =
            "SELECT fre.evidence_id, fre.request_id, "
            "       e.evidence_number, e.evidence_type, e.evidence_status, "
            "       COALESCE(e.description, ''), "
            "       COALESCE(fre.notes, ''), "
            "       EXTRACT(EPOCH FROM fre.linked_at)::bigint "
            "FROM public.Forensic_Request_Evidence fre "
            "JOIN public.Evidence e ON fre.evidence_id = e.evidence_id "
            "WHERE fre.request_id = $1::int "
            "  AND e.is_deleted = false "
            "ORDER BY fre.linked_at ASC;";

        PGresult *res = PQexecParams(conn, sql, 1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("forensic_repository: selectEvidenceByRequest failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }

        int n = PQntuples(res);
        if (n == 0)
        {
            PQclear(res);
            return ResultCode::NOT_FOUND;
        }

        for (int i = 0; i < n; ++i)
        {
            EvidenceRef e;
            _mapEvidenceRef(res, i, e);
            out.push_back(e);
        }
        PQclear(res);
        return ResultCode::OK;
    }

    // ============================================================================
    // fetchCurrentStatus  —  lightweight read used by ForensicManager before transitions
    // ============================================================================
    ResultCode ForensicRepository::fetchCurrentStatus(
        PGconn *conn,
        int request_id,
        char out_status[24],
        time_t &out_receipt_epoch)
    {
        char p0[16];
        std::snprintf(p0, sizeof(p0), "%d", request_id);
        const char *params[1] = {p0};

        const char *sql =
            "SELECT request_status, "
            "       EXTRACT(EPOCH FROM received_by_lab_date)::bigint "
            "FROM public.Forensic_Lab_Requests "
            "WHERE request_id = $1::int;";

        PGresult *res = PQexecParams(conn, sql, 1, nullptr, params, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("forensic_repository: fetchCurrentStatus failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }

        if (PQntuples(res) == 0)
        {
            PQclear(res);
            return ResultCode::NOT_FOUND;
        }

        _scopy(out_status, 24, PQgetvalue(res, 0, 0));
        out_receipt_epoch = _epoch(PQgetvalue(res, 0, 1));
        PQclear(res);
        return ResultCode::OK;
    }

} // namespace forensic