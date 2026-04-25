#pragma once

#include <string>
#include "common/constants.h"

/**
 * @file rule_utils.h
 * @brief Pure utility namespace for rank and severity rule operations.
 *
 * Provides centralized comparison and conversion utilities used across Subsystem3 for:
 *   - Access control rank validation (warrant, arrest, bail authorization)
 *   - Policy engine escalation decisions (severity-based routing)
 *   - Audit log serialization (rank/severity to string)
 *
 * All functions are stateless and thread-safe (no static state).
 * Dependencies: constants.h (for OfficerRank and ResultCode enums).
 *
 * @note Rank hierarchy (lowest to highest):
 *   CONSTABLE < HEAD_CONSTABLE < ASI < SI < INSPECTOR < DSP < SP < SSP < DIG < ADDL_IG < IGP
 *
 * @note Severity weights (high to low):
 *   CRITICAL(4) > HIGH(3) > MEDIUM(2) > LOW(1)
 */
namespace rule_utils
{
    /**
     * Checks if an officer's rank meets or exceeds the minimum required rank.
     *
     * Rank comparison uses hierarchical ordering: a CONSTABLE cannot authorize
     * what an INSPECTOR can, but an INSPECTOR can authorize what a CONSTABLE can.
     *
     * @param actual The officer's actual rank
     * @param minimum The minimum required rank for the operation
     * @return true if actual >= minimum in hierarchy, false otherwise
     *
     * @example
     *   if (rule_utils::meetsMinimumRank(officer.rank, JusticeFlow::OfficerRank::SI)) {
     *       // Officer can issue warrants
     *   }
     */
    bool meetsMinimumRank(JusticeFlow::OfficerRank actual, JusticeFlow::OfficerRank minimum);

    /**
     * Converts an OfficerRank enum value to its human-readable string representation.
     *
     * @param rank The rank enum value
     * @return String representation (e.g., "INSPECTOR", "CONSTABLE")
     *
     * @example
     *   std::string rank_str = rule_utils::rankToString(JusticeFlow::OfficerRank::INSPECTOR);
     *   // Returns "INSPECTOR"
     */
    std::string rankToString(JusticeFlow::OfficerRank rank);

    /**
     * Parses a string into an OfficerRank enum value.
     *
     * Case-sensitive match against standard rank strings. Used to deserialize
     * rank values from database queries or audit logs.
     *
     * @param str The rank string (e.g., "INSPECTOR", "CONSTABLE")
     * @return The corresponding OfficerRank enum value
     * @throw std::invalid_argument if string does not match any known rank
     *
     * @example
     *   auto rank = rule_utils::rankFromString("INSPECTOR");
     *   // Returns OfficerRank::INSPECTOR
     */
    JusticeFlow::OfficerRank rankFromString(const std::string &str);

    /**
     * Calculates a numeric weight for a severity level.
     *
     * Used by policy_engine's Chain of Responsibility pattern to determine
     * escalation paths: higher severity = higher escalation.
     *
     * @param severity The severity string (must be uppercase: "CRITICAL", "HIGH", "MEDIUM", "LOW")
     * @return Numeric weight: CRITICAL=4, HIGH=3, MEDIUM=2, LOW=1
     * @throw std::invalid_argument if severity string is not recognized
     *
     * @example
     *   int weight = rule_utils::severityWeight("CRITICAL");
     *   // Returns 4
     *
     *   if (rule_utils::severityWeight(case.severity) >= 3) {
     *       // HIGH or CRITICAL: escalate to DSP
     *   }
     */
    int severityWeight(const std::string &severity);

    /**
     * Converts a severity weight back to its string representation.
     *
     * Inverse of severityWeight(). Used for audit logging and output formatting.
     *
     * @param weight The numeric weight (1-4)
     * @return String representation ("LOW", "MEDIUM", "HIGH", "CRITICAL")
     * @throw std::invalid_argument if weight is not in range [1, 4]
     *
     * @example
     *   std::string severity_str = rule_utils::severityFromWeight(4);
     *   // Returns "CRITICAL"
     */
    std::string severityFromWeight(int weight);

} // namespace rule_utils
