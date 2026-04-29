/**
 * @file audit_query.cpp
 * @brief Implementation of AuditQuery — stateless parameterised SQL executor.
 *
 * SQL Design Notes
 * ----------------
 * Every query uses $N placeholders with PQexecParams.
 * No std::string concatenation is ever used to build a query string.
 * All parameters are passed as TEXT ($N binds to const char*).
 * Integer parameters are formatted into static char buffers on the stack —
 * never heap-allocated — and remain valid for the duration of the PQexecParams
 * call.
 *
 * Column Order Contract
 * ---------------------
 * Every SELECT list is ordered identically:
 *   0  audit_log_id
 *   1  table_name
 *   2  record_pk
 *   3  operation
 *   4  officer_id
 *   5  belt_number
 *   6  backend_pid
 *   7  client_addr
 *   8  old_values
 *   9  new_values
 *   10 EXTRACT(EPOCH FROM changed_at)
 *
 * _mapRow() is coupled to this order. Any schema change must update both.
 */

#include "../include/audit_query.h"
#include "common/logger.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace JusticeFlow;

namespace audit
{

    // -----------------------------------------------------------------------
    // SQL constants — all parameterised, no string building at runtime
    // -----------------------------------------------------------------------

    // Shared SELECT column list used by every query (see Column Order Contract)
    static constexpr const char *SELECT_COLS =
        "SELECT al.audit_log_id, "
        "       al.table_name, "
        "       al.record_pk, "
        "       al.operation, "
        "       al.officer_id, "
        "       COALESCE(al.belt_number, '') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT, '') AS client_addr, "
        "       COALESCE(al.old_values::TEXT, '') AS old_values, "
        "       COALESCE(al.new_values::TEXT, '') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al ";

    // ------------------------------------------------------------------
    // getChangeHistory — all audit entries related to a case
    // ------------------------------------------------------------------
    static constexpr const char *SQL_CASE_HISTORY =
        "SELECT al.audit_log_id, "
        "       al.table_name, "
        "       al.record_pk, "
        "       al.operation, "
        "       al.officer_id, "
        "       COALESCE(al.belt_number, '') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT, '') AS client_addr, "
        "       COALESCE(al.old_values::TEXT, '') AS old_values, "
        "       COALESCE(al.new_values::TEXT, '') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al "
        "WHERE  (al.table_name = 'CASES'    AND al.record_pk = $1) "
        "OR     (al.table_name = 'EVIDENCE' AND al.record_pk IN "
        "            (SELECT evidence_id FROM subsystem2.evidence WHERE case_id = $1)) "
        "OR     (al.table_name = 'WARRANTS' AND al.record_pk IN "
        "            (SELECT warrant_id FROM subsystem3.warrants WHERE case_id = $1)) "
        "OR     (al.table_name = 'ARRESTS'  AND al.record_pk IN "
        "            (SELECT arrest_id FROM subsystem3.arrests WHERE case_id = $1)) "
        "OR     (al.table_name = 'BAIL_RECORDS' AND al.record_pk IN "
        "            (SELECT br.bail_id FROM subsystem3.bail_records br "
        "             JOIN   subsystem3.arrests a ON br.arrest_id = a.arrest_id "
        "             WHERE  a.case_id = $1)) "
        "OR     (al.table_name = 'CHARGE_SHEETS' AND al.record_pk IN "
        "            (SELECT charge_sheet_id FROM subsystem3.charge_sheets WHERE case_id = $1)) "
        "OR     (al.table_name = 'ACCUSED' AND al.record_pk IN "
        "            (SELECT accused_id FROM subsystem2.accused WHERE case_id = $1)) "
        "ORDER  BY al.changed_at DESC;";

    // ------------------------------------------------------------------
    // getOfficerActions — all entries for one officer in a time window
    // ------------------------------------------------------------------
    static constexpr const char *SQL_OFFICER_ACTIONS =
        "SELECT al.audit_log_id, "
        "       al.table_name, "
        "       al.record_pk, "
        "       al.operation, "
        "       al.officer_id, "
        "       COALESCE(al.belt_number, '') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT, '') AS client_addr, "
        "       COALESCE(al.old_values::TEXT, '') AS old_values, "
        "       COALESCE(al.new_values::TEXT, '') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al "
        "WHERE  al.officer_id = $1 "
        "AND    al.changed_at >= to_timestamp($2::BIGINT) "
        "AND    al.changed_at <= to_timestamp($3::BIGINT) "
        "ORDER  BY al.changed_at DESC;";

    // ------------------------------------------------------------------
    // getTableChanges — full mutation history of one record in one table
    // ------------------------------------------------------------------
    static constexpr const char *SQL_TABLE_CHANGES =
        "SELECT al.audit_log_id, "
        "       al.table_name, "
        "       al.record_pk, "
        "       al.operation, "
        "       al.officer_id, "
        "       COALESCE(al.belt_number, '') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT, '') AS client_addr, "
        "       COALESCE(al.old_values::TEXT, '') AS old_values, "
        "       COALESCE(al.new_values::TEXT, '') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al "
        "WHERE  al.table_name = $1 "
        "AND    al.record_pk  = $2 "
        "ORDER  BY al.changed_at ASC;";

    // ------------------------------------------------------------------
    // queryByTimeWindow — all audit activity in [from, to]
    // ------------------------------------------------------------------
    static constexpr const char *SQL_TIME_WINDOW =
        "SELECT al.audit_log_id, "
        "       al.table_name, "
        "       al.record_pk, "
        "       al.operation, "
        "       al.officer_id, "
        "       COALESCE(al.belt_number, '') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT, '') AS client_addr, "
        "       COALESCE(al.old_values::TEXT, '') AS old_values, "
        "       COALESCE(al.new_values::TEXT, '') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al "
        "WHERE  al.changed_at >= to_timestamp($1::BIGINT) "
        "AND    al.changed_at <= to_timestamp($2::BIGINT) "
        "ORDER  BY al.changed_at DESC;";

    // ------------------------------------------------------------------
    // detectSuspiciousActivity — four-pattern SQL detection (UNION ALL)
    //
    // $1 = station_id (INTEGER)
    //
    // The query is a UNION ALL of four sub-queries, one per pattern.
    // Each sub-query returns the same 11-column shape (matching _mapRow).
    // Deduplication is intentionally NOT done — the same record may appear
    // under multiple patterns, giving the caller full diagnostic information.
    //
    // Pattern 1 — BULK_CHANGE
    //   backend_pids that wrote > 20 rows within any 1-hour bucket in the
    //   last 24 hours, scoped to the station's officers.
    //
    // Pattern 2 — RAPID_DELETE
    //   Officers with 5+ DELETEs within any 5-minute window in the last 24 h.
    //   Scoped to the station's officers.
    //
    // Pattern 3 — AFTER_HOURS
    //   Any DML by station officers outside 06:00–22:00 UTC in last 24 h.
    //
    // Pattern 4 — JURISDICTION_VIOLATION
    //   Records from officers whose station_id (in subsystem1.officers) does
    //   NOT equal $1, but who modified Cases/Evidence belonging to this station.
    // ------------------------------------------------------------------
    static constexpr const char *SQL_SUSPICIOUS =
        /* ---- Pattern 1: BULK_CHANGE ---- */
        "SELECT al.audit_log_id, "
        "       al.table_name, al.record_pk, al.operation, "
        "       al.officer_id, COALESCE(al.belt_number,'') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT,'') AS client_addr, "
        "       COALESCE(al.old_values::TEXT,'') AS old_values, "
        "       COALESCE(al.new_values::TEXT,'') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al "
        "WHERE  al.changed_at >= NOW() - INTERVAL '24 hours' "
        "AND    al.officer_id IN "
        "           (SELECT officer_id FROM subsystem1.officers WHERE station_id = $1) "
        "AND    al.backend_pid IN ( "
        "           SELECT backend_pid "
        "           FROM   audit.Audit_Log "
        "           WHERE  changed_at >= NOW() - INTERVAL '24 hours' "
        "           AND    officer_id IN "
        "                      (SELECT officer_id FROM subsystem1.officers WHERE station_id = $1) "
        "           GROUP  BY backend_pid, date_trunc('hour', changed_at) "
        "           HAVING COUNT(*) > 20 "
        "       ) "

        "UNION ALL "

        /* ---- Pattern 2: RAPID_DELETE ---- */
        "SELECT al.audit_log_id, "
        "       al.table_name, al.record_pk, al.operation, "
        "       al.officer_id, COALESCE(al.belt_number,'') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT,'') AS client_addr, "
        "       COALESCE(al.old_values::TEXT,'') AS old_values, "
        "       COALESCE(al.new_values::TEXT,'') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al "
        "WHERE  al.changed_at >= NOW() - INTERVAL '24 hours' "
        "AND    al.operation   = 'DELETE' "
        "AND    al.officer_id IN "
        "           (SELECT officer_id FROM subsystem1.officers WHERE station_id = $1) "
        "AND    al.officer_id IN ( "
        "           SELECT officer_id "
        "           FROM   audit.Audit_Log "
        "           WHERE  operation    = 'DELETE' "
        "           AND    changed_at  >= NOW() - INTERVAL '24 hours' "
        "           AND    officer_id IN "
        "                      (SELECT officer_id FROM subsystem1.officers WHERE station_id = $1) "
        "           GROUP  BY officer_id, "
        "                     (EXTRACT(EPOCH FROM changed_at)::BIGINT / 300) "
        "           HAVING COUNT(*) >= 5 "
        "       ) "

        "UNION ALL "

        /* ---- Pattern 3: AFTER_HOURS ---- */
        "SELECT al.audit_log_id, "
        "       al.table_name, al.record_pk, al.operation, "
        "       al.officer_id, COALESCE(al.belt_number,'') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT,'') AS client_addr, "
        "       COALESCE(al.old_values::TEXT,'') AS old_values, "
        "       COALESCE(al.new_values::TEXT,'') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al "
        "WHERE  al.changed_at >= NOW() - INTERVAL '24 hours' "
        "AND    al.officer_id IN "
        "           (SELECT officer_id FROM subsystem1.officers WHERE station_id = $1) "
        "AND    ( "
        "           EXTRACT(HOUR FROM al.changed_at AT TIME ZONE 'UTC') < 6 "
        "           OR "
        "           EXTRACT(HOUR FROM al.changed_at AT TIME ZONE 'UTC') >= 22 "
        "       ) "

        "UNION ALL "

        /* ---- Pattern 4: JURISDICTION_VIOLATION ---- */
        "SELECT al.audit_log_id, "
        "       al.table_name, al.record_pk, al.operation, "
        "       al.officer_id, COALESCE(al.belt_number,'') AS belt_number, "
        "       al.backend_pid, "
        "       COALESCE(al.client_addr::TEXT,'') AS client_addr, "
        "       COALESCE(al.old_values::TEXT,'') AS old_values, "
        "       COALESCE(al.new_values::TEXT,'') AS new_values, "
        "       EXTRACT(EPOCH FROM al.changed_at)::BIGINT AS epoch "
        "FROM   audit.Audit_Log al "
        "WHERE  al.changed_at >= NOW() - INTERVAL '24 hours' "
        "AND    al.table_name IN ('CASES', 'EVIDENCE') "
        /* Officer's home station does NOT match this station */
        "AND    al.officer_id IN ( "
        "           SELECT officer_id FROM subsystem1.officers "
        "           WHERE  station_id != $1 "
        "       ) "
        /* But the record they touched belongs to this station */
        "AND    ( "
        "    (al.table_name = 'CASES' AND al.record_pk IN "
        "        (SELECT case_id FROM subsystem2.cases WHERE station_id = $1)) "
        "    OR "
        "    (al.table_name = 'EVIDENCE' AND al.record_pk IN "
        "        (SELECT e.evidence_id FROM subsystem2.evidence e "
        "         JOIN   subsystem2.cases c ON e.case_id = c.case_id "
        "         WHERE  c.station_id = $1)) "
        ") "

        "ORDER  BY epoch DESC;";

    // -----------------------------------------------------------------------
    // _mapRow — positional column → AuditRecord field mapping
    // -----------------------------------------------------------------------

    void AuditQuery::_mapRow(PGresult *res, int row, AuditRecord &rec)
    {
        // Helper lambda: safe strncpy with guaranteed NUL terminator
        auto scopy = [](char *dst, const char *src, int max)
        {
            if (src == nullptr)
            {
                dst[0] = '\0';
                return;
            }
            std::strncpy(dst, src, static_cast<size_t>(max - 1));
            dst[max - 1] = '\0';
        };

        rec.log_id = std::atoi(PQgetvalue(res, row, 0));
        scopy(rec.table_name, PQgetvalue(res, row, 1), AUDIT_TABLE_NAME_LEN);
        rec.record_pk = std::atoi(PQgetvalue(res, row, 2));
        scopy(rec.operation, PQgetvalue(res, row, 3), AUDIT_OPERATION_LEN);
        rec.officer_id = std::atoi(PQgetvalue(res, row, 4));
        scopy(rec.belt_number, PQgetvalue(res, row, 5), AUDIT_BELT_LEN);
        rec.backend_pid = std::atoi(PQgetvalue(res, row, 6));
        scopy(rec.client_addr, PQgetvalue(res, row, 7), AUDIT_ADDR_LEN);
        scopy(rec.old_values, PQgetvalue(res, row, 8), AUDIT_JSONB_LEN);
        scopy(rec.new_values, PQgetvalue(res, row, 9), AUDIT_JSONB_LEN);
        rec.changed_at = static_cast<time_t>(std::atoll(PQgetvalue(res, row, 10)));
    }

    // -----------------------------------------------------------------------
    // _executeAndMap — Template Method skeleton
    // -----------------------------------------------------------------------

    ResultCode AuditQuery::_executeAndMap(
        PGconn *conn,
        const char *sql,
        const char *const *params,
        int nparams,
        std::vector<AuditRecord> &out)
    {
        if (conn == nullptr)
        {
            Logger::error("audit_query: NULL connection passed to _executeAndMap");
            return ResultCode::DB_ERROR;
        }

        PGresult *res = PQexecParams(
            conn,
            sql,
            nparams,
            nullptr, // let server infer param types
            params,
            nullptr, // param lengths (text format — not needed)
            nullptr, // param formats (0 = text)
            0        // result format (0 = text)
        );

        ExecStatusType status = PQresultStatus(res);

        if (status != PGRES_TUPLES_OK)
        {
            Logger::error("audit_query: PQexecParams failed");
            Logger::error(PQresultErrorMessage(res));
            PQclear(res);
            return ResultCode::DB_ERROR;
        }

        int nrows = PQntuples(res);
        if (nrows == 0)
        {
            PQclear(res);
            return ResultCode::NOT_FOUND;
        }

        out.reserve(out.size() + static_cast<size_t>(nrows));
        for (int r = 0; r < nrows; ++r)
        {
            AuditRecord rec;
            _mapRow(res, r, rec);
            out.push_back(rec);
        }

        PQclear(res);
        return ResultCode::OK;
    }

    // -----------------------------------------------------------------------
    // Public query methods — each provides SQL + params to _executeAndMap
    // -----------------------------------------------------------------------

    ResultCode AuditQuery::getChangeHistory(
        PGconn *conn, int case_id,
        std::vector<AuditRecord> &out)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", case_id);
        const char *params[] = {p1};

        ResultCode rc = _executeAndMap(conn, SQL_CASE_HISTORY, params, 1, out);

        if (rc == ResultCode::OK)
            Logger::info("audit_query: getChangeHistory succeeded");
        else if (rc == ResultCode::NOT_FOUND)
            Logger::debug("audit_query: getChangeHistory — no audit history for case");

        return rc;
    }

    ResultCode AuditQuery::getOfficerActions(
        PGconn *conn, int officer_id, time_t from, time_t to,
        std::vector<AuditRecord> &out)
    {
        char p1[24], p2[24], p3[24];
        std::snprintf(p1, sizeof(p1), "%d", officer_id);
        std::snprintf(p2, sizeof(p2), "%lld", static_cast<long long>(from));
        std::snprintf(p3, sizeof(p3), "%lld", static_cast<long long>(to));
        const char *params[] = {p1, p2, p3};

        ResultCode rc = _executeAndMap(conn, SQL_OFFICER_ACTIONS, params, 3, out);

        if (rc == ResultCode::OK)
            Logger::info("audit_query: getOfficerActions succeeded");
        else if (rc == ResultCode::NOT_FOUND)
            Logger::debug("audit_query: getOfficerActions — no actions in window");

        return rc;
    }

    ResultCode AuditQuery::getTableChanges(
        PGconn *conn, const char *table_name, int record_id,
        std::vector<AuditRecord> &out)
    {
        char p2[24];
        std::snprintf(p2, sizeof(p2), "%d", record_id);
        const char *params[] = {table_name, p2};

        ResultCode rc = _executeAndMap(conn, SQL_TABLE_CHANGES, params, 2, out);

        if (rc == ResultCode::OK)
            Logger::info("audit_query: getTableChanges succeeded");
        else if (rc == ResultCode::NOT_FOUND)
            Logger::debug("audit_query: getTableChanges — no history for record");

        return rc;
    }

    ResultCode AuditQuery::queryByTimeWindow(
        PGconn *conn, time_t from, time_t to,
        std::vector<AuditRecord> &out)
    {
        char p1[24], p2[24];
        std::snprintf(p1, sizeof(p1), "%lld", static_cast<long long>(from));
        std::snprintf(p2, sizeof(p2), "%lld", static_cast<long long>(to));
        const char *params[] = {p1, p2};

        ResultCode rc = _executeAndMap(conn, SQL_TIME_WINDOW, params, 2, out);

        if (rc == ResultCode::OK)
            Logger::info("audit_query: queryByTimeWindow succeeded");
        else if (rc == ResultCode::NOT_FOUND)
            Logger::debug("audit_query: queryByTimeWindow — no records in window");

        return rc;
    }

    ResultCode AuditQuery::detectSuspiciousActivity(
        PGconn *conn, int station_id,
        std::vector<AuditRecord> &out)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", station_id);
        const char *params[] = {p1};

        // NOT_FOUND is a valid "no suspicious activity" — normalise to OK
        ResultCode rc = _executeAndMap(conn, SQL_SUSPICIOUS, params, 1, out);
        if (rc == ResultCode::NOT_FOUND)
        {
            Logger::info("audit_query: detectSuspiciousActivity — no suspicious activity found");
            return ResultCode::OK;
        }

        if (rc == ResultCode::OK)
            Logger::info("audit_query: detectSuspiciousActivity — suspicious records flagged");

        return rc;
    }

} // namespace audit