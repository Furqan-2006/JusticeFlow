#pragma once

#include <string>
#include "common/constants.h"

namespace legal
{

    /**
     * @file evidence_rules.h
     * @brief Admissibility and soft-delete enforcement for evidence
     *
     * Owns admissibility checks and soft-delete enforcement (Defense in Depth).
     * All hard DELETE attempts are blocked at C++ layer before reaching database.
     * DB trigger provides second-layer defense.
     *
     * Thread Safety: All functions are stateless and read-only. Safe for concurrent use.
     *
     * Dependencies: common/constants.h only
     */

    class EvidenceRules
    {
    public:
        /**
         * Enforces soft-delete-only policy for evidence records.
         *
         * BLOCKS hard DELETE operations at the C++ layer.
         * Evidence can NEVER be hard-deleted from the system (chain of custody requirement).
         * Soft-delete (UPDATE is_deleted = true) is the only legal deletion method.
         *
         * This function is a legal gate that prevents hard deletion before reaching the DB.
         * The database trigger provides the second layer of defense.
         *
         * @param evidence_id The evidence record ID
         * @return INVALID_STATE (always) — hard delete is illegal
         *
         * @note Usage: Caller must convert hard DELETE requests to soft DELETE (UPDATE).
         */
        static JusticeFlow::ResultCode enforceSoftDelete(int evidence_id);

        /**
         * Checks if evidence is admissible for use in forensic requests.
         *
         * Admissibility criteria (ALL must be true):
         *   1. is_deleted = false (evidence is not soft-deleted)
         *   2. evidence_status ∈ {RECEIVED, SEALED, SENT_TO_LAB, RETURNED_FROM_LAB, PRODUCED_IN_COURT}
         *      (NOT in DISPOSED state)
         *   3. Evidence record exists in database
         *
         * Legal states (admissible):
         *   - RECEIVED: Initial evidence receipt
         *   - SEALED: Chain of custody sealed, ready for use
         *   - SENT_TO_LAB: Currently in forensic examination
         *   - RETURNED_FROM_LAB: Lab analysis complete, evidence available
         *   - PRODUCED_IN_COURT: Used as evidence in court, still available
         *
         * Inadmissible states (block use):
         *   - DISPOSED: Evidence was disposed (destroyed, released, etc.)
         *
         * @param evidence_id The evidence record ID to check
         * @param out_code Output parameter set to:
         *                 - OK: evidence is admissible
         *                 - INVALID_STATE: evidence is deleted or in inadmissible state
         *                 - NOT_FOUND: evidence_id doesn't exist
         *                 - DB_ERROR: database query failure
         * @return true if admissible, false otherwise
         *
         * @note Database query: SELECT is_deleted, evidence_status FROM subsystem2.evidence WHERE evidence_id = evidence_id
         */
        static bool isAdmissible(int evidence_id, JusticeFlow::ResultCode &out_code);

        /**
         * Validates that evidence belongs to the case it's being linked to.
         *
         * Chain of custody requirement: Evidence can only be used in operations
         * related to the case it was collected for. Cross-case evidence usage
         * is a legal violation.
         *
         * Ownership check:
         *   1. Verify evidence.case_id = case_id parameter
         *   2. Verify evidence is not deleted
         *   3. Verify case exists
         *
         * @param evidence_id The evidence record ID
         * @param case_id The case ID for the current operation
         * @param out_code Output parameter set to:
         *                 - OK: evidence belongs to case
         *                 - INVALID_INPUT: evidence belongs to different case (chain of custody violation)
         *                 - NOT_FOUND: evidence_id or case_id doesn't exist
         *                 - DB_ERROR: database query failure
         * @return true if evidence belongs to case, false otherwise
         *
         * @note Database queries:
         *       1. SELECT case_id, is_deleted FROM subsystem2.evidence WHERE evidence_id = evidence_id
         *       2. SELECT case_id FROM subsystem2.cases WHERE case_id = case_id (existence check)
         */
        static bool validateEvidenceOwnership(int evidence_id, int case_id, JusticeFlow::ResultCode &out_code);
    };

} // namespace legal