/**
 * @file bail_manager.cpp
 * @brief BailManager implementation.
 *
 * State machine:
 *   ACTIVE → REVOKED | EXPIRED | CANCELLED   (all terminal)
 *
 * SQL column contract for _mapRow:
 *   0  bail_id
 *   1  bail_number
 *   2  arrest_id
 *   3  bail_status
 *   4  bail_type
 *   5  bail_amount_paise
 *   6  court_name
 *   7  magistrate_name
 *   8  valid_until (ISO date string, empty if NULL)
 *   9  surety_name
 *   10 surety_cnic
 *   11 surety_contact
 *   12 revocation_reason
 *   13 recorded_by
 *   14 revoked_by (0 if NULL)
 *   15 EXTRACT(EPOCH FROM bail_date)
 *   16 EXTRACT(EPOCH FROM revoked_at)  (0 if NULL)
 *   17 EXTRACT(EPOCH FROM created_at)
 *   18 EXTRACT(EPOCH FROM updated_at)
 */

#include "enforcement/include/bail_manager.h"
#include "legal/include/compliance.h"
#include "shr_infra/auth/include/auth_module.h"
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

    static constexpr const char *SQL_INSERT_BAIL =
        "INSERT INTO subsystem3.bail_records "
        "  (bail_number, arrest_id, bail_status, bail_type, bail_amount, "
        "   court_name, magistrate_name, valid_until, "
        "   surety_name, surety_cnic, surety_contact, "
        "   recorded_by, bail_date, created_at, updated_at) "
        "VALUES ($1, $2, 'ACTIVE', $3, $4, "
        "        $5, $6, NULLIF($7, '')::DATE, "
        "        $8, $9, $10, "
        "        $11, NOW(), NOW(), NOW()) "
        "RETURNING bail_id;";

    static constexpr const char *SQL_UPDATE_ARREST_BAIL_GRANTED =
        "UPDATE subsystem3.arrests "
        "SET    custody_status = 'BAIL_GRANTED', updated_at = NOW() "
        "WHERE  arrest_id = $1 AND custody_status = 'IN_CUSTODY';";

    static constexpr const char *SQL_GET_BAIL_STATUS =
        "SELECT bail_status, arrest_id "
        "FROM   subsystem3.bail_records WHERE bail_id = $1;";

    static constexpr const char *SQL_REVOKE_BAIL =
        "UPDATE subsystem3.bail_records "
        "SET    bail_status = 'REVOKED', revoked_by = $1, "
        "       revocation_reason = $2, revoked_at = NOW(), updated_at = NOW() "
        "WHERE  bail_id = $3;";

    static constexpr const char *SQL_RESTORE_CUSTODY =
        "UPDATE subsystem3.arrests "
        "SET    custody_status = 'IN_CUSTODY', updated_at = NOW() "
        "WHERE  arrest_id = $1;";

    static constexpr const char *SQL_GET_BY_ARREST =
        "SELECT b.bail_id, b.bail_number, b.arrest_id, b.bail_status, b.bail_type, "
        "       b.bail_amount, b.court_name, b.magistrate_name, "
        "       COALESCE(b.valid_until::TEXT, '') AS valid_until, "
        "       COALESCE(b.surety_name, '') AS surety_name, "
        "       COALESCE(b.surety_cnic, '') AS surety_cnic, "
        "       COALESCE(b.surety_contact, '') AS surety_contact, "
        "       COALESCE(b.revocation_reason, '') AS revocation_reason, "
        "       b.recorded_by, "
        "       COALESCE(b.revoked_by, 0), "
        "       EXTRACT(EPOCH FROM b.bail_date)::BIGINT, "
        "       COALESCE(EXTRACT(EPOCH FROM b.revoked_at)::BIGINT, 0), "
        "       EXTRACT(EPOCH FROM b.created_at)::BIGINT, "
        "       EXTRACT(EPOCH FROM b.updated_at)::BIGINT "
        "FROM   subsystem3.bail_records b "
        "WHERE  b.arrest_id = $1 "
        "ORDER  BY (b.bail_status = 'ACTIVE') DESC, b.created_at DESC "
        "LIMIT  1;";

    static constexpr const char *SQL_ARREST_IN_CUSTODY =
        "SELECT 1 FROM subsystem3.arrests "
        "WHERE  arrest_id = $1 AND custody_status = 'IN_CUSTODY' LIMIT 1;";

    static constexpr const char *SQL_SET_SESSION =
        "SELECT set_config('app.current_officer_id', $1, true), "
        "       set_config('app.current_belt_number', $2, true);";

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    const char *BailManager::_bailTypeStr(BailType t)
    {
        switch (t)
        {
        case BailType::REGULAR:
            return "REGULAR";
        case BailType::ANTICIPATORY:
            return "ANTICIPATORY";
        case BailType::INTERIM:
            return "INTERIM";
        case BailType::SURETY:
            return "SURETY";
        default:
            return "UNKNOWN";
        }
    }

    bool BailManager::_setSessionVars(PGconn *conn, int officer_id, const char *belt)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", officer_id);
        const char *params[] = {p1, belt};
        PGresult *res = PQexecParams(conn, SQL_SET_SESSION, 2, nullptr, params, nullptr, nullptr, 0);
        bool ok = (PQresultStatus(res) == PGRES_TUPLES_OK);
        if (!ok)
            Logger::error("bail_manager: Failed to set session variables");
        PQclear(res);
        return ok;
    }

    bool BailManager::_arrestIsInCustody(PGconn *conn, int arrest_id)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", arrest_id);
        const char *params[] = {p1};
        PGresult *res = PQexecParams(conn, SQL_ARREST_IN_CUSTODY, 1,
                                     nullptr, params, nullptr, nullptr, 0);
        bool found = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0);
        PQclear(res);
        return found;
    }

    bool BailManager::_validateState(PGconn *conn,
                                     int bail_id,
                                     const char *target_status,
                                     int &out_arrest_id,
                                     ResultCode &out_code)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", bail_id);
        const char *params[] = {p1};

        PGresult *res = PQexecParams(conn, SQL_GET_BAIL_STATUS, 1,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("bail_manager: Bail record not found");
            PQclear(res);
            return false;
        }

        const char *current = PQgetvalue(res, 0, 0);
        out_arrest_id = std::atoi(PQgetvalue(res, 0, 1));
        PQclear(res);

        // Only ACTIVE bail can be transitioned
        if (std::strcmp(current, "ACTIVE") != 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "bail_manager: Bail is %s, cannot transition to %s", current, target_status);
            Logger::debug(msg);
            out_code = ResultCode::INVALID_STATE;
            return false;
        }
        return true;
    }

    void BailManager::_mapRow(PGresult *res, int row, BailRecord &out)
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

        out.bail_id = std::atoi(PQgetvalue(res, row, 0));
        scopy(out.bail_number, PQgetvalue(res, row, 1), BAIL_NUM_LEN);
        out.arrest_id = std::atoi(PQgetvalue(res, row, 2));
        scopy(out.bail_status, PQgetvalue(res, row, 3), BAIL_STATUS_LEN);
        scopy(out.bail_type, PQgetvalue(res, row, 4), BAIL_TYPE_LEN);
        out.bail_amount_paise = (uint64_t)std::atoll(PQgetvalue(res, row, 5));
        scopy(out.court_name, PQgetvalue(res, row, 6), BAIL_COURT_LEN);
        scopy(out.magistrate_name, PQgetvalue(res, row, 7), BAIL_JUDGE_LEN);
        scopy(out.valid_until, PQgetvalue(res, row, 8), BAIL_DATE_LEN);
        scopy(out.surety_name, PQgetvalue(res, row, 9), BAIL_NAME_LEN);
        scopy(out.surety_cnic, PQgetvalue(res, row, 10), BAIL_CNIC_LEN);
        scopy(out.surety_contact, PQgetvalue(res, row, 11), BAIL_CONTACT_LEN);
        scopy(out.revocation_reason, PQgetvalue(res, row, 12), BAIL_REASON_LEN);
        out.recorded_by = std::atoi(PQgetvalue(res, row, 13));
        out.revoked_by = std::atoi(PQgetvalue(res, row, 14));
        out.bail_date = (time_t)std::atoll(PQgetvalue(res, row, 15));
        out.revoked_at = (time_t)std::atoll(PQgetvalue(res, row, 16));
        out.created_at = (time_t)std::atoll(PQgetvalue(res, row, 17));
        out.updated_at = (time_t)std::atoll(PQgetvalue(res, row, 18));
    }

    // -----------------------------------------------------------------------
    // recordBail
    // -----------------------------------------------------------------------

    bool BailManager::recordBail(PGconn *conn,
                                 const SessionContext &session,
                                 int arrest_id,
                                 BailType bail_type,
                                 uint64_t bail_amount_paise,
                                 const char *court_name,
                                 const char *magistrate_name,
                                 const char *valid_until,
                                 const char *surety_name,
                                 const char *surety_cnic,
                                 const char *surety_contact,
                                 int &out_bail_id,
                                 ResultCode &out_code)
    {
        // 1. Rank check — INSPECTOR minimum
        bool rank_ok = false;
        AuthManager::validateRank(session.officerId, OfficerRank::INSPECTOR, rank_ok);
        if (!rank_ok)
        {
            out_code = ResultCode::RANK_INSUFFICIENT;
            Logger::debug("bail_manager: Rank insufficient for bail");
            return false;
        }

        // 2. Arrest must be IN_CUSTODY
        if (!_arrestIsInCustody(conn, arrest_id))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("bail_manager: Arrest is not IN_CUSTODY — cannot record bail");
            return false;
        }

        // 3. Validate bail amount against compliance rules
        legal::ComplianceResult compliance =
            legal::Compliance::validateBailAmount(bail_type, bail_amount_paise);
        if (compliance.code != ResultCode::OK)
        {
            out_code = compliance.code;
            Logger::error("bail_manager: Bail amount failed compliance check");
            Logger::error(compliance.reason.c_str());
            return false;
        }

        // 4. SURETY bail: all three surety fields required
        if (bail_type == BailType::SURETY)
        {
            bool missing_surety = (!surety_name || surety_name[0] == '\0') ||
                                  (!surety_cnic || surety_cnic[0] == '\0') ||
                                  (!surety_contact || surety_contact[0] == '\0');
            if (missing_surety)
            {
                out_code = ResultCode::INVALID_INPUT;
                Logger::error("bail_manager: SURETY bail requires surety_name, surety_cnic, surety_contact");
                return false;
            }
        }

        // 5. Set session vars for audit trigger
        if (!_setSessionVars(conn, session.officerId, session.beltNumber))
        {
            out_code = ResultCode::DB_ERROR;
            return false;
        }

        // 6. Generate bail number
        char bail_num[BAIL_NUM_LEN];
        std::snprintf(bail_num, sizeof(bail_num), "BA-%lld-%d",
                      (long long)std::time(nullptr), session.officerId);

        // 7. Build parameters
        char arrest_str[24], amount_str[32], officer_str[24];
        std::snprintf(arrest_str, sizeof(arrest_str), "%d", arrest_id);
        std::snprintf(amount_str, sizeof(amount_str), "%llu", (unsigned long long)bail_amount_paise);
        std::snprintf(officer_str, sizeof(officer_str), "%d", session.officerId);

        // Use empty strings for optional surety fields when bail is not SURETY
        const char *s_name = (surety_name && surety_name[0]) ? surety_name : "";
        const char *s_cnic = (surety_cnic && surety_cnic[0]) ? surety_cnic : "";
        const char *s_contact = (surety_contact && surety_contact[0]) ? surety_contact : "";
        const char *v_until = (valid_until && valid_until[0]) ? valid_until : "";

        const char *params[] = {
            bail_num,
            arrest_str,
            _bailTypeStr(bail_type),
            amount_str,
            court_name,
            magistrate_name,
            v_until, // NULLIF($7,'')::DATE handles empty → NULL
            s_name,
            s_cnic,
            s_contact,
            officer_str};

        PGresult *res = PQexecParams(conn, SQL_INSERT_BAIL, 11,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
        {
            Logger::error("bail_manager: INSERT failed");
            Logger::error(PQresultErrorMessage(res));
            out_code = ResultCode::DB_ERROR;
            PQclear(res);
            return false;
        }
        out_bail_id = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        // 8. Update arrest custody status to BAIL_GRANTED
        char ap1[24];
        std::snprintf(ap1, sizeof(ap1), "%d", arrest_id);
        const char *aparams[] = {ap1};
        PGresult *ares = PQexecParams(conn, SQL_UPDATE_ARREST_BAIL_GRANTED, 1,
                                      nullptr, aparams, nullptr, nullptr, 0);
        if (PQresultStatus(ares) != PGRES_COMMAND_OK)
            Logger::error("bail_manager: Warning — failed to update arrest to BAIL_GRANTED");
        PQclear(ares);

        out_code = ResultCode::OK;
        Logger::info("bail_manager: Bail recorded");
        return true;
    }

    // -----------------------------------------------------------------------
    // revokeBail
    // -----------------------------------------------------------------------

    bool BailManager::revokeBail(PGconn *conn,
                                 const SessionContext &session,
                                 int bail_id,
                                 const char *revocation_reason,
                                 ResultCode &out_code)
    {
        if (!revocation_reason || revocation_reason[0] == '\0')
        {
            out_code = ResultCode::INVALID_INPUT;
            Logger::error("bail_manager: revocation_reason must not be empty");
            return false;
        }

        // 1. Rank check — INSPECTOR minimum
        bool rank_ok = false;
        AuthManager::validateRank(session.officerId, OfficerRank::INSPECTOR, rank_ok);
        if (!rank_ok)
        {
            out_code = ResultCode::RANK_INSUFFICIENT;
            return false;
        }

        // 2. State validation — must be ACTIVE
        int arrest_id = 0;
        if (!_validateState(conn, bail_id, "REVOKED", arrest_id, out_code))
            return false;

        // 3. Set session vars
        if (!_setSessionVars(conn, session.officerId, session.beltNumber))
        {
            out_code = ResultCode::DB_ERROR;
            return false;
        }

        // 4. Revoke bail
        char p1[24], p3[24];
        std::snprintf(p1, sizeof(p1), "%d", session.officerId);
        std::snprintf(p3, sizeof(p3), "%d", bail_id);
        const char *params[] = {p1, revocation_reason, p3};

        PGresult *res = PQexecParams(conn, SQL_REVOKE_BAIL, 3,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("bail_manager: REVOKE UPDATE failed");
            Logger::error(PQresultErrorMessage(res));
            out_code = ResultCode::DB_ERROR;
            PQclear(res);
            return false;
        }
        PQclear(res);

        // 5. Restore arrest to IN_CUSTODY
        char rp1[24];
        std::snprintf(rp1, sizeof(rp1), "%d", arrest_id);
        const char *rparams[] = {rp1};
        PGresult *rres = PQexecParams(conn, SQL_RESTORE_CUSTODY, 1,
                                      nullptr, rparams, nullptr, nullptr, 0);
        if (PQresultStatus(rres) != PGRES_COMMAND_OK)
            Logger::error("bail_manager: Warning — failed to restore arrest to IN_CUSTODY");
        PQclear(rres);

        out_code = ResultCode::OK;
        Logger::info("bail_manager: Bail revoked, arrest restored to IN_CUSTODY");
        return true;
    }

    // -----------------------------------------------------------------------
    // getBailByArrest
    // -----------------------------------------------------------------------

    ResultCode BailManager::getBailByArrest(PGconn *conn,
                                            int arrest_id,
                                            BailRecord &out)
    {
        char p1[24];
        std::snprintf(p1, sizeof(p1), "%d", arrest_id);
        const char *params[] = {p1};

        PGresult *res = PQexecParams(conn, SQL_GET_BY_ARREST, 1,
                                     nullptr, params, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            Logger::error("bail_manager: getBailByArrest query failed");
            PQclear(res);
            return ResultCode::DB_ERROR;
        }
        if (PQntuples(res) == 0)
        {
            PQclear(res);
            return ResultCode::NOT_FOUND;
        }
        _mapRow(res, 0, out);
        PQclear(res);
        Logger::info("bail_manager: getBailByArrest succeeded");
        return ResultCode::OK;
    }

} // namespace enforcement