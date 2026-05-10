#pragma once

/**
 * @file bail_manager.h
 * @brief Bail record lifecycle — state machine + 3 operations.
 *
 * State Machine
 * -------------
 *   ACTIVE ──→ REVOKED    (SHO revokes with reason, INSPECTOR+ rank required)
 *          ──→ EXPIRED    (valid_until < today — set by nightly SQL job)
 *          ──→ CANCELLED  (court cancels)
 *   REVOKED, EXPIRED, CANCELLED are terminal — no further transitions.
 *
 * Key invariants:
 *   - recordBail() checks that the arrest is IN_CUSTODY before inserting.
 *     It would be a logic error to grant bail to someone already released.
 *   - valid_until is nullable — personal recognizance bail has no expiry.
 *     When it is NULL the nightly SQL job skips the record.
 *   - SURETY bail requires the three surety fields to be non-empty:
 *     surety_name, surety_cnic, surety_contact. BailManager validates this.
 *   - Bail amount is validated against Compliance::validateBailAmount()
 *     before any DB write.
 *   - revokeBail() updates the linked arrest's custody_status back to IN_CUSTODY.
 *
 * Dependencies
 * ------------
 * enforcement/include/arrest_manager.h (verifies IN_CUSTODY precondition)
 * legal/include/compliance.h (bail amount validation)
 * shr_infra/auth/include/auth_module.h
 * common/constants.h, libpq
 */

#include <vector>
#include <cstdint>
#include <ctime>
#include <postgresql/libpq-fe.h>
#include "common/constants.h"
#include "common/common.h"

namespace enforcement
{

    // -----------------------------------------------------------------------
    // Field size constants
    // -----------------------------------------------------------------------
    constexpr int BAIL_NUM_LEN = 32;
    constexpr int BAIL_STATUS_LEN = 16;
    constexpr int BAIL_TYPE_LEN = 16;
    constexpr int BAIL_COURT_LEN = 128;
    constexpr int BAIL_JUDGE_LEN = 128;
    constexpr int BAIL_REASON_LEN = 512;
    constexpr int BAIL_CNIC_LEN = 16;
    constexpr int BAIL_NAME_LEN = 128;
    constexpr int BAIL_CONTACT_LEN = 32;
    constexpr int BAIL_DATE_LEN = 32;

    /**
     * @struct BailRecord
     * @brief Plain data struct mirroring subsystem3.bail_records columns.
     */
    struct BailRecord
    {
        int bail_id;
        char bail_number[BAIL_NUM_LEN];
        int arrest_id;
        char bail_status[BAIL_STATUS_LEN]; // ACTIVE | REVOKED | EXPIRED | CANCELLED
        char bail_type[BAIL_TYPE_LEN];     // REGULAR | ANTICIPATORY | INTERIM | SURETY
        uint64_t bail_amount_paise;
        char court_name[BAIL_COURT_LEN];
        char magistrate_name[BAIL_JUDGE_LEN];
        char valid_until[BAIL_DATE_LEN]; // empty string = no expiry (recognizance)
        char surety_name[BAIL_NAME_LEN]; // SURETY bail only
        char surety_cnic[BAIL_CNIC_LEN];
        char surety_contact[BAIL_CONTACT_LEN];
        char revocation_reason[BAIL_REASON_LEN];
        int recorded_by;
        int revoked_by;
        time_t bail_date;
        time_t revoked_at;
        time_t created_at;
        time_t updated_at;
    };

    /**
     * @class BailManager
     * @brief Stateless manager for all bail operations.
     */
    class BailManager
    {
    public:
        /**
         * Records bail for an arrested person (initial state: ACTIVE).
         *
         * Pre-conditions:
         *   - Arrest must exist and be in IN_CUSTODY state.
         *   - bail_amount_paise must pass Compliance::validateBailAmount().
         *   - For SURETY bail: surety_name, surety_cnic, surety_contact must
         *     all be non-empty; method returns INVALID_INPUT if any are missing.
         *   - Officer rank >= INSPECTOR.
         *
         * On success:
         *   - Inserts into subsystem3.bail_records with bail_status = 'ACTIVE'
         *   - Updates linked arrest's custody_status to BAIL_GRANTED
         *   - Audit trigger fires on both writes
         *
         * @param conn               Active PGconn*.
         * @param session            Authenticated officer's session.
         * @param arrest_id          Arrest this bail is for.
         * @param bail_type          REGULAR | ANTICIPATORY | INTERIM | SURETY.
         * @param bail_amount_paise  Amount in paise (100 paise = ₹1).
         * @param court_name         Granting court name.
         * @param magistrate_name    Authorising magistrate.
         * @param valid_until        ISO date string or NULL/"" for no expiry.
         * @param surety_name        Required for SURETY; empty otherwise.
         * @param surety_cnic        Required for SURETY; empty otherwise.
         * @param surety_contact     Required for SURETY; empty otherwise.
         * @param out_bail_id        Set to new bail_id on success.
         * @param out_code           Result code.
         * @return true on success.
         */
        static bool recordBail(PGconn *conn,
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
                               JusticeFlow::ResultCode &out_code);

        /**
         * Revokes an ACTIVE bail record (ACTIVE → REVOKED).
         *
         * Requires:
         *   - Bail is in ACTIVE state.
         *   - Officer rank >= INSPECTOR (SHO level).
         *   - revocation_reason must not be empty.
         *
         * On success:
         *   - Updates bail_status to REVOKED, sets revoked_by / revoked_at
         *   - Updates linked arrest's custody_status back to IN_CUSTODY
         *   - Audit trigger fires on both writes
         *
         * @param conn               Active PGconn*.
         * @param session            Authenticated officer's session.
         * @param bail_id            Bail to revoke.
         * @param revocation_reason  Non-empty reason (required for audit trail).
         * @param out_code           Result code.
         * @return true on success.
         */
        static bool revokeBail(PGconn *conn,
                               const JusticeFlow::SessionContext &session,
                               int bail_id,
                               const char *revocation_reason,
                               JusticeFlow::ResultCode &out_code);

        /**
         * Retrieves the current bail record for an arrest.
         *
         * Returns the most recently created ACTIVE bail. If no ACTIVE bail
         * exists, returns the most recent record of any status.
         *
         * @param conn      Active PGconn*.
         * @param arrest_id Arrest to query.
         * @param out       Populated with the bail record.
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        static JusticeFlow::ResultCode getBailByArrest(PGconn *conn,
                                                       int arrest_id,
                                                       BailRecord &out);

    private:
        /**
         * @brief Validates the bail record's current state and that the
         *        transition to target_status is legal.
         *
         * @param conn          Active PGconn*.
         * @param bail_id       Bail to check.
         * @param target_status Target status string (e.g. "REVOKED").
         * @param out_arrest_id Populated with the linked arrest_id on success.
         * @param out_code      Set to INVALID_STATE / NOT_FOUND on failure.
         * @return true if transition is legal.
         */
        static bool _validateState(PGconn *conn,
                                   int bail_id,
                                   const char *target_status,
                                   int &out_arrest_id,
                                   JusticeFlow::ResultCode &out_code);

        /**
         * @brief Verifies arrest is in IN_CUSTODY state (pre-condition for recordBail).
         */
        static bool _arrestIsInCustody(PGconn *conn, int arrest_id);

        /**
         * @brief Converts BailType enum to the DB string representation.
         */
        static const char *_bailTypeStr(JusticeFlow::BailType t);

        static bool _setSessionVars(PGconn *conn, int officer_id, const char *belt);
        static void _mapRow(PGresult *res, int row, BailRecord &out);
    };

} // namespace enforcement