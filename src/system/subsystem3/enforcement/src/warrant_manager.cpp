/**
 * @file warrant_manager.cpp
 * @brief WarrantManager implementation — all SQL via PQexecParams.
 *
 * SQL column contract for _mapRow (positions 0-based):
 *   0  warrant_id
 *   1  warrant_number
 *   2  case_id
 *   3  accused_cnic
 *   4  warrant_type
 *   5  warrant_status
 *   6  issuing_court
 *   7  magistrate_name
 *   8  target_address
 *   9  valid_until (ISO date string)
 *   10 cancellation_reason
 *   11 requested_by
 *   12 executed_by
 *   13 cancelled_by
 *   14 EXTRACT(EPOCH FROM issue_date)
 *   15 EXTRACT(EPOCH FROM executed_at)
 *   16 EXTRACT(EPOCH FROM cancelled_at)
 *   17 EXTRACT(EPOCH FROM created_at)
 *   18 EXTRACT(EPOCH FROM updated_at)
 */

#include "../include/warrant_manager.h"
#include "../../../shr_infra/auth/include/auth_module.h"
#include "common/logger.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace JusticeFlow;

namespace enforcement
{

    // -----------------------------------------------------------------------
    // SQL constants — all parameterised, $N placeholders
    // -----------------------------------------------------------------------

    static constexpr const char *SQL_SELECT =
        "SELECT w.warrant_id, w.warrant_number, w.case_id, w.accused_cnic, "
        "       w.warrant_type, w.warrant_status, w.issuing_court, w.magistrate_name, "
        "       COALESCE(w.target_address, '') AS target_address, "
        "       COALESCE(w.valid_until::TEXT, '') AS valid_until, "
        "       COALESCE(w.cancellation_reason, '') AS cancellation_reason, "
        "       w.requested_by, "
        "       COALESCE(w.executed_by, 0) AS executed_by, "
        "       COALESCE(w.cancelled_by, 0) AS cancelled_by, "
        "       EXTRACT(EPOCH FROM w.issue_date)::BIGINT, "
        "       COALESCE(EXTRACT(EPOCH FROM w.executed_at)::BIGINT, 0), "
        "       COALESCE(EXTRACT(EPOCH FROM w.cancelled_at)::BIGINT, 0), "
        "       EXTRACT(EPOCH FROM w.created_at)::BIGINT, "
        "       EXTRACT(EPOCH FROM w.updated_at)::BIGINT "
        "FROM   subsystem3.warrants w ";

    static constexpr const char *SQL_BY_CASE =
        "SELECT w.warrant_id, w.warrant_number, w.case_id, w.accused_cnic, "
        "       w.warrant_type, w.warrant_status, w.issuing_court, w.magistrate_name, "
        "       COALESCE(w.target_address, '') AS target_address, "
        "       COALESCE(w.valid_until::TEXT, '') AS valid_until, "
        "       COALESCE(w.cancellation_reason, '') AS cancellation_reason, "
        "       w.requested_by, "
        "       COALESCE(w.executed_by, 0), COALESCE(w.cancelled_by, 0), "
        "       EXTRACT(EPOCH FROM w.issue_date)::BIGINT, "
        "       COALESCE(EXTRACT(EPOCH FROM w.executed_at)::BIGINT, 0), "
        "       COALESCE(EXTRACT(EPOCH FROM w.cancelled_at)::BIGINT, 0), "
        "       EXTRACT(EPOCH FROM w.created_at)::BIGINT, "
        "       EXTRACT(EPOCH FROM w.updated_at)::BIGINT "
        "FROM   subsystem3.warrants w "
        "WHERE  w.case_id = $1 "
        "ORDER  BY w.issue_date DESC;";

    static constexpr const char *SQL_ACTIVE_BY_STATION =
        "SELECT w.warrant_id, w.warrant_number, w.case_id, w.accused_cnic, "
        "       w.warrant_type, w.warrant_status, w.issuing_court, w.magistrate_name, "
        "       COALESCE(w.target_address, '') AS target_address, "
        "       COALESCE(w.valid_until::TEXT, '') AS valid_until, "
        "       COALESCE(w.cancellation_reason, '') AS cancellation_reason, "
        "       w.requested_by, "
        "       COALESCE(w.executed_by, 0), COALESCE(w.cancelled_by, 0), "
        "       EXTRACT(EPOCH FROM w.issue_date)::BIGINT, "
        "       COALESCE(EXTRACT(EPOCH FROM w.executed_at)::BIGINT, 0), "
        "       COALESCE(EXTRACT(EPOCH FROM w.cancelled_at)::BIGINT, 0), "
        "       EXTRACT(EPOCH FROM w.created_at)::BIGINT, "
        "       EXTRACT(EPOCH FROM w.updated_at)::BIGINT "
        "FROM   subsystem3.warrants w "
        "JOIN   subsystem2.cases c ON c.case_id = w.case_id "
        "WHERE  c.station_id = $1 "
        "AND    w.warrant_status = 'ISSUED' "
        "ORDER  BY w.valid_until ASC;";

    static constexpr const char *SQL_GET_STATUS_EXPIRY =
        "SELECT warrant_status, valid_until::TEXT "
        "FROM   subsystem3.warrants WHERE warrant_id = $1;";

    static constexpr const char *SQL_INSERT =
        "INSERT INTO subsystem3.warrants "
        "  (warrant_number, case_id, accused_cnic, warrant_type, warrant_status, "
        "   issuing_court, magistrate_name, target_address, issue_date, valid_until, "
        "   requested_by, created_at, updated_at) "
        "VALUES "
        "  ($1, $2, $3, $4, 'ISSUED', $5, $6, $7, NOW(), $8::DATE, $9, NOW(), NOW()) "
        "RETURNING warrant_id;";

    static constexpr const char *SQL_EXECUTE =
        "UPDATE subsystem3.warrants "
        "SET    warrant_status = 'EXECUTED', executed_by = $1, "
        "       executed_at = NOW(), updated_at = NOW() "
        "WHERE  warrant_id = $2;";

    static constexpr const char *SQL_CANCEL =
        "UPDATE subsystem3.warrants "
        "SET    warrant_status = 'CANCELLED', cancelled_by = $1, "
        "       cancellation_reason = $2, cancelled_at = NOW(), updated_at = NOW() "
        "WHERE  warrant_id = $3;";

    static constexpr const char *SQL_SET_OFFICER =
        "SELECT set_config('app.current_officer_id', $1, true), "
        "       set_config('app.current_belt_number', $2, true);";

    static constexpr const char *SQL_CASE_STATUS =
        "SELECT case_status FROM subsystem2.cases WHERE case_id = $1;";

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    bool WarrantManager::_setSessionVars(PGconn *conn, int officer_id, const char *belt)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", officer_id);
        const char *params[] = {p1, belt};
        PGresult *res = PQexecParams(conn, SQL_SET_OFFICER, 2, nullptr, params, nullptr, nullptr, 0);
        bool ok = (PQresultStatus(res) == PGRES_TUPLES_OK);
        if (!ok)
            Logger::error("warrant_manager: Failed to set session variables");
        PQclear(res);
        return ok;
    }

    void WarrantManager::_mapRow(PGresult *res, int row, WarrantRecord &rec)
    {
        auto scopy = [](char *dst, const char *src, int max)
        {
            if (!src)
            {
                dst[0] = '\0';
                return;
            }
            std::strncpy(dst, src, (size_t)(max - 1));
            dst[max - 1] = '\0';
        };

        rec.warrant_id = std::atoi(PQgetvalue(res, row, 0));
        scopy(rec.warrant_number, PQgetvalue(res, row, 1), WARRANT_NUM_LEN);
        rec.case_id = std::atoi(PQgetvalue(res, row, 2));
        scopy(rec.accused_cnic, PQgetvalue(res, row, 3), WARRANT_CNIC_LEN);
        scopy(rec.warrant_type, PQgetvalue(res, row, 4), WARRANT_TYPE_LEN);
        scopy(rec.warrant_status, PQgetvalue(res, row, 5), WARRANT_STATUS_LEN);
        scopy(rec.issuing_court, PQgetvalue(res, row, 6), WARRANT_COURT_LEN);
        scopy(rec.magistrate_name, PQgetvalue(res, row, 7), WARRANT_JUDGE_LEN);
        scopy(rec.target_address, PQgetvalue(res, row, 8), WARRANT_ADDR_LEN);
        scopy(rec.valid_until, PQgetvalue(res, row, 9), WARRANT_DATE_LEN);
        scopy(rec.cancellation_reason, PQgetvalue(res, row, 10), WARRANT_REASON_LEN);
        rec.requested_by = std::atoi(PQgetvalue(res, row, 11));
        rec.executed_by = std::atoi(PQgetvalue(res, row, 12));
        rec.cancelled_by = std::atoi(PQgetvalue(res, row, 13));
        rec.issue_date = (time_t)std::atoll(PQgetvalue(res, row, 14));
        rec.executed_at = (time_t)std::atoll(PQgetvalue(res, row, 15));
        rec.cancelled_at = (time_t)std::atoll(PQgetvalue(res, row, 16));
        rec.created_at = (time_t)std::atoll(PQgetvalue(res, row, 17));
        rec.updated_at = (time_t)std::atoll(PQgetvalue(res, row, 18));
    }

    // -----------------------------------------------------------------------
    // _validateState
    // -----------------------------------------------------------------------

    bool WarrantManager::_validateState(PGconn *conn,
                                        int warrant_id,
                                        const char *expected_from,
                                        const char *target_to,
                                        bool check_expiry,
                                        ResultCode &out_code)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", warrant_id);
        const char *params[] = {p1};

        PGresult *res = PQexecParams(conn, SQL_GET_STATUS_EXPIRY, 1,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
        {
            Logger::error("warrant_manager: Warrant not found");
            out_code = ResultCode::NOT_FOUND;
            PQclear(res);
            return false;
        }

        const char *current = PQgetvalue(res, 0, 0);
        const char *expiry = PQgetvalue(res, 0, 1);

        // State machine check
        if (std::strcmp(current, expected_from) != 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "warrant_manager: Illegal transition %s→%s (current=%s)",
                          expected_from, target_to, current);
            Logger::debug(msg);
            out_code = ResultCode::INVALID_STATE;
            PQclear(res);
            return false;
        }

        // Expiry check for execution
        if (check_expiry && expiry && expiry[0] != '\0')
        {
            // Parse ISO date "YYYY-MM-DD" and compare with today (UTC)
            struct tm tm_expiry = {};
            std::sscanf(expiry, "%d-%d-%d",
                        &tm_expiry.tm_year, &tm_expiry.tm_mon, &tm_expiry.tm_mday);
            tm_expiry.tm_year -= 1900;
            tm_expiry.tm_mon -= 1;
            time_t expiry_epoch = timegm(&tm_expiry);
            if (std::time(nullptr) > expiry_epoch)
            {
                Logger::error("warrant_manager: Warrant has expired");
                out_code = ResultCode::INVALID_STATE;
                PQclear(res);
                return false;
            }
        }

        PQclear(res);
        return true;
    }

    // -----------------------------------------------------------------------
    // Warrant type → DB string
    // -----------------------------------------------------------------------

    static const char *warrantTypeStr(WarrantType t)
    {
        switch (t)
        {
        case WarrantType::ARREST:
            return "ARREST";
        case WarrantType::SEARCH:
            return "SEARCH";
        case WarrantType::SEIZURE:
            return "SEIZURE";
        default:
            return "UNKNOWN";
        }
    }

    // -----------------------------------------------------------------------
    // requestWarrant
    // -----------------------------------------------------------------------

    bool WarrantManager::requestWarrant(PGconn *conn,
                                        const SessionContext &session,
                                        int case_id,
                                        const char *accused_cnic,
                                        WarrantType warrant_type,
                                        const char *magistrate_name,
                                        const char *issuing_court,
                                        const char *valid_until,
                                        const char *target_address,
                                        int &out_warrant_id,
                                        ResultCode &out_code)
    {
        // 1. Rank check — INSPECTOR minimum
        ResultCode rank_rc = auth::AuthManager::getInstance().validateRank(session, static_cast<int>(OfficerRank::INSPECTOR));
        if (rank_rc != ResultCode::OK)
        {
            out_code = ResultCode::RANK_INSUFFICIENT;
            Logger::debug("warrant_manager: Rank insufficient for warrant request");
            return false;
        }

        // 2. Case must be open (REGISTERED or UNDER_INVESTIGATION)
        {
            char cp1[24];
            std::snprintf(cp1, sizeof(cp1), "%d", case_id);
            const char *cparams[] = {cp1};
            PGresult *cres = PQexecParams(conn, SQL_CASE_STATUS, 1,
                                          nullptr, cparams, nullptr, nullptr, 0);
            if (PQresultStatus(cres) != PGRES_TUPLES_OK || PQntuples(cres) == 0)
            {
                out_code = ResultCode::NOT_FOUND;
                Logger::error("warrant_manager: Case not found");
                PQclear(cres);
                return false;
            }
            const char *cstatus = PQgetvalue(cres, 0, 0);
            bool open = (std::strcmp(cstatus, "REGISTERED") == 0 ||
                         std::strcmp(cstatus, "UNDER_INVESTIGATION") == 0);
            PQclear(cres);
            if (!open)
            {
                out_code = ResultCode::INVALID_STATE;
                Logger::error("warrant_manager: Case not in valid state for warrant");
                return false;
            }
        }

        // 3. Generate warrant number: WR-<epoch>-<officer_id>
        char warrant_num[WARRANT_NUM_LEN];
        std::snprintf(warrant_num, sizeof(warrant_num), "WR-%lld-%d",
                      (long long)std::time(nullptr), session.officerId);

        // 4. Set session vars for audit trigger
        if (!_setSessionVars(conn, session.officerId, session.belt_number.c_str()))
        {
            out_code = ResultCode::DB_ERROR;
            return false;
        }

        // 5. Insert
        char officer_str[24];
        std::snprintf(officer_str, sizeof(officer_str), "%d", session.officerId);
        char case_str[24];
        std::snprintf(case_str, sizeof(case_str), "%d", case_id);

        // Provide empty string for target_address if null
        const char *addr = (target_address && target_address[0]) ? target_address : "";

        const char *params[] = {
            warrant_num,
            case_str,
            accused_cnic,
            warrantTypeStr(warrant_type),
            issuing_court,
            magistrate_name,
            addr,
            valid_until,
            officer_str};

        PGresult *res = PQexecParams(conn, SQL_INSERT, 9,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
        {
            Logger::error("warrant_manager: INSERT failed");
            Logger::error(PQresultErrorMessage(res));
            out_code = ResultCode::DB_ERROR;
            PQclear(res);
            return false;
        }

        out_warrant_id = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        out_code = ResultCode::OK;
        Logger::info("warrant_manager: Warrant requested successfully");
        return true;
    }

    // -----------------------------------------------------------------------
    // executeWarrant
    // -----------------------------------------------------------------------

    bool WarrantManager::executeWarrant(PGconn *conn,
                                        const SessionContext &session,
                                        int warrant_id,
                                        ResultCode &out_code)
    {
        // 1. Rank check — INSPECTOR minimum
        ResultCode rank_ok = auth::AuthManager::getInstance().validateRank(session, static_cast<int>(OfficerRank::INSPECTOR));
        if (rank_ok != JusticeFlow::ResultCode::OK)
        {
            out_code = ResultCode::RANK_INSUFFICIENT;
            return false;
        }

        // 2. State machine + expiry check
        if (!_validateState(conn, warrant_id, "ISSUED", "EXECUTED", true, out_code))
            return false;

        // 3. Set session vars
        if (!_setSessionVars(conn, session.officerId, session.belt_number.c_str()))
        {
            out_code = ResultCode::DB_ERROR;
            return false;
        }

        // 4. Update
        char p1[24], p2[24];
        std::snprintf(p1, sizeof(p1), "%d", session.officerId);
        std::snprintf(p2, sizeof(p2), "%d", warrant_id);
        const char *params[] = {p1, p2};

        PGresult *res = PQexecParams(conn, SQL_EXECUTE, 2,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("warrant_manager: executeWarrant UPDATE failed");
            Logger::error(PQresultErrorMessage(res));
            out_code = ResultCode::DB_ERROR;
            PQclear(res);
            return false;
        }
        PQclear(res);

        out_code = ResultCode::OK;
        Logger::info("warrant_manager: Warrant executed");
        return true;
    }

    // -----------------------------------------------------------------------
    // cancelWarrant
    // -----------------------------------------------------------------------

    bool WarrantManager::cancelWarrant(PGconn *conn,
                                       const SessionContext &session,
                                       int warrant_id,
                                       const char *cancellation_reason,
                                       ResultCode &out_code)
    {
        // Guard: reason must be non-empty
        if (!cancellation_reason || cancellation_reason[0] == '\0')
        {
            out_code = ResultCode::INVALID_INPUT;
            Logger::error("warrant_manager: cancellation_reason must not be empty");
            return false;
        }

        // 1. Rank check — INSPECTOR minimum (SHO level)
        ResultCode rank_rc = auth::AuthManager::getInstance().validateRank(session, static_cast<int>(OfficerRank::INSPECTOR));
        if (rank_rc != JusticeFlow::ResultCode::OK)
        {
            out_code = ResultCode::RANK_INSUFFICIENT;
            return false;
        }

        // 2. State check: only ISSUED warrants can be cancelled
        if (!_validateState(conn, warrant_id, "ISSUED", "CANCELLED", false, out_code))
            return false;

        // 3. Set session vars
        if (!_setSessionVars(conn, session.officerId, session.belt_number.c_str()))
        {
            out_code = ResultCode::DB_ERROR;
            return false;
        }

        // 4. Update
        char p1[24], p3[24];
        std::snprintf(p1, sizeof(p1), "%d", session.officerId);
        std::snprintf(p3, sizeof(p3), "%d", warrant_id);
        const char *params[] = {p1, cancellation_reason, p3};

        PGresult *res = PQexecParams(conn, SQL_CANCEL, 3,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("warrant_manager: cancelWarrant UPDATE failed");
            Logger::error(PQresultErrorMessage(res));
            out_code = ResultCode::DB_ERROR;
            PQclear(res);
            return false;
        }
        PQclear(res);

        out_code = ResultCode::OK;
        Logger::info("warrant_manager: Warrant cancelled");
        return true;
    }

    // -----------------------------------------------------------------------
    // getWarrantsByCase
    // -----------------------------------------------------------------------

    ResultCode WarrantManager::getWarrantsByCase(PGconn *conn,
                                                 int case_id,
                                                 std::vector<WarrantRecord> &out)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", case_id);
        const char *params[] = {p1};

        PGresult *res = PQexecParams(conn, SQL_BY_CASE, 1,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("warrant_manager: getWarrantsByCase query failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }
        int n = PQntuples(res);
        if (n == 0)
        {
            PQclear(res);
            return ResultCode::NOT_FOUND;
        }

        out.reserve(out.size() + (size_t)n);
        for (int r = 0; r < n; ++r)
        {
            WarrantRecord rec;
            _mapRow(res, r, rec);
            out.push_back(rec);
        }
        PQclear(res);
        Logger::info("warrant_manager: getWarrantsByCase succeeded");
        return ResultCode::OK;
    }

    // -----------------------------------------------------------------------
    // getActiveWarrants
    // -----------------------------------------------------------------------

    ResultCode WarrantManager::getActiveWarrants(PGconn *conn,
                                                 int station_id,
                                                 std::vector<WarrantRecord> &out)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", station_id);
        const char *params[] = {p1};

        PGresult *res = PQexecParams(conn, SQL_ACTIVE_BY_STATION, 1,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("warrant_manager: getActiveWarrants query failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }
        int n = PQntuples(res);
        if (n == 0)
        {
            PQclear(res);
            return ResultCode::NOT_FOUND;
        }

        out.reserve(out.size() + (size_t)n);
        for (int r = 0; r < n; ++r)
        {
            WarrantRecord rec;
            _mapRow(res, r, rec);
            out.push_back(rec);
        }
        PQclear(res);
        Logger::info("warrant_manager: getActiveWarrants succeeded");
        return ResultCode::OK;
    }

} // namespace enforcement