/**
 * @file arrest_manager.cpp
 * @brief ArrestManager implementation.
 *
 * State machine transition table:
 *   IN_CUSTODY  → BAIL_GRANTED | REMANDED | RELEASED | ESCAPED
 *   BAIL_GRANTED → RELEASED
 *   REMANDED     → RELEASED
 *   RELEASED     → (terminal)
 *   ESCAPED      → (terminal)
 *
 * SQL column contract for _mapRow:
 *   0  arrest_id
 *   1  arrest_number
 *   2  accused_cnic
 *   3  case_id
 *   4  warrant_id  (0 if NULL)
 *   5  arresting_officer_id
 *   6  custody_status
 *   7  arrest_location
 *   8  release_reason
 *   9  dispute_reason
 *   10 is_disputed
 *   11 EXTRACT(EPOCH FROM arrested_at)
 *   12 EXTRACT(EPOCH FROM released_at)  (0 if NULL)
 *   13 EXTRACT(EPOCH FROM created_at)
 *   14 EXTRACT(EPOCH FROM updated_at)
 */

#include "../include/arrest_manager.h"
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
    // SQL constants
    // -----------------------------------------------------------------------

    static constexpr const char *SQL_INSERT_ARREST =
        "INSERT INTO subsystem3.arrests "
        "  (arrest_number, accused_cnic, case_id, warrant_id, arresting_officer_id, "
        "   custody_status, arrest_location, arrested_at, created_at, updated_at) "
        "VALUES ($1, $2, $3, $4, $5, 'IN_CUSTODY', $6, NOW(), NOW(), NOW()) "
        "RETURNING arrest_id;";

    static constexpr const char *SQL_GET_STATUS =
        "SELECT custody_status FROM subsystem3.arrests WHERE arrest_id = $1;";

    static constexpr const char *SQL_UPDATE_STATUS =
        "UPDATE subsystem3.arrests "
        "SET    custody_status = $1, release_reason = $2, "
        "       released_at = CASE WHEN $1 IN ('RELEASED','ESCAPED') THEN NOW() ELSE released_at END, "
        "       updated_at = NOW() "
        "WHERE  arrest_id = $3;";

    static constexpr const char *SQL_SET_DISPUTED =
        "UPDATE subsystem3.arrests "
        "SET    is_disputed = TRUE, dispute_reason = $1, updated_at = NOW() "
        "WHERE  arrest_id = $2;";

    static constexpr const char *SQL_EXECUTE_WARRANT_ON_ARREST =
        "UPDATE subsystem3.warrants "
        "SET    warrant_status = 'EXECUTED', executed_by = $1, "
        "       executed_at = NOW(), updated_at = NOW() "
        "WHERE  warrant_id = $2 AND warrant_status = 'ISSUED';";

    static constexpr const char *SQL_PERSON_EXISTS =
        "SELECT 1 FROM subsystem2.persons WHERE cnic = $1 LIMIT 1;";

    static constexpr const char *SQL_SELECT_BY_CASE =
        "SELECT a.arrest_id, a.arrest_number, a.accused_cnic, a.case_id, "
        "       COALESCE(a.warrant_id, -1), a.arresting_officer_id, "
        "       a.custody_status, "
        "       COALESCE(a.arrest_location, '') AS arrest_location, "
        "       COALESCE(a.release_reason, '') AS release_reason, "
        "       COALESCE(a.dispute_reason, '') AS dispute_reason, "
        "       a.is_disputed, "
        "       EXTRACT(EPOCH FROM a.arrested_at)::BIGINT, "
        "       COALESCE(EXTRACT(EPOCH FROM a.released_at)::BIGINT, 0), "
        "       EXTRACT(EPOCH FROM a.created_at)::BIGINT, "
        "       EXTRACT(EPOCH FROM a.updated_at)::BIGINT "
        "FROM   subsystem3.arrests a "
        "WHERE  a.case_id = $1 "
        "ORDER  BY a.arrested_at DESC;";

    static constexpr const char *SQL_SET_SESSION =
        "SELECT set_config('app.current_officer_id', $1, true), "
        "       set_config('app.current_belt_number', $2, true);";

    // -----------------------------------------------------------------------
    // State machine table
    // -----------------------------------------------------------------------

    static const struct
    {
        const char *from;
        const char *to;
    } TRANSITIONS[] = {
        {"IN_CUSTODY", "BAIL_GRANTED"},
        {"IN_CUSTODY", "REMANDED"},
        {"IN_CUSTODY", "RELEASED"},
        {"IN_CUSTODY", "ESCAPED"},
        {"BAIL_GRANTED", "RELEASED"},
        {"REMANDED", "RELEASED"},
    };

    bool ArrestManager::_isLegalTransition(const char *from, const char *to)
    {
        for (const auto &t : TRANSITIONS)
            if (std::strcmp(t.from, from) == 0 && std::strcmp(t.to, to) == 0)
                return true;
        return false;
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    bool ArrestManager::_setSessionVars(PGconn *conn, int officer_id, const char *belt)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", officer_id);
        const char *params[] = {p1, belt};
        PGresult *res = PQexecParams(conn, SQL_SET_SESSION, 2, nullptr, params, nullptr, nullptr, 0);
        bool ok = (PQresultStatus(res) == PGRES_TUPLES_OK);
        if (!ok)
            Logger::error("arrest_manager: Failed to set session variables");
        PQclear(res);
        return ok;
    }

    bool ArrestManager::_personExists(PGconn *conn, const char *cnic)
    {
        const char *params[] = {cnic};
        PGresult *res = PQexecParams(conn, SQL_PERSON_EXISTS, 1, nullptr, params, nullptr, nullptr, 0);
        bool found = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0);
        PQclear(res);
        return found;
    }

    bool ArrestManager::_validateTransition(PGconn *conn,
                                            int arrest_id,
                                            const char *target_status,
                                            char out_current[ARREST_STATUS_LEN],
                                            ResultCode &out_code)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", arrest_id);
        const char *params[] = {p1};

        PGresult *res = PQexecParams(conn, SQL_GET_STATUS, 1, nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("arrest_manager: Arrest not found");
            PQclear(res);
            return false;
        }

        const char *current = PQgetvalue(res, 0, 0);
        std::strncpy(out_current, current, ARREST_STATUS_LEN - 1);
        out_current[ARREST_STATUS_LEN - 1] = '\0';
        PQclear(res);

        if (!_isLegalTransition(current, target_status))
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "arrest_manager: Illegal custody transition %s→%s", current, target_status);
            Logger::debug(msg);
            out_code = ResultCode::INVALID_STATE;
            return false;
        }
        return true;
    }

    void ArrestManager::_mapRow(PGresult *res, int row, ArrestRecord &rec)
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
        rec.arrest_id = std::atoi(PQgetvalue(res, row, 0));
        scopy(rec.arrest_number, PQgetvalue(res, row, 1), ARREST_NUM_LEN);
        scopy(rec.accused_cnic, PQgetvalue(res, row, 2), ARREST_CNIC_LEN);
        rec.case_id = std::atoi(PQgetvalue(res, row, 3));
        rec.warrant_id = std::atoi(PQgetvalue(res, row, 4));
        rec.arresting_officer_id = std::atoi(PQgetvalue(res, row, 5));
        scopy(rec.custody_status, PQgetvalue(res, row, 6), ARREST_STATUS_LEN);
        scopy(rec.arrest_location, PQgetvalue(res, row, 7), ARREST_LOCATION_LEN);
        scopy(rec.release_reason, PQgetvalue(res, row, 8), ARREST_REASON_LEN);
        scopy(rec.dispute_reason, PQgetvalue(res, row, 9), ARREST_REASON_LEN);
        rec.is_disputed = (std::strcmp(PQgetvalue(res, row, 10), "t") == 0);
        rec.arrested_at = (time_t)std::atoll(PQgetvalue(res, row, 11));
        rec.released_at = (time_t)std::atoll(PQgetvalue(res, row, 12));
        rec.created_at = (time_t)std::atoll(PQgetvalue(res, row, 13));
        rec.updated_at = (time_t)std::atoll(PQgetvalue(res, row, 14));
    }

    // Custody status enum → DB string
    static const char *custodyStr(CustodyStatus s)
    {
        switch (s)
        {
        case CustodyStatus::IN_CUSTODY:
            return "IN_CUSTODY";
        case CustodyStatus::BAIL_GRANTED:
            return "BAIL_GRANTED";
        case CustodyStatus::REMANDED:
            return "REMANDED";
        case CustodyStatus::RELEASED:
            return "RELEASED";
        case CustodyStatus::ESCAPED:
            return "ESCAPED";
        default:
            return "UNKNOWN";
        }
    }

    // -----------------------------------------------------------------------
    // recordArrest
    // -----------------------------------------------------------------------

    bool ArrestManager::recordArrest(PGconn *conn,
                                     const JusticeFlow::SessionContext &session,
                                     int case_id,
                                     const char *accused_cnic,
                                     const char *arrest_location,
                                     int warrant_id,
                                     int &out_arrest_id,
                                     ResultCode &out_code)
    {
        // 1. Verify person exists (chain of custody requirement)
        if (!_personExists(conn, accused_cnic))
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("arrest_manager: Accused CNIC not found in Persons table");
            return false;
        }

        // 2. Set session vars for audit trigger
        if (!_setSessionVars(conn, session.officerId, session.belt_number.c_str()))
        {
            out_code = ResultCode::DB_ERROR;
            return false;
        }

        // 3. Generate arrest number: AR-<epoch>-<officer_id>
        char arrest_num[ARREST_NUM_LEN];
        std::snprintf(arrest_num, sizeof(arrest_num), "AR-%lld-%d",
                      (long long)std::time(nullptr), session.officerId);

        // 4. Prepare parameters — warrant_id may be NULL
        char case_str[24], officer_str[24], warrant_str[24];
        std::snprintf(case_str, sizeof(case_str), "%d", case_id);
        std::snprintf(officer_str, sizeof(officer_str), "%d", session.officerId);

        const char *warrant_param;
        if (warrant_id > 0)
        {
            std::snprintf(warrant_str, sizeof(warrant_str), "%d", warrant_id);
            warrant_param = warrant_str;
        }
        else
        {
            warrant_param = nullptr; // NULL in DB
        }

        const char *params[] = {
            arrest_num,
            accused_cnic,
            case_str,
            warrant_param, // may be NULL — PQexecParams handles this
            officer_str,
            arrest_location};

        // For NULL params, we must pass param lengths and formats properly.
        // NULL is represented by a NULL pointer in the params array.
        // PQexecParams with text format: NULL pointer = SQL NULL.
        int param_lengths[] = {0, 0, 0, 0, 0, 0}; // 0 = use strlen for text
        int param_formats[] = {0, 0, 0, 0, 0, 0}; // 0 = text

        PGresult *res = PQexecParams(conn, SQL_INSERT_ARREST, 6,
                                     nullptr, params,
                                     param_lengths, param_formats, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
        {
            Logger::error("arrest_manager: INSERT failed");
            Logger::error(PQresultErrorMessage(res));
            out_code = ResultCode::DB_ERROR;
            PQclear(res);
            return false;
        }

        out_arrest_id = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        // 5. If warrant_id provided, mark it EXECUTED
        if (warrant_id > 0)
        {
            char p1[24], p2[24];
            std::snprintf(p1, sizeof(p1), "%d", session.officerId);
            std::snprintf(p2, sizeof(p2), "%d", warrant_id);
            const char *wparams[] = {p1, p2};
            PGresult *wres = PQexecParams(conn, SQL_EXECUTE_WARRANT_ON_ARREST, 2,
                                          nullptr, wparams, nullptr, nullptr, 0);
            // Non-fatal if warrant update fails — arrest is already recorded
            if (PQresultStatus(wres) != PGRES_COMMAND_OK)
                Logger::error("arrest_manager: Warning — failed to mark warrant EXECUTED");
            PQclear(wres);
        }

        out_code = ResultCode::OK;
        Logger::info("arrest_manager: Arrest recorded");
        return true;
    }

    // -----------------------------------------------------------------------
    // updateCustodyStatus
    // -----------------------------------------------------------------------

    bool ArrestManager::updateCustodyStatus(PGconn *conn,
                                            const JusticeFlow::SessionContext &session,
                                            int arrest_id,
                                            CustodyStatus new_status,
                                            const char *reason,
                                            ResultCode &out_code)
    {
        const char *target = custodyStr(new_status);

        // Require reason for terminal states
        if ((new_status == CustodyStatus::RELEASED || new_status == CustodyStatus::ESCAPED) && (!reason || reason[0] == '\0'))
        {
            out_code = ResultCode::INVALID_INPUT;
            Logger::error("arrest_manager: reason required for RELEASED/ESCAPED transition");
            return false;
        }

        // Validate transition
        char current[ARREST_STATUS_LEN];
        if (!_validateTransition(conn, arrest_id, target, current, out_code))
            return false;

        // Set session vars
        if (!_setSessionVars(conn, session.officerId, session.belt_number.c_str()))
        {
            out_code = ResultCode::DB_ERROR;
            return false;
        }

        // Update
        char p3[24];
        std::snprintf(p3, sizeof(p3), "%d", arrest_id);
        const char *reason_val = (reason && reason[0]) ? reason : "";
        const char *params[] = {target, reason_val, p3};

        PGresult *res = PQexecParams(conn, SQL_UPDATE_STATUS, 3,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("arrest_manager: updateCustodyStatus UPDATE failed");
            Logger::error(PQresultErrorMessage(res));
            out_code = ResultCode::DB_ERROR;
            PQclear(res);
            return false;
        }
        PQclear(res);

        out_code = ResultCode::OK;
        Logger::info("arrest_manager: Custody status updated");
        return true;
    }

    // -----------------------------------------------------------------------
    // markAsDisputed
    // -----------------------------------------------------------------------

    bool ArrestManager::markAsDisputed(PGconn *conn,
                                       const JusticeFlow::SessionContext &session,
                                       int arrest_id,
                                       const char *dispute_reason,
                                       ResultCode &out_code)
    {
        if (!dispute_reason || dispute_reason[0] == '\0')
        {
            out_code = ResultCode::INVALID_INPUT;
            Logger::error("arrest_manager: dispute_reason must not be empty");
            return false;
        }

        if (!_setSessionVars(conn, session.officerId, session.belt_number.c_str()))
        {
            out_code = ResultCode::DB_ERROR;
            return false;
        }

        char p2[24];
        std::snprintf(p2, sizeof(p2), "%d", arrest_id);
        const char *params[] = {dispute_reason, p2};

        PGresult *res = PQexecParams(conn, SQL_SET_DISPUTED, 2,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("arrest_manager: markAsDisputed UPDATE failed");
            Logger::error(PQresultErrorMessage(res));
            out_code = ResultCode::DB_ERROR;
            PQclear(res);
            return false;
        }
        PQclear(res);

        out_code = ResultCode::OK;
        Logger::info("arrest_manager: Arrest marked as disputed");
        return true;
    }

    // -----------------------------------------------------------------------
    // getArrestsByCase
    // -----------------------------------------------------------------------

    ResultCode ArrestManager::getArrestsByCase(PGconn *conn,
                                               int case_id,
                                               std::vector<ArrestRecord> &out)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", case_id);
        const char *params[] = {p1};

        PGresult *res = PQexecParams(conn, SQL_SELECT_BY_CASE, 1,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("arrest_manager: getArrestsByCase query failed");
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
            ArrestRecord rec;
            _mapRow(res, r, rec);
            out.push_back(rec);
        }
        PQclear(res);
        Logger::info("arrest_manager: getArrestsByCase succeeded");
        return ResultCode::OK;
    }

} // namespace enforcement