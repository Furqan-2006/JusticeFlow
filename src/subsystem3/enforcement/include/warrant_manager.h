#pragma once

/**
 * @file warrant_manager.h
 * @brief Warrant lifecycle — state machine + all 5 operations.
 *
 * Owns every warrant write and read operation for Subsystem 3.
 * No other module queries the Warrants table directly.
 *
 * State Machine
 * -------------
 *   ISSUED ──→ EXECUTED   (warrant served and arrest made)
 *          ──→ CANCELLED  (SHO cancels, INSPECTOR+ rank required)
 *          ──→ EXPIRED    (valid_until < today — set by nightly SQL job)
 *   EXECUTED, CANCELLED, EXPIRED are terminal — no further transitions.
 *
 * Authorization Chain (per operation)
 * ------------------------------------
 * Every write method enforces the following sequence before touching the DB:
 *   1. Caller's session token is validated by the auth layer before reaching here.
 *   2. AuthManager::validateRank() — minimum rank check.
 *   3. _validateState() — current DB state must allow the transition.
 *   4. PQexecParams write — parameterised, no string interpolation.
 *   5. Audit trigger fires automatically on the DB write.
 *
 * Session Variable
 * ----------------
 * _setSessionVars() runs "SET LOCAL app.current_officer_id = $1"
 * and "SET LOCAL app.current_belt_number = $2" before every INSERT/UPDATE.
 * This stamps the audit trigger with the correct officer identity.
 * SET LOCAL resets automatically at transaction end.
 *
 * SQL Injection
 * -------------
 * All queries use PQexecParams with $N placeholders.
 * No string concatenation is used anywhere in this module.
 *
 * Thread Safety
 * -------------
 * All methods are stateless. Thread-safe via the OS layer IPC connection pool.
 *
 * Dependencies
 * ------------
 * shr_infra/auth/include/auth_module.h — validateRank()
 * common/constants.h, libpq
 */

#include <vector>
#include <ctime>
#include <postgresql/libpq-fe.h>
#include "common/constants.h"
#include "common/common.h"

namespace enforcement
{

    // -----------------------------------------------------------------------
    // Field size constants — mirror Warrants table column widths
    // -----------------------------------------------------------------------
    constexpr int WARRANT_NUM_LEN = 32;
    constexpr int WARRANT_CNIC_LEN = 16; // Pakistani CNIC: 13 digits + dashes + NUL
    constexpr int WARRANT_COURT_LEN = 128;
    constexpr int WARRANT_JUDGE_LEN = 128;
    constexpr int WARRANT_ADDR_LEN = 256;
    constexpr int WARRANT_STATUS_LEN = 16;
    constexpr int WARRANT_TYPE_LEN = 16;
    constexpr int WARRANT_REASON_LEN = 512;
    constexpr int WARRANT_DATE_LEN = 32; // ISO date string

    /**
     * @struct WarrantRecord
     * @brief Plain data struct mirroring subsystem3.Warrants columns.
     *
     * Fixed-size char arrays — safe to pass across module boundaries,
     * memcpy-able, and stack-allocatable without heap overhead.
     */
    struct WarrantRecord
    {
        int warrant_id;
        char warrant_number[WARRANT_NUM_LEN];
        int case_id;
        char accused_cnic[WARRANT_CNIC_LEN];
        char warrant_type[WARRANT_TYPE_LEN];     // ARREST | SEARCH | SEIZURE
        char warrant_status[WARRANT_STATUS_LEN]; // ISSUED | EXECUTED | CANCELLED | EXPIRED
        char issuing_court[WARRANT_COURT_LEN];
        char magistrate_name[WARRANT_JUDGE_LEN];
        char target_address[WARRANT_ADDR_LEN]; // SEARCH warrants
        char valid_until[WARRANT_DATE_LEN];
        char cancellation_reason[WARRANT_REASON_LEN];
        int requested_by; // officer_id
        int executed_by;  // officer_id, 0 if not executed
        int cancelled_by; // officer_id, 0 if not cancelled
        time_t issue_date;
        time_t executed_at;
        time_t cancelled_at;
        time_t created_at;
        time_t updated_at;
    };

    /**
     * @class WarrantManager
     * @brief Stateless manager for all warrant operations.
     *
     * All methods are static — no instance state.
     * Caller provides a valid PGconn* from the OS layer IPC pool.
     */
    class WarrantManager
    {
    public:
        /**
         * Requests a new warrant (ISSUED state) for an accused in a case.
         *
         * Pre-conditions checked inside:
         *   - Officer rank >= INSPECTOR (via AuthManager::validateRank)
         *   - Case is in REGISTERED or UNDER_INVESTIGATION state
         *   - Warrant type is compatible with case crime type
         *
         * On success:
         *   - Inserts row into subsystem3.warrants with status = 'ISSUED'
         *   - Audit trigger fires automatically
         *   - out_warrant_id is set to the new warrant_id
         *
         * @param conn            Active PGconn* from IPC pool.
         * @param session         Authenticated officer's session context.
         * @param case_id         Case for which warrant is requested.
         * @param accused_cnic    CNIC of the accused person.
         * @param warrant_type    ARREST | SEARCH | SEIZURE.
         * @param magistrate_name Name of issuing magistrate.
         * @param issuing_court   Court name.
         * @param valid_until     ISO date string for expiry (e.g. "2026-05-15").
         * @param target_address  Location for SEARCH warrants; empty for ARREST.
         * @param out_warrant_id  Set to created warrant_id on success.
         * @param out_code        Result code (OK, RANK_INSUFFICIENT, INVALID_STATE,
         *                        JURISDICTION_DENIED, DB_ERROR).
         * @return true on success, false otherwise.
         */
        static bool requestWarrant(PGconn *conn,
                                   const JusticeFlow::SessionContext &session,
                                   int case_id,
                                   const char *accused_cnic,
                                   JusticeFlow::WarrantType warrant_type,
                                   const char *magistrate_name,
                                   const char *issuing_court,
                                   const char *valid_until,
                                   const char *target_address,
                                   int &out_warrant_id,
                                   JusticeFlow::ResultCode &out_code);

        /**
         * Executes an ISSUED warrant (ISSUED → EXECUTED).
         *
         * Validates:
         *   - Warrant exists and is in ISSUED state
         *   - valid_until has not passed (not expired)
         *   - Officer rank >= INSPECTOR
         *
         * On success:
         *   - Updates warrant_status to EXECUTED, sets executed_by and executed_at
         *   - Caller is expected to call ArrestManager::recordArrest immediately after
         *
         * @param conn        Active PGconn*.
         * @param session     Authenticated officer's session.
         * @param warrant_id  Warrant to execute.
         * @param out_code    Result code.
         * @return true on success.
         */
        static bool executeWarrant(PGconn *conn,
                                   const JusticeFlow::SessionContext &session,
                                   int warrant_id,
                                   JusticeFlow::ResultCode &out_code);

        /**
         * Cancels an ISSUED warrant (ISSUED → CANCELLED).
         *
         * Requires:
         *   - Warrant is in ISSUED state (EXECUTED warrants cannot be cancelled)
         *   - Officer rank >= INSPECTOR (SHO level)
         *   - cancellation_reason must not be empty
         *
         * @param conn                Active PGconn*.
         * @param session             Authenticated officer's session.
         * @param warrant_id          Warrant to cancel.
         * @param cancellation_reason Non-empty reason string (required for audit).
         * @param out_code            Result code.
         * @return true on success.
         */
        static bool cancelWarrant(PGconn *conn,
                                  const JusticeFlow::SessionContext &session,
                                  int warrant_id,
                                  const char *cancellation_reason,
                                  JusticeFlow::ResultCode &out_code);

        /**
         * Retrieves all warrants associated with a case.
         *
         * Read-only. No rank check required.
         * Results are ordered by issue_date descending.
         *
         * @param conn     Active PGconn*.
         * @param case_id  Case to query.
         * @param out      Vector appended with WarrantRecords.
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        static JusticeFlow::ResultCode getWarrantsByCase(PGconn *conn,
                                                         int case_id,
                                                         std::vector<WarrantRecord> &out);

        /**
         * Retrieves all ISSUED (active) warrants at a station.
         *
         * Used by the station dashboard to show pending warrants.
         * Filters by station_id via the cases table join.
         * Results ordered by valid_until ascending (soonest expiry first).
         *
         * @param conn        Active PGconn*.
         * @param station_id  Station to query.
         * @param out         Vector appended with WarrantRecords.
         * @return ResultCode::OK / NOT_FOUND / DB_ERROR
         */
        static JusticeFlow::ResultCode getActiveWarrants(PGconn *conn,
                                                         int station_id,
                                                         std::vector<WarrantRecord> &out);

    private:
        /**
         * @brief Validates that a warrant's current DB state permits a transition.
         *
         * Queries warrant_status from the DB. Checks against the state machine
         * transition table. Also optionally checks expiry if check_expiry = true.
         *
         * @param conn            Active PGconn*.
         * @param warrant_id      Warrant to check.
         * @param expected_from   State the warrant must currently be in.
         * @param target_to       State we want to transition to.
         * @param check_expiry    If true, also verify valid_until has not passed.
         * @param out_code        Set to INVALID_STATE or NOT_FOUND on failure.
         * @return true if transition is legal, false otherwise.
         */
        static bool _validateState(PGconn *conn,
                                   int warrant_id,
                                   const char *expected_from,
                                   const char *target_to,
                                   bool check_expiry,
                                   JusticeFlow::ResultCode &out_code);

        /**
         * @brief Sets session variables for audit trigger stamping.
         *
         * Executes:
         *   SET LOCAL app.current_officer_id  = '<officer_id>';
         *   SET LOCAL app.current_belt_number = '<belt_number>';
         *
         * Must be called within the same transaction as the write that follows.
         * SET LOCAL resets automatically at transaction end — no manual cleanup.
         *
         * @param conn       Active PGconn*.
         * @param officer_id Officer performing the operation.
         * @param belt       Officer's belt number string.
         * @return true if both SET LOCAL commands succeeded.
         */
        static bool _setSessionVars(PGconn *conn, int officer_id, const char *belt);

        /**
         * @brief Maps a PGresult row to a WarrantRecord.
         * Column order must match the SELECT list in SQL_SELECT_COLS (warrant_query.cpp).
         */
        static void _mapRow(PGresult *res, int row, WarrantRecord &rec);
    };

} // namespace enforcement