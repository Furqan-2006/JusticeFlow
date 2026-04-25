#pragma once

#include <string>
#include "common/constants.h"
#include "common/common.h"

namespace security
{

    /**
     * @file access_control.h
     * @brief Pre-flight authorization layer for all S3 operations
     *
     * Thin validation layer that composes:
     *   1. Rank check via policy_engine
     *   2. Duty status check via auth_manager
     *   3. Legal pre-flight checks via case_validation/evidence_rules
     *   4. Jurisdiction check via s2_bridge
     *
     * Every operation in enforcement.cpp calls access_control first.
     * Returns single ResultCode — caller either proceeds or returns immediately.
     *
     * Thread Safety: All functions are read-only and thread-safe.
     *
     * Dependencies: policy_engine.h, legal/case_validation.h,
     *               shr_infra/auth/session_store.h, integration/s1_bridge.h
     */

    class AccessControl
    {
    public:
        /**
         * Pre-flight authorization for warrant operations.
         *
         * Composed checks:
         *   1. Session must be valid (auth_manager.validate())
         *   2. Officer must be on active duty (s1_bridge.getOfficerDutyStatus())
         *   3. Case must be open (case_validation.caseExistsAndOpen())
         *   4. Officer must have jurisdiction (case_validation.officerBelongsToStation())
         *   5. Policy engine must approve based on rank
         *
         * @param session The officer's session context (from auth layer)
         * @param case_id The case for which warrant is requested
         * @param warrant_type The type of warrant (ARREST, SEARCH, SEIZURE)
         * @param out_code Output parameter set to:
         *                 - OK: all checks pass, proceed
         *                 - SESSION_EXPIRED: session invalid
         *                 - RANK_INSUFFICIENT: escalation required
         *                 - JURISDICTION_DENIED: officer not authorized
         *                 - INVALID_STATE: case not in legal state
         *                 - DUTY_INACTIVE: officer not on active duty
         * @return true if all checks pass, false otherwise
         *
         * @example
         *   auto result = AccessControl::checkWarrantPermission(session, case_id, ResultCode);
         *   if (result) {
         *       // Proceed to enforcement.requestWarrant()
         *   } else {
         *       // Return error to caller
         *   }
         */
        static bool checkWarrantPermission(const JusticeFlow::SessionContext &session,
                                           int case_id,
                                           JusticeFlow::WarrantType warrant_type,
                                           JusticeFlow::ResultCode &out_code);

        /**
         * Pre-flight authorization for arrest operations.
         *
         * Composed checks:
         *   1. Session must be valid
         *   2. Officer must be on active duty
         *   3. Warrant must exist and be valid (not expired, not already executed)
         *   4. Case associated with warrant must be accessible
         *   5. Policy engine must approve based on rank
         *
         * @param session The officer's session context
         * @param warrant_id The warrant being executed
         * @param out_code Output parameter set to:
         *                 - OK: all checks pass, proceed
         *                 - SESSION_EXPIRED: session invalid
         *                 - RANK_INSUFFICIENT: escalation required
         *                 - JURISDICTION_DENIED: officer not authorized
         *                 - INVALID_STATE: warrant expired or already executed
         *                 - NOT_FOUND: warrant doesn't exist
         * @return true if all checks pass, false otherwise
         */
        static bool checkArrestPermission(const JusticeFlow::SessionContext &session,
                                          int warrant_id,
                                          JusticeFlow::ResultCode &out_code);

        /**
         * Pre-flight authorization for bail operations.
         *
         * Composed checks:
         *   1. Session must be valid
         *   2. Officer must be on active duty
         *   3. Arrest must exist and not already bail-processed
         *   4. Case associated with arrest must be accessible
         *   5. Policy engine must approve based on rank
         *
         * @param session The officer's session context
         * @param arrest_id The arrest for which bail is being set
         * @param out_code Output parameter set to:
         *                 - OK: all checks pass, proceed
         *                 - SESSION_EXPIRED: session invalid
         *                 - RANK_INSUFFICIENT: escalation required
         *                 - JURISDICTION_DENIED: officer not authorized
         *                 - INVALID_STATE: arrest already bail-processed
         *                 - NOT_FOUND: arrest doesn't exist
         * @return true if all checks pass, false otherwise
         */
        static bool checkBailPermission(const JusticeFlow::SessionContext &session,
                                        int arrest_id,
                                        JusticeFlow::ResultCode &out_code);
    };

} // namespace security