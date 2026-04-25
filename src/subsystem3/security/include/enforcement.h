#pragma once

#include <string>
#include <ctime>
#include <set>
#include "common/constants.h"
#include "common/common.h"

namespace security
{

    /**
     * @file enforcement.h
     * @brief Warrant, arrest, and bail operation implementations with state machines
     *
     * Core S3 module implementing:
     *   - 5 warrant operations (request, approve, execute, reject, cancel)
     *   - 4 arrest operations (record, update custody, dispute, release)
     *   - 3 bail operations (set, modify, revoke)
     *
     * All state transitions are validated before DB writes.
     * Each operation follows:
     *   1. Call access_control for pre-flight checks
     *   2. Validate state transition via isValidTransition()
     *   3. Execute DB write via ipc_manager
     *   4. Notify audit_bridge and integration bridges
     *
     * Thread Safety: All functions are stateless. Thread-safe via OS layer IPC.
     *
     * Dependencies: access_control.h, legal/*, integration/*, utils/time_utils.h
     */

    class Enforcement
    {
    private:
        /**
         * Validates state transitions for a record.
         *
         * Maintains lookup tables for legal transitions:
         *   - WarrantStatus: ISSUED → (EXECUTED, CANCELLED); EXECUTED → (final); etc.
         *   - CustodyStatus: IN_CUSTODY → (BAIL_GRANTED, REMANDED, RELEASED)
         *   - BailStatus: ACTIVE → (REVOKED, EXPIRED)
         *
         * Logs attempts at illegal transitions for compliance audit.
         *
         * @param current_state String representation of current state
         * @param new_state String representation of target state
         * @param state_type Type of state ("WarrantStatus", "CustodyStatus", etc.)
         * @return true if transition is legal, false otherwise
         */
        static bool isValidTransition(const std::string &current_state,
                                      const std::string &new_state,
                                      const std::string &state_type);

    public:
        // ========================
        // WARRANT OPERATIONS (5)
        // ========================

        /**
         * Requests a warrant against an accused in a case.
         *
         * State transition: (new) → ISSUED
         * Validations:
         *   - Case must be open (REGISTERED or UNDER_INVESTIGATION)
         *   - Officer must have jurisdiction
         *   - Warrant type must be valid for case type (compliance check)
         *   - Officer rank must be sufficient (policy engine)
         *
         * @param session Officer's session context
         * @param case_id Case ID
         * @param accused_cnic CNIC of accused
         * @param warrant_type Type of warrant (ARREST, SEARCH, SEIZURE)
         * @param magistrate_name Name of issuing magistrate
         * @param issuing_court Court name
         * @param valid_until Expiry date for warrant
         * @param target_address For SEARCH warrants, location to search
         * @param out_warrant_id Output parameter set to created warrant ID
         * @param out_code Output parameter set to:
         *                 - OK: warrant created
         *                 - RANK_INSUFFICIENT: escalation required
         *                 - JURISDICTION_DENIED: officer not authorized
         *                 - INVALID_STATE: case not in legal state
         *                 - DB_ERROR: database failure
         * @return true if successful, false otherwise
         */
        static bool requestWarrant(const JusticeFlow::SessionContext &session,
                                   int case_id,
                                   const std::string &accused_cnic,
                                   JusticeFlow::WarrantType warrant_type,
                                   const std::string &magistrate_name,
                                   const std::string &issuing_court,
                                   const std::string &valid_until,
                                   const std::string &target_address,
                                   int &out_warrant_id,
                                   JusticeFlow::ResultCode &out_code);

        /**
         * Approves a warrant (internal authorization).
         *
         * State transition: ISSUED → ISSUED (approved flag set)
         * Used for multi-level approval workflows.
         *
         * @param session Officer approving warrant
         * @param warrant_id Warrant to approve
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool approveWarrant(const JusticeFlow::SessionContext &session,
                                   int warrant_id,
                                   JusticeFlow::ResultCode &out_code);

        /**
         * Executes an approved warrant (arrest, search, or seizure).
         *
         * State transition: ISSUED → EXECUTED
         * Records officer who executed warrant and timestamp.
         *
         * @param session Officer executing warrant
         * @param warrant_id Warrant being executed
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool executeWarrant(const JusticeFlow::SessionContext &session,
                                   int warrant_id,
                                   JusticeFlow::ResultCode &out_code);

        /**
         * Rejects a warrant request.
         *
         * State transition: ISSUED → CANCELLED
         * Records rejection reason and authority.
         *
         * @param session Officer rejecting warrant
         * @param warrant_id Warrant being rejected
         * @param rejection_reason Reason for rejection
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool rejectWarrant(const JusticeFlow::SessionContext &session,
                                  int warrant_id,
                                  const std::string &rejection_reason,
                                  JusticeFlow::ResultCode &out_code);

        /**
         * Cancels an executed warrant (revocation).
         *
         * State transition: EXECUTED → CANCELLED
         * Used for warrant revocation or case dismissal.
         *
         * @param session Officer cancelling warrant
         * @param warrant_id Warrant being cancelled
         * @param cancellation_reason Reason for cancellation
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool cancelWarrant(const JusticeFlow::SessionContext &session,
                                  int warrant_id,
                                  const std::string &cancellation_reason,
                                  JusticeFlow::ResultCode &out_code);

        // ========================
        // ARREST OPERATIONS (4)
        // ========================

        /**
         * Records an arrest execution.
         *
         * Creates an arrest record and transitions warrant to EXECUTED state.
         * Initiates custody clock (arrest_time is captured).
         *
         * @param session Officer making arrest
         * @param warrant_id Warrant being executed
         * @param arrest_location Physical location of arrest
         * @param out_arrest_id Output parameter set to created arrest ID
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool recordArrest(const JusticeFlow::SessionContext &session,
                                 int warrant_id,
                                 const std::string &arrest_location,
                                 int &out_arrest_id,
                                 JusticeFlow::ResultCode &out_code);

        /**
         * Updates custody status of arrested person.
         *
         * State transitions: IN_CUSTODY → (BAIL_GRANTED, REMANDED, RELEASED, ESCAPED)
         * Used to track custody state throughout investigation.
         *
         * @param session Officer updating custody
         * @param arrest_id Arrest record
         * @param new_status New custody status
         * @param reason Reason for status change
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool updateCustodyStatus(const JusticeFlow::SessionContext &session,
                                        int arrest_id,
                                        JusticeFlow::CustodyStatus new_status,
                                        const std::string &reason,
                                        JusticeFlow::ResultCode &out_code);

        /**
         * Marks an arrest as disputed (procedural violation claim).
         *
         * Used when accused claims illegal arrest or procedure violation.
         * Flags arrest for review by higher authority.
         *
         * @param session Officer or accused marking dispute
         * @param arrest_id Arrest being disputed
         * @param dispute_reason Reason for dispute claim
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool disputeArrest(const JusticeFlow::SessionContext &session,
                                  int arrest_id,
                                  const std::string &dispute_reason,
                                  JusticeFlow::ResultCode &out_code);

        /**
         * Releases arrested person from custody.
         *
         * State transition: IN_CUSTODY/BAIL_GRANTED → RELEASED
         * Records release reason (bail, court order, expiry, etc.).
         *
         * @param session Officer releasing person
         * @param arrest_id Arrest record
         * @param release_reason Reason for release
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool releaseFromCustody(const JusticeFlow::SessionContext &session,
                                       int arrest_id,
                                       const std::string &release_reason,
                                       JusticeFlow::ResultCode &out_code);

        // ========================
        // BAIL OPERATIONS (3)
        // ========================

        /**
         * Sets bail for an arrested person.
         *
         * Creates bail record with amount, type, and terms.
         * Updates custody status to BAIL_GRANTED.
         *
         * Validations:
         *   - Bail amount must be within legal bounds (compliance check)
         *   - Officer must have authority (policy engine)
         *
         * @param session Officer setting bail
         * @param arrest_id Arrest for which bail is set
         * @param bail_type Type of bail (REGULAR, ANTICIPATORY, INTERIM, SURETY)
         * @param bail_amount Amount in paise
         * @param magistrate_name Name of authorizing magistrate
         * @param court_name Court granting bail
         * @param valid_until Bail expiry date
         * @param surety_cnic CNIC of surety (if SURETY type)
         * @param out_bail_id Output parameter set to created bail ID
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool setBail(const JusticeFlow::SessionContext &session,
                            int arrest_id,
                            JusticeFlow::BailType bail_type,
                            uint64_t bail_amount,
                            const std::string &magistrate_name,
                            const std::string &court_name,
                            const std::string &valid_until,
                            const std::string &surety_cnic,
                            int &out_bail_id,
                            JusticeFlow::ResultCode &out_code);

        /**
         * Modifies existing bail terms.
         *
         * State transition: ACTIVE → ACTIVE (with updated terms)
         * Can change amount or conditions.
         *
         * @param session Officer modifying bail
         * @param bail_id Bail record to modify
         * @param new_amount New bail amount in paise
         * @param modification_reason Reason for modification
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool modifyBail(const JusticeFlow::SessionContext &session,
                               int bail_id,
                               uint64_t new_amount,
                               const std::string &modification_reason,
                               JusticeFlow::ResultCode &out_code);

        /**
         * Revokes active bail.
         *
         * State transition: ACTIVE → REVOKED
         * Returns accused to custody.
         *
         * @param session Officer revoking bail (usually higher authority)
         * @param bail_id Bail being revoked
         * @param revocation_reason Reason for revocation
         * @param out_code Output parameter
         * @return true if successful, false otherwise
         */
        static bool revokeBail(const JusticeFlow::SessionContext &session,
                               int bail_id,
                               const std::string &revocation_reason,
                               JusticeFlow::ResultCode &out_code);
    };

} // namespace security