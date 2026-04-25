#pragma once

#include <string>
#include "common/constants.h"
#include "common/common.h"

namespace integration
{

    /**
     * @file s2_bridge.h
     * @brief Interface between Subsystem 3 and Subsystem 2 (FIR/Evidence/Cases)
     *
     * Owns all data access for Subsystem 2 records. S3 modules call these
     * rather than querying subsystem2 tables directly, keeping data ownership
     * boundaries clean.
     *
     * If Subsystem 2's internal structure changes, only s2_bridge.cpp needs updating.
     * All other subsystems are insulated from the change.
     *
     * Thread Safety: All functions are read-only and thread-safe (except notify operations).
     *
     * Dependencies: common/constants.h, common/common.h only
     */

    class S2Bridge
    {
    public:
        /**
         * Retrieves the full Case record for a case.
         *
         * Queries subsystem2.cases table and populates the Case struct
         * with all case metadata.
         *
         * @param case_id The case's unique ID
         * @param out_case Output parameter populated with case data
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if case doesn't exist
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT * FROM subsystem2.cases WHERE case_id = case_id
         *
         * @example
         *   JusticeFlow::Case case_record;
         *   auto result = S2Bridge::getCaseRecord(case_id, case_record);
         *   if (result == ResultCode::OK) {
         *       // Use case_record.station_id, case_record.case_status, etc.
         *   }
         */
        static JusticeFlow::ResultCode getCaseRecord(int case_id, JusticeFlow::Case &out_case);

        /**
         * Retrieves the full Evidence record for an evidence item.
         *
         * Queries subsystem2.evidence table and populates the Evidence struct
         * with all evidence metadata.
         *
         * @param evidence_id The evidence's unique ID
         * @param out_evidence Output parameter populated with evidence data
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if evidence doesn't exist
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT * FROM subsystem2.evidence WHERE evidence_id = evidence_id
         *
         * @example
         *   JusticeFlow::Evidence evidence;
         *   auto result = S2Bridge::getEvidenceRecord(evidence_id, evidence);
         *   if (result == ResultCode::OK) {
         *       // Use evidence.case_id, evidence.evidence_status, etc.
         *   }
         */
        static JusticeFlow::ResultCode getEvidenceRecord(int evidence_id,
                                                         JusticeFlow::Evidence &out_evidence);

        /**
         * Notifies Subsystem 2 that evidence has been linked to a forensic request.
         *
         * Called by forensic/forensic_request.cpp when evidence is submitted for
         * lab analysis. Updates subsystem2.evidence.evidence_status and logs the
         * state change for Subsystem 2's state machines.
         *
         * The bridge executes: UPDATE subsystem2.evidence SET evidence_status = 'SENT_TO_LAB'
         *                      WHERE evidence_id = evidence_id
         *
         * This ensures Subsystem 2 is aware of the forensic submission even though
         * the actual status update is trigger-driven.
         *
         * @param evidence_id The evidence being submitted
         * @param request_id The forensic request ID
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if evidence doesn't exist
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database operation: UPDATE subsystem2.evidence SET evidence_status = ...
         *       Also triggers audit.Audit_Log entry via trigger (SECURITY DEFINER).
         *
         * @example
         *   // When forensic request is created:
         *   auto result = S2Bridge::notifyForensicSubmission(evidence_id, request_id);
         */
        static JusticeFlow::ResultCode notifyForensicSubmission(int evidence_id, int request_id);

        /**
         * Validates that a case belongs to a specific station (jurisdictional check).
         *
         * Queries subsystem2.cases and verifies case.station_id = station_id.
         * Used for access control and jurisdiction validation.
         *
         * @param case_id The case to validate
         * @param station_id The station that should own the case
         * @return ResultCode::OK if case belongs to station
         *         ResultCode::JURISDICTION_DENIED if case belongs to different station
         *         ResultCode::NOT_FOUND if case doesn't exist
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT station_id FROM subsystem2.cases WHERE case_id = case_id
         *
         * @example
         *   auto result = S2Bridge::validateCaseOwnership(case_id, officer_station_id);
         *   if (result != ResultCode::OK) {
         *       // Reject operation — officer doesn't have jurisdiction
         *   }
         */
        static JusticeFlow::ResultCode validateCaseOwnership(int case_id, int station_id);
    };

} // namespace integration