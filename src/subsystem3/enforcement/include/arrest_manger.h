#pragma once

/**
 * @file arrest_manager.h
 * @brief Arrest and custody lifecycle — state machine + 4 operations.
 *
 * State Machine
 * -------------
 *   IN_CUSTODY ──→ BAIL_GRANTED  (court grants bail)
 *              ──→ REMANDED      (court remands to custody)
 *              ──→ RELEASED      (released with reason — terminal)
 *              ──→ ESCAPED       (escape recorded — terminal)
 *   BAIL_GRANTED ──→ RELEASED
 *   REMANDED     ──→ RELEASED
 *   RELEASED and ESCAPED are terminal — no further transitions permitted.
 *
 * Key invariants:
 *   - warrant_id may be -1 for warrantless arrests (legally valid).
 *   - accused_cnic must exist in the Persons table before insertion.
 *   - reason is REQUIRED when transitioning to ESCAPED or RELEASED.
 *   - is_disputed flag is orthogonal to custody_status — can be set independently.
 *
 * Dependencies
 * ------------
 * shr_infra/auth/include/auth_module.h, common/constants.h, libpq
 */

#include <vector>
#include <ctime>
#include <libpq-fe.h>
#include "common/constants.h"
#include "common/common.h"

namespace enforcement
{

    // -----------------------------------------------------------------------
    // Field size constants
    // -----------------------------------------------------------------------
    constexpr int ARREST_NUM_LEN = 32;
    constexpr int ARREST_CNIC_LEN = 16;
    constexpr int ARREST_STATUS_LEN = 16;
    constexpr int ARREST_LOCATION_LEN = 256;
    constexpr int ARREST_REASON_LEN = 512;

    /**
     * @struct ArrestRecord
     * @brief Plain data struct mirroring subsystem3.arrests columns.
     */
    struct ArrestRecord
    {
        int arrest_id;
        char arrest_number[ARREST_NUM_LEN];
        char accused_cnic[ARREST_CNIC_LEN];
        int case_id;
        int warrant_id; // -1 for warrantless arrest
        int arresting_officer_id;
        char custody_status[ARREST_STATUS_LEN]; // IN_CUSTODY|BAIL_GRANTED|REMANDED|RELEASED|ESCAPED
        char arrest_location[ARREST_LOCATION_LEN];
        char release_reason[ARREST_REASON_LEN];
        char dispute_reason[ARREST_REASON_LEN];
        bool is_disputed;
        time_t arrested_at;
        time_t released_at;
        time_t created_at;
        time_t updated_at;
    };

    /**
     * @class ArrestManager
     * @brief Stateless manager for all arrest and custody operations.
     */
    class ArrestManager
    {
    public:
        /**
         * Records a new arrest (initial state: IN_CUSTODY).
         *
         * Pre-conditions:
         *   - If warrant_id != -1, the warrant must be in ISSUED state.
         *   - accused_cnic must exist in subsystem2.Persons table.
         *   - Officer must be on active duty.
         *   - Officer rank >= CONSTABLE (any serving officer may arrest).
         *
         * On success:
         *   - Inserts into subsystem3.arrests with custody_status = 'IN_CUSTODY'
         *   - If warrant_id != -1, updates warrant status to EXECUTED
         *   - Increments active_case_count on subsystem1.officers (via S1 bridge)
         *   - Audit trigger fires automatically
         *
         * @param conn            Active PGconn*.
         * @param session         Authenticated officer's session.
         * @param case_id         Case this arrest belongs to.
         * @param accused_cnic    CNIC of the person being arrested.
         * @param arrest_location Physical location where arrest was made.
         * @param warrant_id      Warrant being executed, or -1 for warrantless.
         * @param out_arrest_id   Set to new arrest_id on success.
         * @param out_code        Result code.
         * @return true on success.
         */
        static bool recordArrest(PGconn *conn,
                                 const JusticeFlow::SessionContext &session,
                                 int case_id,
                                 const char *accused_cnic,
                                 const char *arrest_location,
                                 int warrant_id,
                                 int &out_arrest_id,
                                 JusticeFlow::ResultCode &out_code);

        /**
         * Updates custody status (state transition).
         *
         * Validates the transition against the state machine before any DB write.
         * reason is required when transitioning to RELEASED or ESCAPED.
         *
         * @param conn        Active PGconn*.
         * @param session     Authenticated officer's session.
         * @param arrest_id   Arrest record to update.
         * @param new_status  Target custody status.
         * @param reason      Required for RELEASED/ESCAPED; may be empty otherwise.
         * @param out_code    Result code.
         * @return true on success.
         */
        static bool updateCustodyStatus(PGconn *conn,
                                        const JusticeFlow::SessionContext &session,
                                        int arrest_id,
                                        JusticeFlow::CustodyStatus new_status,
                                        const char *reason,
                                        JusticeFlow::ResultCode &out_code);

        /**
         * Flags an arrest as disputed (procedural violation claim).
         *
         * Sets is_disputed = TRUE and records the dispute_reason.
         * Dispute flag is independent of custody_status — a disputed arrest
         * can still transition through the normal custody states.
         * dispute_reason must not be empty.
         *
         * @param conn           Active PGconn*.
         * @param session        Session of the officer raising the dispute.
         * @param arrest_id      Arrest being disputed.
         * @param dispute_reason Non-empty description of the procedural violation.
         * @param out_code       Result code.
         * @return true on success.
         */
        static bool markAsDisputed(PGconn *conn,
                                   const JusticeFlow::SessionContext &session,
                                   int arrest_id,
                                   const char *dispute_reason,
                                   JusticeFlow::ResultCode &out_code);

        /**
         * Retrieves all arrests for a case.
         *
         * Read-only. No rank check required.
         * Results ordered by arrested_at descending.
         *
         * @param conn     Active PGconn*.
         * @param case_id  Case to query.
         * @param out      Vector appended with ArrestRecords.
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        static JusticeFlow::ResultCode getArrestsByCase(PGconn *conn,
                                                        int case_id,
                                                        std::vector<ArrestRecord> &out);

    private:
        /**
         * @brief Validates that the current DB state permits a custody transition.
         *
         * @param conn          Active PGconn*.
         * @param arrest_id     Arrest to check.
         * @param target_status Target status string (e.g. "RELEASED").
         * @param out_current   Populated with the current status string on success.
         * @param out_code      Set to INVALID_STATE or NOT_FOUND on failure.
         * @return true if transition is legal.
         */
        static bool _validateTransition(PGconn *conn,
                                        int arrest_id,
                                        const char *target_status,
                                        char out_current[ARREST_STATUS_LEN],
                                        JusticeFlow::ResultCode &out_code);

        /**
         * @brief Verifies that a CNIC exists in the Persons table.
         *
         * @param conn  Active PGconn*.
         * @param cnic  CNIC string to look up.
         * @return true if the person record exists.
         */
        static bool _personExists(PGconn *conn, const char *cnic);

        static bool _setSessionVars(PGconn *conn, int officer_id, const char *belt);
        static void _mapRow(PGresult *res, int row, ArrestRecord &rec);

        // Legal transitions map (checked at compile time — defined in .cpp)
        static bool _isLegalTransition(const char *from, const char *to);
    };

} // namespace enforcement