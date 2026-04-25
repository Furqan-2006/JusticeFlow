#pragma once

#include <string>
#include "common/constants.h"
#include "common/common.h"

namespace integration
{

    /**
     * @file s1_bridge.h
     * @brief Interface between Subsystem 3 and Subsystem 1 (Officers/Personnel)
     *
     * Owns all data access for Subsystem 1 records. S3 modules call these
     * rather than querying subsystem1 tables directly, keeping data ownership
     * boundaries clean.
     *
     * If Subsystem 1's internal structure changes, only s1_bridge.cpp needs updating.
     * All other subsystems are insulated from the change.
     *
     * Thread Safety: All functions are read-only and thread-safe.
     *
     * Dependencies: common/constants.h, common/common.h only
     */

    class S1Bridge
    {
    public:
        /**
         * Retrieves the full Officer record for an officer.
         *
         * Queries subsystem1.officers table and populates the Officer struct
         * with all officer metadata.
         *
         * @param officer_id The officer's unique ID
         * @param out_officer Output parameter populated with officer data
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if officer doesn't exist
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT * FROM subsystem1.officers WHERE officer_id = officer_id
         *
         * @example
         *   JusticeFlow::Officer officer;
         *   auto result = S1Bridge::getOfficerRecord(officer_id, officer);
         *   if (result == ResultCode::OK) {
         *       // Use officer.currentRank, officer.station_id, etc.
         *   }
         */
        static JusticeFlow::ResultCode getOfficerRecord(int officer_id,
                                                        JusticeFlow::Officer &out_officer);

        /**
         * Checks if an officer is currently on active duty.
         *
         * Queries subsystem1.officers table for duty status.
         * Active duty means status = ACTIVE (not SUSPENDED, ON_LEAVE, RETIRED, TERMINATED).
         *
         * @param officer_id The officer's unique ID
         * @param out_active Output parameter set to true if officer is active, false otherwise
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if officer doesn't exist
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database query: SELECT status FROM subsystem1.officers WHERE officer_id = officer_id
         *
         * @example
         *   bool is_active = false;
         *   auto result = S1Bridge::getOfficerDutyStatus(officer_id, is_active);
         *   if (result == ResultCode::OK && is_active) {
         *       // Officer can be assigned to case
         *   }
         */
        static JusticeFlow::ResultCode getOfficerDutyStatus(int officer_id, bool &out_active);

        /**
         * Notifies Subsystem 1 that an officer has been assigned to a case.
         *
         * Called by enforcement.cpp when an arrest is recorded. Updates
         * subsystem1.officers.active_case_count to keep officer workload
         * accurate for assignment decisions.
         *
         * The bridge executes: UPDATE subsystem1.officers SET active_case_count = active_case_count + 1
         *                      WHERE officer_id = officer_id
         *
         * This ensures Subsystem 1 stays aware of S3 case assignments without
         * direct coupling.
         *
         * @param officer_id The officer being assigned
         * @param case_id The case they're assigned to
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if officer doesn't exist
         *         ResultCode::DB_ERROR on query failure
         *
         * @note Database operation: UPDATE subsystem1.officers SET active_case_count = ...
         *       Also triggers audit.Audit_Log entry via trigger (SECURITY DEFINER).
         *
         * @example
         *   // When arrest is recorded:
         *   auto result = S1Bridge::notifyOfficerCaseAssignment(arrest.arresting_officer_id, case_id);
         */
        static JusticeFlow::ResultCode notifyOfficerCaseAssignment(int officer_id, int case_id);
    };

} // namespace integration