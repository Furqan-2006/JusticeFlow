#pragma once

#include <string>
#include <ctime>
#include "common/constants.h"
#include "common/common.h"

namespace forensic {

/**
 * @file forensic_request.h
 * @brief Forensic request state machine and business operations
 *
 * Owns all forensic lab request workflow:
 *   - requestForensicExamination: Create request (REQUESTED state)
 *   - submitToLab: Mark request received by lab (RECEIVED_BY_LAB state)
 *   - recordExaminationStart: Mark examination begun (UNDER_EXAMINATION state)
 *   - recordFindings: Record lab findings (REPORT_READY state)
 *   - deliverReport: Deliver findings to case (REPORT_DELIVERED state)
 *   - contestReport: Challenge findings (CONTESTED state)
 *   - linkEvidence: Link evidence to request (composite operation)
 *
 * All state transitions validated via isValidTransition().
 * Evidence admissibility checked before linking.
 * All operations call access_control for authorization.
 * All changes logged via audit_bridge.
 *
 * Thread Safety: All functions are stateless. Thread-safe via OS layer IPC.
 *
 * Dependencies: forensic_repository.h, access_control.h, evidence_rules.h,
 *               audit_bridge.h, s2_bridge.h, utils/time_utils.h
 */

class ForensicRequest {
private:
    /**
     * Validates state transitions for forensic requests.
     *
     * Legal state machine:
     *   REQUESTED → RECEIVED_BY_LAB
     *   RECEIVED_BY_LAB → UNDER_EXAMINATION
     *   UNDER_EXAMINATION → REPORT_READY
     *   REPORT_READY → REPORT_DELIVERED or CONTESTED
     *   CONTESTED → REPORT_READY (amended) or REPORT_DELIVERED
     *
     * @param current_state Current request status
     * @param new_state Target request status
     * @return true if transition is legal, false otherwise
     */
    static bool isValidTransition(const std::string& current_state,
                                  const std::string& new_state);

public:
    /**
     * Requests a forensic examination for evidence in a case.
     *
     * State transition: (new) → REQUESTED
     * Creates forensic lab request record.
     *
     * Authorization:
     *   - Officer must be on active duty
     *   - Officer must have jurisdiction over case (via s2_bridge)
     *   - Case must be in UNDER_INVESTIGATION or EVIDENCE_COLLECTED state
     *
     * @param session Officer's session context
     * @param case_id Case requiring forensic examination
     * @param lab_name Target forensic lab
     * @param examiner_name Lead examiner name
     * @param examination_purpose Type of examination (DNA_ANALYSIS, FINGERPRINT_ANALYSIS, etc.)
     * @param purpose_description Detailed examination goals
     * @param report_expected_date Expected delivery date
     * @param out_request_id Output parameter set to created request ID
     * @param out_code Output parameter set to:
     *                 - OK: request created
     *                 - RANK_INSUFFICIENT: authorization required
     *                 - JURISDICTION_DENIED: officer not authorized for case
     *                 - INVALID_STATE: case not in legal state
     *                 - DB_ERROR: database failure
     * @return true if successful, false otherwise
     */
    static bool requestForensicExamination(const JusticeFlow::SessionContext& session,
                                           int case_id,
                                           const std::string& lab_name,
                                           const std::string& examiner_name,
                                           JusticeFlow::ExaminationPurpose examination_purpose,
                                           const std::string& purpose_description,
                                           const std::string& report_expected_date,
                                           int& out_request_id,
                                           JusticeFlow::ResultCode& out_code);

    /**
     * Marks a forensic request as received by the lab.
     *
     * State transition: REQUESTED → RECEIVED_BY_LAB
     * Records the date request was physically received by lab.
     *
     * @param session Officer updating request
     * @param request_id Request being received
     * @param received_date Date received by lab
     * @param out_code Output parameter
     * @return true if successful, false otherwise
     */
    static bool submitToLab(const JusticeFlow::SessionContext& session,
                            int request_id,
                            const std::string& received_date,
                            JusticeFlow::ResultCode& out_code);

    /**
     * Marks a forensic examination as begun at the lab.
     *
     * State transition: RECEIVED_BY_LAB → UNDER_EXAMINATION
     * Lab has started actual examination of evidence.
     *
     * @param session Lab technician or investigator updating request
     * @param request_id Request in examination
     * @param out_code Output parameter
     * @return true if successful, false otherwise
     */
    static bool recordExaminationStart(const JusticeFlow::SessionContext& session,
                                       int request_id,
                                       JusticeFlow::ResultCode& out_code);

    /**
     * Records findings from completed forensic examination.
     *
     * State transition: UNDER_EXAMINATION → REPORT_READY
     * Lab has completed analysis and generated report.
     *
     * Validations:
     *   - Report generated_date must be >= examination start date (via time_utils)
     *   - Report content must not be empty
     *
     * @param session Lab officer recording findings
     * @param request_id Request with findings
     * @param findings Examination findings/analysis results (JSON or text)
     * @param report_file_path Path to report file in storage system
     * @param out_code Output parameter
     * @return true if successful, false otherwise
     */
    static bool recordFindings(const JusticeFlow::SessionContext& session,
                               int request_id,
                               const std::string& findings,
                               const std::string& report_file_path,
                               JusticeFlow::ResultCode& out_code);

    /**
     * Delivers forensic report to investigation/prosecution.
     *
     * State transition: REPORT_READY → REPORT_DELIVERED
     * Report is now official and admissible in legal proceedings.
     * Updates subsystem2.evidence status to PRODUCED_IN_COURT (via trigger).
     *
     * @param session Officer receiving/delivering report
     * @param request_id Request being delivered
     * @param delivered_date Date report delivered
     * @param out_code Output parameter
     * @return true if successful, false otherwise
     */
    static bool deliverReport(const JusticeFlow::SessionContext& session,
                              int request_id,
                              const std::string& delivered_date,
                              JusticeFlow::ResultCode& out_code);

    /**
     * Contests/challenges a forensic report.
     *
     * State transition: REPORT_DELIVERED → CONTESTED
     * Initiates re-examination or legal challenge of findings.
     * Can result in amended report or rejection.
     *
     * @param session Officer contesting report
     * @param request_id Report being contested
     * @param contest_reason Reason for challenge
     * @param out_code Output parameter
     * @return true if successful, false otherwise
     */
    static bool contestReport(const JusticeFlow::SessionContext& session,
                              int request_id,
                              const std::string& contest_reason,
                              JusticeFlow::ResultCode& out_code);

    /**
     * Links evidence to a forensic request for examination.
     *
     * Composite operation:
     *   1. Validates evidence is admissible (evidence_rules.isAdmissible())
     *   2. Validates evidence ownership (evidence_rules.validateEvidenceOwnership())
     *   3. Inserts link into Forensic_Request_Evidence
     *   4. Trigger updates evidence status to SENT_TO_LAB
     *   5. Notifies S2 bridge of state change
     *
     * @param session Officer linking evidence
     * @param request_id Forensic request
     * @param evidence_id Evidence to link
     * @param notes Optional notes about evidence submission
     * @param out_code Output parameter set to:
     *                 - OK: evidence linked
     *                 - INVALID_STATE: evidence not admissible
     *                 - INVALID_INPUT: evidence doesn't belong to case
     *                 - NOT_FOUND: evidence or request not found
     *                 - DB_ERROR: database failure
     * @return true if successful, false otherwise
     */
    static bool linkEvidence(const JusticeFlow::SessionContext& session,
                             int request_id,
                             int evidence_id,
                             const std::string& notes,
                             JusticeFlow::ResultCode& out_code);
};

} // namespace forensic