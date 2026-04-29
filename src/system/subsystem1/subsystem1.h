#pragma once

/**
 * @file subsystem1.h
 * @brief Public API facade for Subsystem 1 (Crime Intelligence & Resource Optimization).
 *
 * This is the only header that the API gateway / router should include for S1.
 *
 * Subsystem 1 modules:
 *   - Case Management  — register, update, close, reopen, transfer cases;
 *                        manage complainants, victims, witnesses, accused.
 *   - Duty & Patrol    — schedule and track officer duty shifts; manage patrol routes.
 *   - Officers & Personnel — officer profile, rank history, deployments, status.
 *
 * Design pattern: Facade
 *   Wraps internal managers (CaseManager, DutyManager, PersonnelManager) and
 *   presents a stable S1 API surface to the rest of the system.
 *
 * Internal design patterns (in-process, not exposed here):
 *   - Strategy Pattern  : CaseTransitionStrategy (rank-based case closure authorization)
 *   - Observer Pattern  : EvidenceMgr notifies audit logger and AI triggers on evidence events
 *   - Factory Pattern   : ReportFactory creates DailySummary / ChainOfCustody / CaseHistory
 *
 * Notes:
 *   - All write operations require a validated JusticeFlow::SessionContext obtained
 *     from the Shared Infrastructure / Auth layer before calling S1.
 *   - All operations take a PGconn* from the caller's connection pool.
 *   - Out-parameters follow the convention used in S3: bool return = success/failure,
 *     ResultCode& out_code carries the reason on failure.
 */

#include <vector>
#include <ctime>
#include <string>

#include <postgresql/libpq-fe.h>

#include "common/constants.h"
#include "common/common.h"

// --- Subsystem 1 internal managers (facade targets) ---
#include "include/case_manager.h"
#include "include/case_strategy.h"
#include "include/officer_manager.h"

namespace subsystem1
{
    class Subsystem1
    {
    public:
        // ============================================================
        // Case Management — Core CRUD
        // ============================================================

        /**
         * @brief Register a new FIR / case in the system.
         * @param conn         Active DB connection (justice_app role).
         * @param session      Validated session of the filing officer.
         * @param case_type    Nature of the offence (CaseType enum).
         * @param incident_date Unix timestamp of when the offence occurred.
         * @param incident_address  Free-text address of the crime scene.
         * @param description  Narrative description of the incident.
         * @param lat / lon    GPS coordinates of the incident location.
         * @param station_id   Registering station.
         * @param complainant_cnic CNIC of the primary complainant.
         * @param out_case_id  [out] Newly assigned case ID on success.
         * @param out_code     [out] ResultCode explaining failure.
         * @return true on success, false otherwise.
         */
        static bool registerCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            JusticeFlow::CaseType case_type,
            time_t incident_date,
            const char *incident_address,
            const char *description,
            double lat,
            double lon,
            int station_id,
            const char *complainant_cnic,
            int &out_case_id,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Retrieve a single case record by its primary key.
         */
        static JusticeFlow::ResultCode getCaseById(
            PGconn *conn,
            int case_id,
            JusticeFlow::Case &out);

        /**
         * @brief Retrieve all cases registered at a given station.
         */
        static JusticeFlow::ResultCode getCasesByStation(
            PGconn *conn,
            int station_id,
            std::vector<JusticeFlow::Case> &out);

        /**
         * @brief Retrieve cases filtered by status (e.g. UNDER_INVESTIGATION).
         */
        static JusticeFlow::ResultCode getCasesByStatus(
            PGconn *conn,
            int station_id,
            JusticeFlow::CaseStatus status,
            std::vector<JusticeFlow::Case> &out);

        // ============================================================
        // Case Management — Status Transitions
        // ============================================================

        /**
         * @brief Advance a case to a new status.
         * Internally applies the CaseTransitionStrategy to enforce rank-based
         * authorization rules before writing to the DB.
         *
         * @param reason       Mandatory reason text logged in case_status_log.
         */
        static bool updateCaseStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            JusticeFlow::CaseStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Close a case. Requires SP rank or above for serious offences.
         * Uses the Strategy Pattern internally to enforce rank rules.
         */
        static bool closeCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *closure_reason,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Reopen a previously closed case.
         * Requires DSP rank or above; writes to case_status_log.
         */
        static bool reopenCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *reopen_reason,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Transfer case jurisdiction to another station.
         * Writes to case_jurisdiction_history. Requires DIG+ rank.
         */
        static bool transferCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            int to_station_id,
            const char *transfer_reason,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Retrieve the full status-change log for a case.
         */
        static JusticeFlow::ResultCode getCaseStatusLog(
            PGconn *conn,
            int case_id,
            std::vector<JusticeFlow::CaseStatusLog> &out);

        // ============================================================
        // Case Management — Officer Assignment
        // ============================================================

        /**
         * @brief Assign an officer to a case with a specific role.
         * Validates that the officer belongs to the case station.
         */
        static bool assignOfficerToCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            int officer_id,
            JusticeFlow::CaseOfficerRole role,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Relieve an officer from a case (set relieved_at timestamp).
         */
        static bool relieveOfficerFromCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            int officer_id,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief List all officers currently assigned to a case.
         */
        static JusticeFlow::ResultCode getAssignedOfficers(
            PGconn *conn,
            int case_id,
            std::vector<JusticeFlow::CaseOfficer> &out);

        // ============================================================
        // Case Management — Complainants
        // ============================================================

        static bool addComplainant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *person_cnic,
            JusticeFlow::RelationshipToVictim relation,
            bool notify_on_update,
            int &out_complainant_id,
            JusticeFlow::ResultCode &out_code);

        static bool updateComplainantStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int complainant_id,
            JusticeFlow::ComplainantStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getComplainantsByCase(
            PGconn *conn,
            int case_id,
            std::vector<JusticeFlow::Complainant> &out);

        // ============================================================
        // Case Management — Victims
        // ============================================================

        static bool addVictim(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *person_cnic,
            const char *injury_type,
            JusticeFlow::InjurySeverity injury_severity,
            JusticeFlow::VulnerabilityCategory vulnerability,
            const char *medical_report_ref,
            int &out_victim_id,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getVictimsByCase(
            PGconn *conn,
            int case_id,
            std::vector<JusticeFlow::Victim> &out);

        // ============================================================
        // Case Management — Witnesses
        // ============================================================

        static bool addWitness(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *person_cnic,
            const char *statement_text,
            const char *statement_file_path,
            JusticeFlow::WitnessProtection protection_status,
            bool conceal_identity,
            int &out_witness_id,
            JusticeFlow::ResultCode &out_code);

        static bool updateWitnessProtection(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int witness_id,
            JusticeFlow::WitnessProtection new_status,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getWitnessesByCase(
            PGconn *conn,
            int case_id,
            std::vector<JusticeFlow::Witness> &out);

        // ============================================================
        // Case Management — Accused
        // ============================================================

        static bool addAccused(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *person_cnic,
            JusticeFlow::InvolvementType involvement,
            int &out_accused_id,
            JusticeFlow::ResultCode &out_code);

        static bool linkAccusedAssociation(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int accused_id,
            int associated_accused_id,
            JusticeFlow::AssociationType association_type,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getAccusedByCase(
            PGconn *conn,
            int case_id,
            std::vector<JusticeFlow::Accused> &out);

        // ============================================================
        // Case Management — Vehicles
        // ============================================================

        static bool linkVehicleToCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            int vehicle_id,
            JusticeFlow::VehicleRole role,
            const char *condition_notes,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getVehiclesByCase(
            PGconn *conn,
            int case_id,
            std::vector<JusticeFlow::VehicleCase> &out);

        // ============================================================
        // Duty & Patrol — Scheduling
        // ============================================================

        /**
         * @brief Schedule a duty shift for an officer.
         * Validates that the officer is ACTIVE and not already scheduled for
         * the same date/shift before inserting.
         *
         * @param patrol_route_id  Pass 0 / -1 if no specific patrol route is assigned.
         * @param out_duty_id      [out] Newly created duty roster ID.
         */
        static bool scheduleDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            int station_id,
            int patrol_route_id,
            JusticeFlow::ShiftType shift_type,
            const char *duty_date, // "YYYY-MM-DD"
            time_t scheduled_start,
            time_t scheduled_end,
            int &out_duty_id,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Mark an officer as having started their duty shift.
         * Transitions status from SCHEDULED → ON_DUTY and records actual_start.
         */
        static bool markDutyStart(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Mark an officer as having completed their duty shift.
         * Transitions ON_DUTY → COMPLETED and records actual_end.
         */
        static bool markDutyEnd(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Update the status of a duty entry (e.g. mark as ABSENT, ON_LEAVE).
         * @param absence_reason  Required when new_status is ABSENT or ON_LEAVE.
         */
        static bool updateDutyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::DutyStatus new_status,
            const char *absence_reason,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Cancel / delete a scheduled duty before it starts.
         * Only SCHEDULED entries may be cancelled.
         */
        static bool cancelDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        // ============================================================
        // Duty & Patrol — Queries
        // ============================================================

        /**
         * @brief Retrieve the full duty roster for a station on a given date.
         * @param duty_date  "YYYY-MM-DD" format; pass nullptr for today.
         */
        static JusticeFlow::ResultCode getDutyRoster(
            PGconn *conn,
            int station_id,
            const char *duty_date,
            std::vector<JusticeFlow::DutyRoster> &out);

        /**
         * @brief Retrieve all currently ON_DUTY officers at a station.
         */
        static JusticeFlow::ResultCode getActiveDuties(
            PGconn *conn,
            int station_id,
            std::vector<JusticeFlow::DutyRoster> &out);

        /**
         * @brief Retrieve the duty history for a specific officer.
         * @param from / to  Unix timestamps for the date range window.
         */
        static JusticeFlow::ResultCode getOfficerDutyHistory(
            PGconn *conn,
            int officer_id,
            time_t from,
            time_t to,
            std::vector<JusticeFlow::DutyRoster> &out);

        // ============================================================
        // Duty & Patrol — Patrol Routes
        // ============================================================

        /**
         * @brief Create a new patrol beat / route for a station.
         * @param out_route_id  [out] Newly assigned patrol route ID.
         */
        static bool createPatrolRoute(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int station_id,
            const char *beat_code,
            const char *route_name,
            const char *area_description,
            int &out_route_id,
            JusticeFlow::ResultCode &out_code);

        static bool deactivatePatrolRoute(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int route_id,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getPatrolRoutesByStation(
            PGconn *conn,
            int station_id,
            std::vector<JusticeFlow::PatrolRoute> &out);

        // ============================================================
        // Officers & Personnel — Profiles
        // ============================================================

        /**
         * @brief Fetch a single officer record by their internal officer_id.
         */
        static JusticeFlow::ResultCode getOfficerById(
            PGconn *conn,
            int officer_id,
            JusticeFlow::Officer &out);

        /**
         * @brief Fetch a single officer record by their CNIC.
         */
        static JusticeFlow::ResultCode getOfficerByCnic(
            PGconn *conn,
            const char *cnic,
            JusticeFlow::Officer &out);

        /**
         * @brief Retrieve all officers currently posted at a station.
         */
        static JusticeFlow::ResultCode getOfficersByStation(
            PGconn *conn,
            int station_id,
            std::vector<JusticeFlow::Officer> &out);

        /**
         * @brief Filter officers by their current status.
         */
        static JusticeFlow::ResultCode getOfficersByStatus(
            PGconn *conn,
            int station_id,
            JusticeFlow::OfficerStatus status,
            std::vector<JusticeFlow::Officer> &out);

        // ============================================================
        // Officers & Personnel — Status & Rank
        // ============================================================

        /**
         * @brief Update an officer's operational status (e.g. SUSPENDED, ON_LEAVE).
         * Requires DSP rank or above when suspending.
         */
        static bool updateOfficerStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerStatus new_status,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Record a rank promotion or demotion for an officer.
         * Inserts a row into officer_rank_history; updates officers table.
         *
         * @param new_belt_number  New belt number assigned at promotion (if changed).
         * @param promotion_type   e.g. "TIME_SCALE", "MERIT", "DEMOTION"
         * @param effective_date   "YYYY-MM-DD"
         * @param order_date       "YYYY-MM-DD" of the official gazette order.
         * @param out_history_id   [out] New rank history record ID.
         */
        static bool promoteOfficer(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerRank new_rank,
            const char *new_belt_number,
            const char *promotion_type,
            const char *effective_date,
            const char *order_date,
            int &out_history_id,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Retrieve the full rank history for an officer.
         */
        static JusticeFlow::ResultCode getOfficerRankHistory(
            PGconn *conn,
            int officer_id,
            std::vector<JusticeFlow::OfficerRankHistory> &out);

        // ============================================================
        // Officers & Personnel — Deployments
        // ============================================================

        /**
         * @brief Deploy an officer from one station to another.
         * Sets the old deployment to is_active=false and inserts a new active one.
         *
         * @param deployed_from  "YYYY-MM-DD"
         * @param deployed_until "YYYY-MM-DD", or nullptr for indefinite.
         * @param out_deployment_id  [out] New deployment record ID.
         */
        static bool deployOfficer(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            int to_station_id,
            const char *deployment_reason,
            const char *order_number,
            const char *deployed_from,
            const char *deployed_until,
            int &out_deployment_id,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief End an active deployment (marks is_active = false).
         */
        static bool endDeployment(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int deployment_id,
            JusticeFlow::ResultCode &out_code);

        /**
         * @brief Retrieve all active or historical deployments for an officer.
         */
        static JusticeFlow::ResultCode getOfficerDeployments(
            PGconn *conn,
            int officer_id,
            bool active_only,
            std::vector<JusticeFlow::OfficerDeployment> &out);

        // ============================================================
        // Officers & Personnel — Reports (Factory pattern)
        // ============================================================

        /**
         * @brief Generate a report for an officer and write it to the provided buffer.
         * Internally uses the ReportFactory to select the correct report type.
         * Logs report generation to the audit trail via the Observer pattern.
         *
         * @param type   DAILY_SUMMARY | CHAIN_OF_CUSTODY | CASE_HISTORY
         * @param out_report_text  [out] Generated plain-text report content.
         */
        static JusticeFlow::ResultCode generateOfficerReport(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            ReportType type,
            std::string &out_report_text);
    };

} // namespace subsystem1