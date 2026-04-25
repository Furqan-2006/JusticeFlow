#pragma once

#include <string>
#include "common/constants.h"

namespace legal
{

    /**
     * @file case_validation.h
     * @brief Case legality checks before warrant requests
     *
     * Owns all validation that must pass before a warrant can be requested against a case.
     * Three operations composed into a single pre-flight gate for enforcement.cpp.
     *
     * Does NOT own its own DB connection — receives connection reference from caller.
     * All database operations are read-only (SELECT queries only).
     *
     * Thread Safety: All functions are stateless and read-only. Safe for concurrent use.
     *
     * Dependencies: utils/time_utils (for expiry checks), common/constants.h
     */

    class CaseValidation
    {
    public:
        /**
         * Verifies that a case exists and is in a legal state for warrant operations.
         *
         * Valid states for warrant requests:
         *   - REGISTERED: FIR filed, investigation pending
         *   - UNDER_INVESTIGATION: Active investigation
         *
         * Invalid states (block warrant):
         *   - EVIDENCE_COLLECTED: Evidence phase, warrant only after investigation begins
         *   - PENDING_TRIAL: Case moved to court
         *   - CLOSED: Investigation completed
         *   - REOPENED: Under review before returning to REGISTERED
         *
         * @param case_id The case ID to validate
         * @param out_code Output parameter set to:
         *                 - OK: case exists and is open
         *                 - NOT_FOUND: case_id doesn't exist
         *                 - INVALID_STATE: case exists but in wrong state for warrant
         *                 - DB_ERROR: database query failure
         * @return true if case is valid for warrant operations, false otherwise
         *
         * @note Database query: SELECT case_status FROM subsystem2.cases WHERE case_id = case_id
         */
        static bool caseExistsAndOpen(int case_id, JusticeFlow::ResultCode &out_code);

        /**
         * Verifies that an officer has jurisdictional authority over a case.
         *
         * Jurisdiction rules:
         *   1. Case belongs to officer's station_id
         *   2. Officer is in active duty (status = ACTIVE)
         *   3. Case is not in a different zone (unless officer is HQ rank)
         *   4. HQ officers (DSP+) have zone-wide jurisdiction
         *   5. Station officers can only operate within their assigned station
         *
         * @param officer_id The officer's unique ID
         * @param case_id The case ID being validated
         * @param out_code Output parameter set to:
         *                 - OK: officer has jurisdiction
         *                 - JURISDICTION_DENIED: officer not authorized for this case
         *                 - NOT_FOUND: officer_id or case_id doesn't exist
         *                 - DB_ERROR: database query failure
         * @return true if officer has jurisdiction, false otherwise
         *
         * @note Database queries:
         *       1. SELECT station_id FROM subsystem2.cases WHERE case_id = case_id
         *       2. SELECT station_id, currentRank, status FROM subsystem1.officers WHERE officer_id = officer_id
         *       3. Station zone validation via subsystem1.stations table
         */
        static bool officerBelongsToStation(int officer_id, int case_id, JusticeFlow::ResultCode &out_code);

        /**
         * Composite validation gate for warrant requests.
         *
         * Performs BOTH checks (caseExistsAndOpen + officerBelongsToStation) in sequence.
         * Recommended single pre-flight call before enforcement.cpp::requestWarrant().
         *
         * Execution order:
         *   1. caseExistsAndOpen(case_id) — case must be in legal state
         *   2. officerBelongsToStation(officer_id, case_id) — jurisdiction check
         *
         * Short-circuits on first failure.
         *
         * @param case_id The case to validate
         * @param officer_id The officer requesting the warrant
         * @param out_code Output parameter set to:
         *                 - OK: both validations pass
         *                 - NOT_FOUND, INVALID_STATE, JURISDICTION_DENIED, DB_ERROR: first failure code
         * @return true if case is valid for warrant operations AND officer has jurisdiction
         *
         * @example
         *   if (CaseValidation::validateCaseForWarrant(case_id, officer_id, result_code)) {
         *       // Proceed to requestWarrant()
         *   } else {
         *       Logger::error("Warrant validation failed");
         *   }
         */
        static bool validateCaseForWarrant(int case_id, int officer_id, JusticeFlow::ResultCode &out_code);
    };

} // namespace legal