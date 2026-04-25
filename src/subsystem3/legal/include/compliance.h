#pragma once

#include <string>
#include <cstdint>
#include "common/constants.h"

namespace legal
{

    /**
     * @struct ComplianceResult
     * @brief Structured result for compliance checks with audit trail reason
     *
     * Combines a ResultCode with a human-readable reason string for audit logging.
     * Implicitly convertible to ResultCode for backward compatibility.
     *
     * @example
     *   ComplianceResult result = Compliance::validateWarrantType(case_type, warrant_type);
     *   if (result.code != JusticeFlow::ResultCode::OK) {
     *       // Log rejection reason for audit trail
     *   }
     */
    struct ComplianceResult
    {
        JusticeFlow::ResultCode code; ///< ResultCode (OK or error)
        std::string reason;           ///< Human-readable reason for audit log

        /**
         * @brief Implicit conversion to ResultCode for compatibility with existing code
         */
        operator JusticeFlow::ResultCode() const { return code; }

        /**
         * @brief Convenience constructor
         */
        ComplianceResult(JusticeFlow::ResultCode c = JusticeFlow::ResultCode::OK,
                         const std::string &r = "")
            : code(c), reason(r) {}
    };

    /**
     * @file compliance.h
     * @brief SOP and policy compliance checks spanning multiple tables
     *
     * Owns warrant type validation, bail amount enforcement, and general SOP compliance gates.
     * Returns structured ComplianceResult with code + reason for audit logging.
     *
     * Thread Safety: All functions are stateless and read-only. Safe for concurrent use.
     *
     * Dependencies: common/constants.h, utils/rule_utils.h (for severity escalation)
     */

    class Compliance
    {
    public:
        /**
         * Validates that a warrant type is legal for the given crime type.
         *
         * Warrant Type Rules:
         *
         * ARREST:
         *   Allowed: All crime types (arrest warrants are universal)
         *
         * SEARCH:
         *   Allowed: BURGLARY, ROBBERY, ARMED_ROBBERY, ASSAULT, AGGRAVATED_ASSAULT, KIDNAPPING,
         *            HUMAN_TRAFFICKING, TERRORISM, DRUG_TRAFFICKING, GANG_ACTIVITY
         *   Denied: White-collar crimes (FRAUD, BRIBERY, FORGERY, CYBERCRIME)
         *
         * SEIZURE:
         *   Allowed: ROBBERY, ARMED_ROBBERY, DRUG_TRAFFICKING, TERRORISM, GANG_ACTIVITY
         *            (crimes involving contraband/proceeds)
         *
         * @param case_type The crime type (from subsystem2.cases.case_type)
         * @param warrant_type The requested warrant type
         * @return ComplianceResult with:
         *         - code = OK: warrant type is legal for this crime
         *         - code = INVALID_INPUT: warrant type not allowed for this crime
         *         - reason = human-readable explanation for audit log
         *
         * @example
         *   auto result = Compliance::validateWarrantType(CaseType::MURDER, WarrantType::ARREST);
         *   if (result.code == ResultCode::OK) {
         *       // Proceed to issue arrest warrant
         *   }
         */
        static ComplianceResult validateWarrantType(JusticeFlow::CaseType case_type,
                                                    JusticeFlow::WarrantType warrant_type);

        /**
         * Validates bail amount against legal minimum/maximum per bail type.
         *
         * Bail Type Constraints (all amounts in paise, 1 INR = 100 paise):
         *
         * REGULAR:
         *   Min: ₹5,000 (500,000 paise)
         *   Max: ₹500,000 (50,000,000 paise)
         *   Use: Standard bail for most crimes
         *
         * ANTICIPATORY:
         *   Min: ₹10,000 (1,000,000 paise)
         *   Max: ₹1,000,000 (100,000,000 paise)
         *   Use: Preventive bail before arrest
         *
         * INTERIM:
         *   Min: ₹2,500 (250,000 paise)
         *   Max: ₹100,000 (10,000,000 paise)
         *   Use: Temporary bail pending main hearing
         *
         * SURETY:
         *   Min: ₹25,000 (2,500,000 paise)
         *   Max: ₹5,000,000 (500,000,000 paise)
         *   Use: Bail with guarantor for serious crimes
         *
         * @param bail_type The type of bail being set
         * @param amount_paise The bail amount in paise
         * @return ComplianceResult with:
         *         - code = OK: amount is within legal bounds
         *         - code = INVALID_INPUT: amount below minimum or above maximum
         *         - reason = "Amount ₹X below/above limit for REGULAR"
         *
         * @example
         *   uint64_t bail_paise = 1500000;  // ₹15,000
         *   auto result = Compliance::validateBailAmount(BailType::REGULAR, bail_paise);
         */
        static ComplianceResult validateBailAmount(JusticeFlow::BailType bail_type, uint64_t amount_paise);

        /**
         * General SOP compliance gate for non-standard operations.
         *
         * Operation Types and Escalation Requirements:
         *
         * NON_STANDARD_WARRANT:
         *   Requires: DSP+ rank (JusticeFlow::OfficerRank::DSP or higher)
         *   Reason: Unusual warrant type or rare crime category
         *
         * UNUSUAL_EVIDENCE_HANDLING:
         *   Requires: SI+ rank (JusticeFlow::OfficerRank::SI or higher) + SOP review
         *   Reason: Evidence chain of custody deviation
         *
         * MULTI_CASE_LINKAGE:
         *   Requires: INSPECTOR+ rank + formal approval
         *   Reason: Evidence/accused linked across multiple cases
         *
         * CROSS_JURISDICTION:
         *   Requires: DSP+ rank + inter-station notification
         *   Reason: Case operation involving multiple zones/stations
         *
         * STANDARD_OPERATION:
         *   No escalation required
         *   Examples: Normal warrant issue, standard evidence collection, routine bail
         *
         * @param operation_type The type of operation being performed (case-insensitive)
         * @param context Free-form context string (passed to audit log)
         * @return ComplianceResult with:
         *         - code = OK: operation is compliant as-is
         *         - code = INVALID_STATE: operation requires escalation/approval
         *         - reason = escalation requirement and reason
         *
         * @example
         *   auto result = Compliance::checkSOPCompliance(
         *       "MULTI_CASE_LINKAGE",
         *       "Evidence from Case #5 linked to Case #12"
         *   );
         */
        static ComplianceResult checkSOPCompliance(const std::string &operation_type,
                                                   const std::string &context);
    };

} // namespace legal