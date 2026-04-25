#pragma once

#include <ctime>
#include <string>

/**
 * @file time_utils.h
 * @brief Pure utility namespace for time_t arithmetic and formatting.
 *
 * Provides centralized time operations used across Subsystem3 for:
 *   - Warrant expiry validation
 *   - Bail expiry validation
 *   - Session timeout checks
 *   - Audit log formatting
 *
 * All functions are stateless and thread-safe (no static state).
 * No dependencies on OS layer or other subsystem modules.
 *
 * @note All timestamps are in UTC (time_t semantics).
 */
namespace time_utils
{
    /**
     * Checks whether a deadline has passed.
     *
     * @param valid_until The expiry timestamp (UTC)
     * @return true if current time >= valid_until, false otherwise
     *
     * @example
     *   if (time_utils::isExpired(warrant.expiry_time)) {
     *       // Warrant is no longer valid
     *   }
     */
    bool isExpired(time_t valid_until);

    /**
     * Calculates the number of days between two timestamps.
     *
     * @param from Start timestamp (UTC)
     * @param to End timestamp (UTC)
     * @return Number of whole days (truncated). Negative if 'to' is before 'from'.
     *
     * @example
     *   int days = time_utils::daysBetween(case_start, case_end);
     *   if (days > 180) {
     *       // Case has exceeded 6-month investigation period
     *   }
     */
    int daysBetween(time_t from, time_t to);

    /**
     * Computes the midnight boundary (00:01 UTC) for a given date.
     *
     * Used for expiry calculations where warrants/bail expire at the END of a day
     * (i.e., 00:01 the next morning). For example, a 7-day warrant issued on
     * 2026-04-25 10:00 expires at 2026-05-02 00:01.
     *
     * @param date Any timestamp within the desired date
     * @param days_offset How many days to add (default 0 = next midnight)
     * @return The timestamp representing 00:01 on the target date
     *
     * @example
     *   time_t warrant_issued = now();
     *   time_t warrant_expires = time_utils::midnight(warrant_issued, 7);
     *   // Warrant is valid until 7 days from now at 00:01
     */
    time_t midnight(time_t date, int days_offset = 0);

    /**
     * Formats a timestamp for human-readable audit log output.
     *
     * @param t The timestamp to format (UTC)
     * @return String in format "YYYY-MM-DD HH:MM:SS UTC"
     *
     * @example
     *   std::string audit_entry = "Warrant issued at " +
     *       time_utils::toReadableString(now());
     *   // Output: "Warrant issued at 2026-04-25 14:30:45 UTC"
     */
    std::string toReadableString(time_t t);

} // namespace time_utils
