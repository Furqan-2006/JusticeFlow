/**
 * @file system.cpp
 * @brief SystemManager implementation: concrete adapters + facade routing.
 *
 * File layout
 * ──────────────────────────────────────────────────────────────────────────
 * Section 1  Default concrete Adapter implementations
 *              DefaultAuthAdapter       — delegates to auth::AuthManager
 *              DefaultSubsystem1Adapter — delegates to subsystem1::Subsystem1
 *              DefaultSubsystem2Adapter — delegates to subsystem2::Subsystem2
 *              DefaultSubsystem3Adapter — delegates to subsystem3::Subsystem3
 *
 * Section 2  SystemManager — Singleton + Lifecycle (Manager pattern)
 *
 * Section 3  SystemManager — Auth facade
 *
 * Section 4  SystemManager — Subsystem 1 facade (Case, Duty, Personnel)
 *
 * Section 5  SystemManager — Subsystem 2 facade (Investigation UCs)
 *
 * Section 6  SystemManager — Subsystem 3 facade (Audit, Enforcement, Forensic)
 * ──────────────────────────────────────────────────────────────────────────
 *
 * Every public method in Sections 3-6 follows the same three-step pattern:
 *   1. guardInitialized() — abort with NOT_INITIALIZED if init() was skipped.
 *   2. Delegate to the injected adapter (virtual dispatch).
 *   3. Return the result unchanged — no business logic lives here.
 */

#include "system.h"
#include "common/logger.h"

namespace system_layer
{

    // =============================================================================
    // Section 1 — Default Concrete Adapters (Adapter Pattern)
    // =============================================================================
    // Each concrete adapter is an internal implementation detail — NOT exported.
    // They are only ever created by SystemManager::init() when no injection has
    // been provided for that slot.
    // =============================================================================

    // -----------------------------------------------------------------------------
    // DefaultAuthAdapter
    // -----------------------------------------------------------------------------

    class DefaultAuthAdapter final : public IAuthAdapter
    {
    public:
        JusticeFlow::ResultCode login(
            const char *cnic,
            const char *password,
            std::string &out_token) override
        {
            return auth::AuthManager::getInstance().login(cnic, password, out_token);
        }

        JusticeFlow::ResultCode validateToken(
            const char *token,
            JusticeFlow::SessionContext &out_session) override
        {
            return auth::AuthManager::getInstance().validateToken(token, out_session);
        }

        JusticeFlow::ResultCode validateRank(
            const JusticeFlow::SessionContext &session,
            JusticeFlow::OfficerRank required_rank) override
        {
            return auth::AuthManager::getInstance().validateRank(session, required_rank);
        }

        bool isDutyActive(int officer_id) override
        {
            return auth::AuthManager::getInstance().isDutyActive(officer_id);
        }

        JusticeFlow::ResultCode refreshSession(const char *token) override
        {
            return auth::AuthManager::getInstance().refreshSession(token);
        }

        JusticeFlow::ResultCode logout(const char *token) override
        {
            return auth::AuthManager::getInstance().logout(token);
        }
    };

    // -----------------------------------------------------------------------------
    // DefaultSubsystem1Adapter
    // Bridges SystemManager's virtual-dispatch surface to S1's all-static interface.
    // -----------------------------------------------------------------------------

    class DefaultSubsystem1Adapter final : public ISubsystem1Adapter
    {
    public:
        // ── Case CRUD ─────────────────────────────────────────────────────────────

        bool registerCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            JusticeFlow::CaseType case_type,
            time_t incident_date,
            const char *incident_address,
            const char *description,
            double lat, double lon,
            int station_id,
            const char *complainant_cnic,
            int &out_case_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::registerCase(
                conn, session, case_type, incident_date,
                incident_address, description, lat, lon,
                station_id, complainant_cnic, out_case_id, out_code);
        }

        JusticeFlow::ResultCode getCaseById(
            PGconn *conn, int case_id,
            JusticeFlow::Case &out) override
        {
            return subsystem1::Subsystem1::getCaseById(conn, case_id, out);
        }

        JusticeFlow::ResultCode getCasesByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::Case> &out) override
        {
            return subsystem1::Subsystem1::getCasesByStation(conn, station_id, out);
        }

        JusticeFlow::ResultCode getCasesByStatus(
            PGconn *conn, int station_id,
            JusticeFlow::CaseStatus status,
            std::vector<JusticeFlow::Case> &out) override
        {
            return subsystem1::Subsystem1::getCasesByStatus(conn, station_id, status, out);
        }

        // ── Status Transitions ────────────────────────────────────────────────────

        bool updateCaseStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            JusticeFlow::CaseStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::updateCaseStatus(
                conn, session, case_id, new_status, reason, out_code);
        }

        bool closeCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *closure_reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::closeCase(
                conn, session, case_id, closure_reason, out_code);
        }

        bool reopenCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *reopen_reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::reopenCase(
                conn, session, case_id, reopen_reason, out_code);
        }

        bool transferCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            int to_station_id,
            const char *transfer_reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::transferCase(
                conn, session, case_id, to_station_id, transfer_reason, out_code);
        }

        JusticeFlow::ResultCode getCaseStatusLog(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::CaseStatusLog> &out) override
        {
            return subsystem1::Subsystem1::getCaseStatusLog(conn, case_id, out);
        }

        // ── Officer Assignment ────────────────────────────────────────────────────

        bool assignOfficerToCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int officer_id,
            JusticeFlow::CaseOfficerRole role,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::assignOfficerToCase(
                conn, session, case_id, officer_id, role, out_code);
        }

        bool relieveOfficerFromCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int officer_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::relieveOfficerFromCase(
                conn, session, case_id, officer_id, out_code);
        }

        JusticeFlow::ResultCode getAssignedOfficers(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::CaseOfficer> &out) override
        {
            return subsystem1::Subsystem1::getAssignedOfficers(conn, case_id, out);
        }

        // ── Complainants ──────────────────────────────────────────────────────────

        bool addComplainant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, const char *person_cnic,
            JusticeFlow::RelationshipToVictim relation,
            bool notify_on_update,
            int &out_complainant_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::addComplainant(
                conn, session, case_id, person_cnic,
                relation, notify_on_update, out_complainant_id, out_code);
        }

        bool updateComplainantStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int complainant_id,
            JusticeFlow::ComplainantStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::updateComplainantStatus(
                conn, session, complainant_id, new_status, reason, out_code);
        }

        JusticeFlow::ResultCode getComplainantsByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Complainant> &out) override
        {
            return subsystem1::Subsystem1::getComplainantsByCase(conn, case_id, out);
        }

        // ── Victims ───────────────────────────────────────────────────────────────

        bool addVictim(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, const char *person_cnic,
            const char *injury_type,
            JusticeFlow::InjurySeverity injury_severity,
            JusticeFlow::VulnerabilityCategory vulnerability,
            const char *medical_report_ref,
            int &out_victim_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::addVictim(
                conn, session, case_id, person_cnic,
                injury_type, injury_severity, vulnerability,
                medical_report_ref, out_victim_id, out_code);
        }

        JusticeFlow::ResultCode getVictimsByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Victim> &out) override
        {
            return subsystem1::Subsystem1::getVictimsByCase(conn, case_id, out);
        }

        // ── Witnesses ─────────────────────────────────────────────────────────────

        bool addWitness(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, const char *person_cnic,
            const char *statement_text,
            const char *statement_file_path,
            JusticeFlow::WitnessProtection protection_status,
            bool conceal_identity,
            int &out_witness_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::addWitness(
                conn, session, case_id, person_cnic,
                statement_text, statement_file_path,
                protection_status, conceal_identity,
                out_witness_id, out_code);
        }

        bool updateWitnessProtection(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int witness_id,
            JusticeFlow::WitnessProtection new_status,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::updateWitnessProtection(
                conn, session, witness_id, new_status, out_code);
        }

        JusticeFlow::ResultCode getWitnessesByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Witness> &out) override
        {
            return subsystem1::Subsystem1::getWitnessesByCase(conn, case_id, out);
        }

        // ── Accused ───────────────────────────────────────────────────────────────

        bool addAccused(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, const char *person_cnic,
            JusticeFlow::InvolvementType involvement,
            int &out_accused_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::addAccused(
                conn, session, case_id, person_cnic,
                involvement, out_accused_id, out_code);
        }

        bool linkAccusedAssociation(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int accused_id, int associated_accused_id,
            JusticeFlow::AssociationType association_type,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::linkAccusedAssociation(
                conn, session, accused_id, associated_accused_id,
                association_type, out_code);
        }

        JusticeFlow::ResultCode getAccusedByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Accused> &out) override
        {
            return subsystem1::Subsystem1::getAccusedByCase(conn, case_id, out);
        }

        // ── Vehicles ──────────────────────────────────────────────────────────────

        bool linkVehicleToCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int vehicle_id,
            JusticeFlow::VehicleRole role,
            const char *condition_notes,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::linkVehicleToCase(
                conn, session, case_id, vehicle_id, role, condition_notes, out_code);
        }

        JusticeFlow::ResultCode getVehiclesByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::VehicleCase> &out) override
        {
            return subsystem1::Subsystem1::getVehiclesByCase(conn, case_id, out);
        }

        // ── Duty Scheduling ───────────────────────────────────────────────────────

        bool scheduleDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id, int station_id, int patrol_route_id,
            JusticeFlow::ShiftType shift_type,
            const char *duty_date,
            time_t scheduled_start, time_t scheduled_end,
            int &out_duty_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::scheduleDuty(
                conn, session, officer_id, station_id, patrol_route_id,
                shift_type, duty_date,
                scheduled_start, scheduled_end,
                out_duty_id, out_code);
        }

        bool markDutyStart(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::markDutyStart(conn, session, duty_id, out_code);
        }

        bool markDutyEnd(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::markDutyEnd(conn, session, duty_id, out_code);
        }

        bool updateDutyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::DutyStatus new_status,
            const char *absence_reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::updateDutyStatus(
                conn, session, duty_id, new_status, absence_reason, out_code);
        }

        bool cancelDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::cancelDuty(conn, session, duty_id, out_code);
        }

        JusticeFlow::ResultCode getDutyRoster(
            PGconn *conn, int station_id,
            const char *duty_date,
            std::vector<JusticeFlow::DutyRoster> &out) override
        {
            return subsystem1::Subsystem1::getDutyRoster(conn, station_id, duty_date, out);
        }

        JusticeFlow::ResultCode getActiveDuties(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::DutyRoster> &out) override
        {
            return subsystem1::Subsystem1::getActiveDuties(conn, station_id, out);
        }

        JusticeFlow::ResultCode getOfficerDutyHistory(
            PGconn *conn, int officer_id,
            time_t from, time_t to,
            std::vector<JusticeFlow::DutyRoster> &out) override
        {
            return subsystem1::Subsystem1::getOfficerDutyHistory(
                conn, officer_id, from, to, out);
        }

        // ── Patrol Routes ─────────────────────────────────────────────────────────

        bool createPatrolRoute(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int station_id,
            const char *beat_code,
            const char *route_name,
            const char *area_description,
            int &out_route_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::createPatrolRoute(
                conn, session, station_id, beat_code, route_name,
                area_description, out_route_id, out_code);
        }

        bool deactivatePatrolRoute(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int route_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::deactivatePatrolRoute(
                conn, session, route_id, out_code);
        }

        JusticeFlow::ResultCode getPatrolRoutesByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::PatrolRoute> &out) override
        {
            return subsystem1::Subsystem1::getPatrolRoutesByStation(conn, station_id, out);
        }

        // ── Personnel ─────────────────────────────────────────────────────────────

        JusticeFlow::ResultCode getOfficerById(
            PGconn *conn, int officer_id,
            JusticeFlow::Officer &out) override
        {
            return subsystem1::Subsystem1::getOfficerById(conn, officer_id, out);
        }

        JusticeFlow::ResultCode getOfficerByCnic(
            PGconn *conn, const char *cnic,
            JusticeFlow::Officer &out) override
        {
            return subsystem1::Subsystem1::getOfficerByCnic(conn, cnic, out);
        }

        JusticeFlow::ResultCode getOfficersByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::Officer> &out) override
        {
            return subsystem1::Subsystem1::getOfficersByStation(conn, station_id, out);
        }

        JusticeFlow::ResultCode getOfficersByStatus(
            PGconn *conn, int station_id,
            JusticeFlow::OfficerStatus status,
            std::vector<JusticeFlow::Officer> &out) override
        {
            return subsystem1::Subsystem1::getOfficersByStatus(conn, station_id, status, out);
        }

        bool updateOfficerStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerStatus new_status,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::updateOfficerStatus(
                conn, session, officer_id, new_status, out_code);
        }

        bool promoteOfficer(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerRank new_rank,
            const char *new_belt_number,
            const char *promotion_type,
            const char *effective_date,
            const char *order_date,
            int &out_history_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::promoteOfficer(
                conn, session, officer_id, new_rank,
                new_belt_number, promotion_type,
                effective_date, order_date,
                out_history_id, out_code);
        }

        JusticeFlow::ResultCode getOfficerRankHistory(
            PGconn *conn, int officer_id,
            std::vector<JusticeFlow::OfficerRankHistory> &out) override
        {
            return subsystem1::Subsystem1::getOfficerRankHistory(conn, officer_id, out);
        }

        bool deployOfficer(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id, int to_station_id,
            const char *deployment_reason,
            const char *order_number,
            const char *deployed_from,
            const char *deployed_until,
            int &out_deployment_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::deployOfficer(
                conn, session, officer_id, to_station_id,
                deployment_reason, order_number,
                deployed_from, deployed_until,
                out_deployment_id, out_code);
        }

        bool endDeployment(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int deployment_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem1::Subsystem1::endDeployment(
                conn, session, deployment_id, out_code);
        }

        JusticeFlow::ResultCode getOfficerDeployments(
            PGconn *conn, int officer_id,
            bool active_only,
            std::vector<JusticeFlow::OfficerDeployment> &out) override
        {
            return subsystem1::Subsystem1::getOfficerDeployments(
                conn, officer_id, active_only, out);
        }

        JusticeFlow::ResultCode generateOfficerReport(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            subsystem1::ReportType type,
            std::string &out_report_text) override
        {
            return subsystem1::Subsystem1::generateOfficerReport(
                conn, session, officer_id, type, out_report_text);
        }
    };

    // -----------------------------------------------------------------------------
    // DefaultSubsystem2Adapter
    // Bridges SystemManager to the Subsystem2 singleton.
    // Preserves S2's heap-allocated entity return contract.
    // -----------------------------------------------------------------------------

    class DefaultSubsystem2Adapter final : public ISubsystem2Adapter
    {
    public:
        JusticeFlow::ResultCode registerFIR(
            const subsystem2::FIRRegistrationRequest &request,
            const JusticeFlow::SessionContext &session,
            subsystem2::Case *&out_case) override
        {
            return subsystem2::Subsystem2::getInstance().registerFIR(
                request, session, out_case);
        }

        JusticeFlow::ResultCode logAndSecureEvidence(
            int64_t case_id,
            JusticeFlow::EvidenceType type,
            const std::string &description,
            const std::string &file_path,
            const JusticeFlow::SessionContext &session,
            subsystem2::Evidence *&out_evidence) override
        {
            return subsystem2::Subsystem2::getInstance().logAndSecureEvidence(
                case_id, type, description, file_path, session, out_evidence);
        }

        JusticeFlow::ResultCode draftChargeSheet(
            int64_t case_id,
            const JusticeFlow::SessionContext &session,
            subsystem2::ChargeSheet *&out_sheet) override
        {
            return subsystem2::Subsystem2::getInstance().draftChargeSheet(
                case_id, session, out_sheet);
        }

        JusticeFlow::ResultCode submitChargeSheet(
            subsystem2::ChargeSheet *sheet,
            const JusticeFlow::SessionContext &session) override
        {
            return subsystem2::Subsystem2::getInstance().submitChargeSheet(sheet, session);
        }

        JusticeFlow::ResultCode fetchCase(
            int64_t case_id,
            subsystem2::Case *&out_case) override
        {
            return subsystem2::Subsystem2::getInstance().fetchCase(case_id, out_case);
        }
    };

    // -----------------------------------------------------------------------------
    // DefaultSubsystem3Adapter
    // Bridges SystemManager to the mixed-convention Subsystem3 static facade.
    // Audit lifecycle is managed externally by SystemManager::init/shutdown.
    // -----------------------------------------------------------------------------

    class DefaultSubsystem3Adapter final : public ISubsystem3Adapter
    {
    public:
        // ── Audit ─────────────────────────────────────────────────────────────────

        JusticeFlow::ResultCode getAuditChangeHistory(
            int case_id,
            std::vector<audit::AuditRecord> &out) override
        {
            return subsystem3::Subsystem3::getAuditChangeHistory(case_id, out);
        }

        JusticeFlow::ResultCode getAuditOfficerActions(
            int officer_id,
            time_t from, time_t to,
            std::vector<audit::AuditRecord> &out) override
        {
            return subsystem3::Subsystem3::getAuditOfficerActions(
                officer_id, from, to, out);
        }

        JusticeFlow::ResultCode getAuditTableChanges(
            const char *table_name,
            int record_id,
            std::vector<audit::AuditRecord> &out) override
        {
            return subsystem3::Subsystem3::getAuditTableChanges(
                table_name, record_id, out);
        }

        JusticeFlow::ResultCode auditQueryByTimeWindow(
            time_t from, time_t to,
            std::vector<audit::AuditRecord> &out) override
        {
            return subsystem3::Subsystem3::auditQueryByTimeWindow(from, to, out);
        }

        JusticeFlow::ResultCode detectSuspiciousActivity(
            int station_id,
            std::vector<audit::AuditRecord> &out) override
        {
            return subsystem3::Subsystem3::detectSuspiciousActivity(station_id, out);
        }

        // ── Warrants ──────────────────────────────────────────────────────────────

        bool requestWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *accused_cnic,
            JusticeFlow::WarrantType warrant_type,
            const char *magistrate_name,
            const char *issuing_court,
            const char *valid_until,
            const char *target_address,
            int &out_warrant_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem3::Subsystem3::requestWarrant(
                conn, session, case_id, accused_cnic, warrant_type,
                magistrate_name, issuing_court, valid_until, target_address,
                out_warrant_id, out_code);
        }

        bool executeWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int warrant_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem3::Subsystem3::executeWarrant(
                conn, session, warrant_id, out_code);
        }

        bool cancelWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int warrant_id,
            const char *cancellation_reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem3::Subsystem3::cancelWarrant(
                conn, session, warrant_id, cancellation_reason, out_code);
        }

        JusticeFlow::ResultCode getWarrantsByCase(
            PGconn *conn, int case_id,
            std::vector<enforcement::WarrantRecord> &out) override
        {
            return subsystem3::Subsystem3::getWarrantsByCase(conn, case_id, out);
        }

        JusticeFlow::ResultCode getActiveWarrants(
            PGconn *conn, int station_id,
            std::vector<enforcement::WarrantRecord> &out) override
        {
            return subsystem3::Subsystem3::getActiveWarrants(conn, station_id, out);
        }

        // ── Arrests ───────────────────────────────────────────────────────────────

        bool recordArrest(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *accused_cnic,
            const char *arrest_location,
            int warrant_id,
            int &out_arrest_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem3::Subsystem3::recordArrest(
                conn, session, case_id, accused_cnic,
                arrest_location, warrant_id, out_arrest_id, out_code);
        }

        bool updateCustodyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            JusticeFlow::CustodyStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem3::Subsystem3::updateCustodyStatus(
                conn, session, arrest_id, new_status, reason, out_code);
        }

        bool markArrestAsDisputed(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            const char *dispute_reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem3::Subsystem3::markArrestAsDisputed(
                conn, session, arrest_id, dispute_reason, out_code);
        }

        JusticeFlow::ResultCode getArrestsByCase(
            PGconn *conn, int case_id,
            std::vector<enforcement::ArrestRecord> &out) override
        {
            return subsystem3::Subsystem3::getArrestsByCase(conn, case_id, out);
        }

        // ── Bail ──────────────────────────────────────────────────────────────────

        bool recordBail(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            JusticeFlow::BailType bail_type,
            uint64_t bail_amount_paise,
            const char *court_name,
            const char *magistrate_name,
            const char *valid_until,
            const char *surety_name,
            const char *surety_cnic,
            const char *surety_contact,
            int &out_bail_id,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem3::Subsystem3::recordBail(
                conn, session, arrest_id, bail_type, bail_amount_paise,
                court_name, magistrate_name, valid_until,
                surety_name, surety_cnic, surety_contact,
                out_bail_id, out_code);
        }

        bool revokeBail(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int bail_id,
            const char *revocation_reason,
            JusticeFlow::ResultCode &out_code) override
        {
            return subsystem3::Subsystem3::revokeBail(
                conn, session, bail_id, revocation_reason, out_code);
        }

        JusticeFlow::ResultCode getBailByArrest(
            PGconn *conn, int arrest_id,
            enforcement::BailRecord &out) override
        {
            return subsystem3::Subsystem3::getBailByArrest(conn, arrest_id, out);
        }

        // ── Forensic & Lab ────────────────────────────────────────────────────────

        JusticeFlow::ResultCode createForensicRequest(
            const char *token,
            int case_id,
            const char *examination_purpose,
            const char *purpose_description,
            const char *lab_name,
            const char *examiner_name,
            int &out_request_id) override
        {
            return subsystem3::Subsystem3::createForensicRequest(
                token, case_id, examination_purpose, purpose_description,
                lab_name, examiner_name, out_request_id);
        }

        JusticeFlow::ResultCode linkEvidence(
            const char *token,
            int request_id,
            int evidence_id,
            const char *notes) override
        {
            return subsystem3::Subsystem3::linkEvidence(
                token, request_id, evidence_id, notes);
        }

        JusticeFlow::ResultCode recordLabReceipt(
            const char *token,
            int request_id,
            const char *received_date) override
        {
            return subsystem3::Subsystem3::recordLabReceipt(
                token, request_id, received_date);
        }

        JusticeFlow::ResultCode recordExaminationStart(
            const char *token,
            int request_id) override
        {
            return subsystem3::Subsystem3::recordExaminationStart(token, request_id);
        }

        JusticeFlow::ResultCode recordFindings(
            const char *token,
            int request_id,
            const char *findings,
            const char *report_file_path,
            const char *delivery_date) override
        {
            return subsystem3::Subsystem3::recordFindings(
                token, request_id, findings, report_file_path, delivery_date);
        }

        JusticeFlow::ResultCode recordAmendment(
            const char *token,
            int request_id,
            const char *amended_findings) override
        {
            return subsystem3::Subsystem3::recordAmendment(
                token, request_id, amended_findings);
        }

        JusticeFlow::ResultCode contestReport(
            const char *token,
            int request_id,
            const char *contest_reason) override
        {
            return subsystem3::Subsystem3::contestReport(
                token, request_id, contest_reason);
        }

        JusticeFlow::ResultCode getForensicRequestsByCase(
            const char *token, int case_id,
            std::vector<forensic::ForensicRecord> &out) override
        {
            return subsystem3::Subsystem3::getForensicRequestsByCase(
                token, case_id, out);
        }

        JusticeFlow::ResultCode getPendingForensicRequests(
            const char *token, int station_id,
            std::vector<forensic::ForensicRecord> &out) override
        {
            return subsystem3::Subsystem3::getPendingForensicRequests(
                token, station_id, out);
        }

        JusticeFlow::ResultCode getEvidenceByForensicRequest(
            const char *token, int request_id,
            std::vector<forensic::EvidenceRef> &out) override
        {
            return subsystem3::Subsystem3::getEvidenceByForensicRequest(
                token, request_id, out);
        }
    };

    // =============================================================================
    // Section 2 — SystemManager: Singleton + Lifecycle (Manager pattern)
    // =============================================================================

    SystemManager &SystemManager::getInstance()
    {
        // C++11 §6.7: thread-safe static local initialisation.
        static SystemManager instance;
        return instance;
    }

    // ── Dependency Injection slots ────────────────────────────────────────────────

    void SystemManager::injectAuth(std::unique_ptr<IAuthAdapter> adapter)
    {
        auth_ = std::move(adapter);
    }

    void SystemManager::injectS1(std::unique_ptr<ISubsystem1Adapter> adapter)
    {
        s1_ = std::move(adapter);
    }

    void SystemManager::injectS2(std::unique_ptr<ISubsystem2Adapter> adapter)
    {
        s2_ = std::move(adapter);
    }

    void SystemManager::injectS3(std::unique_ptr<ISubsystem3Adapter> adapter)
    {
        s3_ = std::move(adapter);
    }

    // ── Lifecycle ─────────────────────────────────────────────────────────────────

    JusticeFlow::ResultCode SystemManager::init(const char *audit_conninfo)
    {
        Logger::info("[System] Initialising JusticeFlow SystemManager.");

        // Install default adapters for any slot not pre-filled by injection.
        if (!auth_)
            auth_ = std::make_unique<DefaultAuthAdapter>();
        if (!s1_)
            s1_ = std::make_unique<DefaultSubsystem1Adapter>();
        if (!s2_)
            s2_ = std::make_unique<DefaultSubsystem2Adapter>();
        if (!s3_)
            s3_ = std::make_unique<DefaultSubsystem3Adapter>();

        // Boot the S3 audit subsystem — it manages its own dedicated DB connection.
        Logger::info("[System] Connecting S3 audit manager.");
        JusticeFlow::ResultCode rc = subsystem3::Subsystem3::initAudit(audit_conninfo);
        if (rc != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[System] S3 audit init failed — system will not start.");
            return rc;
        }

        initialized_ = true;
        Logger::info("[System] SystemManager initialised successfully.");
        return JusticeFlow::ResultCode::OK;
    }

    void SystemManager::shutdown()
    {
        if (!initialized_)
            return;

        Logger::info("[System] Shutting down JusticeFlow SystemManager.");

        // Tear down in reverse-init order.
        // Auth and S1/S2 adapters hold no persistent resources beyond their
        // delegated singletons; those own their own shutdown logic.
        subsystem3::Subsystem3::shutdownAudit();
        Logger::info("[System] S3 audit connection closed.");

        initialized_ = false;
        Logger::info("[System] SystemManager shutdown complete.");
    }

    // ── Init guard ────────────────────────────────────────────────────────────────

    JusticeFlow::ResultCode SystemManager::guardInitialized(const char *caller) const
    {
        if (!initialized_)
        {
            Logger::error(
                std::string("[System] ") + caller +
                " called before SystemManager::init(). Aborting.");
            return JusticeFlow::ResultCode::NOT_INITIALIZED;
        }
        return JusticeFlow::ResultCode::OK;
    }

    // =============================================================================
    // Section 3 — Auth facade
    // =============================================================================

    JusticeFlow::ResultCode SystemManager::login(
        const char *cnic,
        const char *password,
        std::string &out_token)
    {
        if (auto rc = guardInitialized("login");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return auth_->login(cnic, password, out_token);
    }

    JusticeFlow::ResultCode SystemManager::validateToken(
        const char *token,
        JusticeFlow::SessionContext &out_session)
    {
        if (auto rc = guardInitialized("validateToken");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return auth_->validateToken(token, out_session);
    }

    JusticeFlow::ResultCode SystemManager::validateRank(
        const JusticeFlow::SessionContext &session,
        JusticeFlow::OfficerRank required_rank)
    {
        if (auto rc = guardInitialized("validateRank");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return auth_->validateRank(session, required_rank);
    }

    bool SystemManager::isDutyActive(int officer_id)
    {
        if (!initialized_)
            return false;
        return auth_->isDutyActive(officer_id);
    }

    JusticeFlow::ResultCode SystemManager::refreshSession(const char *token)
    {
        if (auto rc = guardInitialized("refreshSession");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return auth_->refreshSession(token);
    }

    JusticeFlow::ResultCode SystemManager::logout(const char *token)
    {
        if (auto rc = guardInitialized("logout");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return auth_->logout(token);
    }

    // =============================================================================
    // Section 4 — Subsystem 1 facade
    // =============================================================================

    // ── Case CRUD ─────────────────────────────────────────────────────────────────

    bool SystemManager::registerCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        JusticeFlow::CaseType case_type,
        time_t incident_date,
        const char *incident_address,
        const char *description,
        double lat, double lon,
        int station_id,
        const char *complainant_cnic,
        int &out_case_id,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("registerCase")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->registerCase(conn, session, case_type, incident_date,
                                 incident_address, description, lat, lon,
                                 station_id, complainant_cnic, out_case_id, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getCaseById(
        PGconn *conn, int case_id, JusticeFlow::Case &out)
    {
        if (auto rc = guardInitialized("getCaseById");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getCaseById(conn, case_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getCasesByStation(
        PGconn *conn, int station_id, std::vector<JusticeFlow::Case> &out)
    {
        if (auto rc = guardInitialized("getCasesByStation");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getCasesByStation(conn, station_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getCasesByStatus(
        PGconn *conn, int station_id,
        JusticeFlow::CaseStatus status,
        std::vector<JusticeFlow::Case> &out)
    {
        if (auto rc = guardInitialized("getCasesByStatus");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getCasesByStatus(conn, station_id, status, out);
    }

    // ── Status Transitions ────────────────────────────────────────────────────────

    bool SystemManager::updateCaseStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, JusticeFlow::CaseStatus new_status,
        const char *reason, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("updateCaseStatus")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->updateCaseStatus(conn, session, case_id, new_status, reason, out_code);
    }

    bool SystemManager::closeCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, const char *closure_reason,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("closeCase")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->closeCase(conn, session, case_id, closure_reason, out_code);
    }

    bool SystemManager::reopenCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, const char *reopen_reason,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("reopenCase")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->reopenCase(conn, session, case_id, reopen_reason, out_code);
    }

    bool SystemManager::transferCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, int to_station_id,
        const char *transfer_reason,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("transferCase")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->transferCase(conn, session, case_id, to_station_id,
                                 transfer_reason, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getCaseStatusLog(
        PGconn *conn, int case_id,
        std::vector<JusticeFlow::CaseStatusLog> &out)
    {
        if (auto rc = guardInitialized("getCaseStatusLog");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getCaseStatusLog(conn, case_id, out);
    }

    // ── Officer Assignment ────────────────────────────────────────────────────────

    bool SystemManager::assignOfficerToCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, int officer_id,
        JusticeFlow::CaseOfficerRole role,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("assignOfficerToCase")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->assignOfficerToCase(conn, session, case_id, officer_id, role, out_code);
    }

    bool SystemManager::relieveOfficerFromCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, int officer_id,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("relieveOfficerFromCase")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->relieveOfficerFromCase(conn, session, case_id, officer_id, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getAssignedOfficers(
        PGconn *conn, int case_id,
        std::vector<JusticeFlow::CaseOfficer> &out)
    {
        if (auto rc = guardInitialized("getAssignedOfficers");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getAssignedOfficers(conn, case_id, out);
    }

    // ── Complainants ──────────────────────────────────────────────────────────────

    bool SystemManager::addComplainant(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, const char *person_cnic,
        JusticeFlow::RelationshipToVictim relation,
        bool notify_on_update,
        int &out_complainant_id,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("addComplainant")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->addComplainant(conn, session, case_id, person_cnic,
                                   relation, notify_on_update, out_complainant_id, out_code);
    }

    bool SystemManager::updateComplainantStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int complainant_id,
        JusticeFlow::ComplainantStatus new_status,
        const char *reason, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("updateComplainantStatus")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->updateComplainantStatus(conn, session, complainant_id,
                                            new_status, reason, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getComplainantsByCase(
        PGconn *conn, int case_id,
        std::vector<JusticeFlow::Complainant> &out)
    {
        if (auto rc = guardInitialized("getComplainantsByCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getComplainantsByCase(conn, case_id, out);
    }

    // ── Victims ───────────────────────────────────────────────────────────────────

    bool SystemManager::addVictim(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, const char *person_cnic,
        const char *injury_type,
        JusticeFlow::InjurySeverity injury_severity,
        JusticeFlow::VulnerabilityCategory vulnerability,
        const char *medical_report_ref,
        int &out_victim_id,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("addVictim")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->addVictim(conn, session, case_id, person_cnic,
                              injury_type, injury_severity, vulnerability,
                              medical_report_ref, out_victim_id, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getVictimsByCase(
        PGconn *conn, int case_id, std::vector<JusticeFlow::Victim> &out)
    {
        if (auto rc = guardInitialized("getVictimsByCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getVictimsByCase(conn, case_id, out);
    }

    // ── Witnesses ─────────────────────────────────────────────────────────────────

    bool SystemManager::addWitness(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, const char *person_cnic,
        const char *statement_text,
        const char *statement_file_path,
        JusticeFlow::WitnessProtection protection_status,
        bool conceal_identity,
        int &out_witness_id,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("addWitness")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->addWitness(conn, session, case_id, person_cnic,
                               statement_text, statement_file_path,
                               protection_status, conceal_identity,
                               out_witness_id, out_code);
    }

    bool SystemManager::updateWitnessProtection(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int witness_id,
        JusticeFlow::WitnessProtection new_status,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("updateWitnessProtection")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->updateWitnessProtection(conn, session, witness_id, new_status, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getWitnessesByCase(
        PGconn *conn, int case_id, std::vector<JusticeFlow::Witness> &out)
    {
        if (auto rc = guardInitialized("getWitnessesByCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getWitnessesByCase(conn, case_id, out);
    }

    // ── Accused ───────────────────────────────────────────────────────────────────

    bool SystemManager::addAccused(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, const char *person_cnic,
        JusticeFlow::InvolvementType involvement,
        int &out_accused_id,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("addAccused")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->addAccused(conn, session, case_id, person_cnic,
                               involvement, out_accused_id, out_code);
    }

    bool SystemManager::linkAccusedAssociation(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int accused_id, int associated_accused_id,
        JusticeFlow::AssociationType association_type,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("linkAccusedAssociation")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->linkAccusedAssociation(conn, session, accused_id,
                                           associated_accused_id, association_type, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getAccusedByCase(
        PGconn *conn, int case_id, std::vector<JusticeFlow::Accused> &out)
    {
        if (auto rc = guardInitialized("getAccusedByCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getAccusedByCase(conn, case_id, out);
    }

    // ── Vehicles ──────────────────────────────────────────────────────────────────

    bool SystemManager::linkVehicleToCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, int vehicle_id,
        JusticeFlow::VehicleRole role,
        const char *condition_notes,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("linkVehicleToCase")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->linkVehicleToCase(conn, session, case_id, vehicle_id,
                                      role, condition_notes, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getVehiclesByCase(
        PGconn *conn, int case_id, std::vector<JusticeFlow::VehicleCase> &out)
    {
        if (auto rc = guardInitialized("getVehiclesByCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getVehiclesByCase(conn, case_id, out);
    }

    // ── Duty Scheduling ───────────────────────────────────────────────────────────

    bool SystemManager::scheduleDuty(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id, int station_id, int patrol_route_id,
        JusticeFlow::ShiftType shift_type,
        const char *duty_date,
        time_t scheduled_start, time_t scheduled_end,
        int &out_duty_id,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("scheduleDuty")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->scheduleDuty(conn, session, officer_id, station_id, patrol_route_id,
                                 shift_type, duty_date, scheduled_start, scheduled_end,
                                 out_duty_id, out_code);
    }

    bool SystemManager::markDutyStart(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int duty_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("markDutyStart")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->markDutyStart(conn, session, duty_id, out_code);
    }

    bool SystemManager::markDutyEnd(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int duty_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("markDutyEnd")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->markDutyEnd(conn, session, duty_id, out_code);
    }

    bool SystemManager::updateDutyStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int duty_id, JusticeFlow::DutyStatus new_status,
        const char *absence_reason, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("updateDutyStatus")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->updateDutyStatus(conn, session, duty_id, new_status,
                                     absence_reason, out_code);
    }

    bool SystemManager::cancelDuty(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int duty_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("cancelDuty")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->cancelDuty(conn, session, duty_id, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getDutyRoster(
        PGconn *conn, int station_id,
        const char *duty_date, std::vector<JusticeFlow::DutyRoster> &out)
    {
        if (auto rc = guardInitialized("getDutyRoster");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getDutyRoster(conn, station_id, duty_date, out);
    }

    JusticeFlow::ResultCode SystemManager::getActiveDuties(
        PGconn *conn, int station_id, std::vector<JusticeFlow::DutyRoster> &out)
    {
        if (auto rc = guardInitialized("getActiveDuties");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getActiveDuties(conn, station_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getOfficerDutyHistory(
        PGconn *conn, int officer_id,
        time_t from, time_t to,
        std::vector<JusticeFlow::DutyRoster> &out)
    {
        if (auto rc = guardInitialized("getOfficerDutyHistory");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getOfficerDutyHistory(conn, officer_id, from, to, out);
    }

    // ── Patrol Routes ─────────────────────────────────────────────────────────────

    bool SystemManager::createPatrolRoute(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int station_id, const char *beat_code,
        const char *route_name, const char *area_description,
        int &out_route_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("createPatrolRoute")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->createPatrolRoute(conn, session, station_id, beat_code,
                                      route_name, area_description, out_route_id, out_code);
    }

    bool SystemManager::deactivatePatrolRoute(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int route_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("deactivatePatrolRoute")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->deactivatePatrolRoute(conn, session, route_id, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getPatrolRoutesByStation(
        PGconn *conn, int station_id,
        std::vector<JusticeFlow::PatrolRoute> &out)
    {
        if (auto rc = guardInitialized("getPatrolRoutesByStation");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getPatrolRoutesByStation(conn, station_id, out);
    }

    // ── Personnel ─────────────────────────────────────────────────────────────────

    JusticeFlow::ResultCode SystemManager::getOfficerById(
        PGconn *conn, int officer_id, JusticeFlow::Officer &out)
    {
        if (auto rc = guardInitialized("getOfficerById");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getOfficerById(conn, officer_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getOfficerByCnic(
        PGconn *conn, const char *cnic, JusticeFlow::Officer &out)
    {
        if (auto rc = guardInitialized("getOfficerByCnic");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getOfficerByCnic(conn, cnic, out);
    }

    JusticeFlow::ResultCode SystemManager::getOfficersByStation(
        PGconn *conn, int station_id, std::vector<JusticeFlow::Officer> &out)
    {
        if (auto rc = guardInitialized("getOfficersByStation");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getOfficersByStation(conn, station_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getOfficersByStatus(
        PGconn *conn, int station_id,
        JusticeFlow::OfficerStatus status, std::vector<JusticeFlow::Officer> &out)
    {
        if (auto rc = guardInitialized("getOfficersByStatus");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getOfficersByStatus(conn, station_id, status, out);
    }

    bool SystemManager::updateOfficerStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id, JusticeFlow::OfficerStatus new_status,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("updateOfficerStatus")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->updateOfficerStatus(conn, session, officer_id, new_status, out_code);
    }

    bool SystemManager::promoteOfficer(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id, JusticeFlow::OfficerRank new_rank,
        const char *new_belt_number, const char *promotion_type,
        const char *effective_date, const char *order_date,
        int &out_history_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("promoteOfficer")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->promoteOfficer(conn, session, officer_id, new_rank,
                                   new_belt_number, promotion_type,
                                   effective_date, order_date,
                                   out_history_id, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getOfficerRankHistory(
        PGconn *conn, int officer_id,
        std::vector<JusticeFlow::OfficerRankHistory> &out)
    {
        if (auto rc = guardInitialized("getOfficerRankHistory");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getOfficerRankHistory(conn, officer_id, out);
    }

    bool SystemManager::deployOfficer(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id, int to_station_id,
        const char *deployment_reason, const char *order_number,
        const char *deployed_from, const char *deployed_until,
        int &out_deployment_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("deployOfficer")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->deployOfficer(conn, session, officer_id, to_station_id,
                                  deployment_reason, order_number,
                                  deployed_from, deployed_until,
                                  out_deployment_id, out_code);
    }

    bool SystemManager::endDeployment(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int deployment_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("endDeployment")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s1_->endDeployment(conn, session, deployment_id, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getOfficerDeployments(
        PGconn *conn, int officer_id, bool active_only,
        std::vector<JusticeFlow::OfficerDeployment> &out)
    {
        if (auto rc = guardInitialized("getOfficerDeployments");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->getOfficerDeployments(conn, officer_id, active_only, out);
    }

    JusticeFlow::ResultCode SystemManager::generateOfficerReport(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id,
        subsystem1::ReportType type,
        std::string &out_report_text)
    {
        if (auto rc = guardInitialized("generateOfficerReport");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s1_->generateOfficerReport(conn, session, officer_id, type, out_report_text);
    }

    // =============================================================================
    // Section 5 — Subsystem 2 facade
    // =============================================================================

    JusticeFlow::ResultCode SystemManager::registerFIR(
        const subsystem2::FIRRegistrationRequest &request,
        const JusticeFlow::SessionContext &session,
        subsystem2::Case *&out_case)
    {
        if (auto rc = guardInitialized("registerFIR");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s2_->registerFIR(request, session, out_case);
    }

    JusticeFlow::ResultCode SystemManager::logAndSecureEvidence(
        int64_t case_id,
        JusticeFlow::EvidenceType type,
        const std::string &description,
        const std::string &file_path,
        const JusticeFlow::SessionContext &session,
        subsystem2::Evidence *&out_evidence)
    {
        if (auto rc = guardInitialized("logAndSecureEvidence");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s2_->logAndSecureEvidence(case_id, type, description, file_path,
                                         session, out_evidence);
    }

    JusticeFlow::ResultCode SystemManager::draftChargeSheet(
        int64_t case_id,
        const JusticeFlow::SessionContext &session,
        subsystem2::ChargeSheet *&out_sheet)
    {
        if (auto rc = guardInitialized("draftChargeSheet");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s2_->draftChargeSheet(case_id, session, out_sheet);
    }

    JusticeFlow::ResultCode SystemManager::submitChargeSheet(
        subsystem2::ChargeSheet *sheet,
        const JusticeFlow::SessionContext &session)
    {
        if (auto rc = guardInitialized("submitChargeSheet");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s2_->submitChargeSheet(sheet, session);
    }

    JusticeFlow::ResultCode SystemManager::fetchCase(
        int64_t case_id, subsystem2::Case *&out_case)
    {
        if (auto rc = guardInitialized("fetchCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s2_->fetchCase(case_id, out_case);
    }

    // =============================================================================
    // Section 6 — Subsystem 3 facade
    // =============================================================================

    // ── Audit ─────────────────────────────────────────────────────────────────────

    JusticeFlow::ResultCode SystemManager::getAuditChangeHistory(
        int case_id, std::vector<audit::AuditRecord> &out)
    {
        if (auto rc = guardInitialized("getAuditChangeHistory");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getAuditChangeHistory(case_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getAuditOfficerActions(
        int officer_id, time_t from, time_t to,
        std::vector<audit::AuditRecord> &out)
    {
        if (auto rc = guardInitialized("getAuditOfficerActions");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getAuditOfficerActions(officer_id, from, to, out);
    }

    JusticeFlow::ResultCode SystemManager::getAuditTableChanges(
        const char *table_name, int record_id,
        std::vector<audit::AuditRecord> &out)
    {
        if (auto rc = guardInitialized("getAuditTableChanges");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getAuditTableChanges(table_name, record_id, out);
    }

    JusticeFlow::ResultCode SystemManager::auditQueryByTimeWindow(
        time_t from, time_t to, std::vector<audit::AuditRecord> &out)
    {
        if (auto rc = guardInitialized("auditQueryByTimeWindow");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->auditQueryByTimeWindow(from, to, out);
    }

    JusticeFlow::ResultCode SystemManager::detectSuspiciousActivity(
        int station_id, std::vector<audit::AuditRecord> &out)
    {
        if (auto rc = guardInitialized("detectSuspiciousActivity");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->detectSuspiciousActivity(station_id, out);
    }

    // ── Warrants ──────────────────────────────────────────────────────────────────

    bool SystemManager::requestWarrant(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, const char *accused_cnic,
        JusticeFlow::WarrantType warrant_type,
        const char *magistrate_name, const char *issuing_court,
        const char *valid_until, const char *target_address,
        int &out_warrant_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("requestWarrant")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s3_->requestWarrant(conn, session, case_id, accused_cnic,
                                   warrant_type, magistrate_name, issuing_court, valid_until,
                                   target_address, out_warrant_id, out_code);
    }

    bool SystemManager::executeWarrant(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int warrant_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("executeWarrant")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s3_->executeWarrant(conn, session, warrant_id, out_code);
    }

    bool SystemManager::cancelWarrant(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int warrant_id, const char *cancellation_reason,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("cancelWarrant")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s3_->cancelWarrant(conn, session, warrant_id,
                                  cancellation_reason, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getWarrantsByCase(
        PGconn *conn, int case_id,
        std::vector<enforcement::WarrantRecord> &out)
    {
        if (auto rc = guardInitialized("getWarrantsByCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getWarrantsByCase(conn, case_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getActiveWarrants(
        PGconn *conn, int station_id,
        std::vector<enforcement::WarrantRecord> &out)
    {
        if (auto rc = guardInitialized("getActiveWarrants");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getActiveWarrants(conn, station_id, out);
    }

    // ── Arrests ───────────────────────────────────────────────────────────────────

    bool SystemManager::recordArrest(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id, const char *accused_cnic,
        const char *arrest_location, int warrant_id,
        int &out_arrest_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("recordArrest")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s3_->recordArrest(conn, session, case_id, accused_cnic,
                                 arrest_location, warrant_id, out_arrest_id, out_code);
    }

    bool SystemManager::updateCustodyStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int arrest_id, JusticeFlow::CustodyStatus new_status,
        const char *reason, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("updateCustodyStatus")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s3_->updateCustodyStatus(conn, session, arrest_id,
                                        new_status, reason, out_code);
    }

    bool SystemManager::markArrestAsDisputed(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int arrest_id, const char *dispute_reason,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("markArrestAsDisputed")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s3_->markArrestAsDisputed(conn, session, arrest_id,
                                         dispute_reason, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getArrestsByCase(
        PGconn *conn, int case_id,
        std::vector<enforcement::ArrestRecord> &out)
    {
        if (auto rc = guardInitialized("getArrestsByCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getArrestsByCase(conn, case_id, out);
    }

    // ── Bail ──────────────────────────────────────────────────────────────────────

    bool SystemManager::recordBail(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int arrest_id, JusticeFlow::BailType bail_type,
        uint64_t bail_amount_paise,
        const char *court_name, const char *magistrate_name,
        const char *valid_until,
        const char *surety_name, const char *surety_cnic,
        const char *surety_contact,
        int &out_bail_id, JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("recordBail")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s3_->recordBail(conn, session, arrest_id, bail_type, bail_amount_paise,
                               court_name, magistrate_name, valid_until,
                               surety_name, surety_cnic, surety_contact,
                               out_bail_id, out_code);
    }

    bool SystemManager::revokeBail(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int bail_id, const char *revocation_reason,
        JusticeFlow::ResultCode &out_code)
    {
        if ((out_code = guardInitialized("revokeBail")) !=
            JusticeFlow::ResultCode::OK)
            return false;
        return s3_->revokeBail(conn, session, bail_id, revocation_reason, out_code);
    }

    JusticeFlow::ResultCode SystemManager::getBailByArrest(
        PGconn *conn, int arrest_id, enforcement::BailRecord &out)
    {
        if (auto rc = guardInitialized("getBailByArrest");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getBailByArrest(conn, arrest_id, out);
    }

    // ── Forensic & Lab ────────────────────────────────────────────────────────────

    JusticeFlow::ResultCode SystemManager::createForensicRequest(
        const char *token, int case_id,
        const char *examination_purpose, const char *purpose_description,
        const char *lab_name, const char *examiner_name,
        int &out_request_id)
    {
        if (auto rc = guardInitialized("createForensicRequest");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->createForensicRequest(token, case_id,
                                          examination_purpose, purpose_description,
                                          lab_name, examiner_name, out_request_id);
    }

    JusticeFlow::ResultCode SystemManager::linkEvidence(
        const char *token, int request_id,
        int evidence_id, const char *notes)
    {
        if (auto rc = guardInitialized("linkEvidence");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->linkEvidence(token, request_id, evidence_id, notes);
    }

    JusticeFlow::ResultCode SystemManager::recordLabReceipt(
        const char *token, int request_id, const char *received_date)
    {
        if (auto rc = guardInitialized("recordLabReceipt");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->recordLabReceipt(token, request_id, received_date);
    }

    JusticeFlow::ResultCode SystemManager::recordExaminationStart(
        const char *token, int request_id)
    {
        if (auto rc = guardInitialized("recordExaminationStart");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->recordExaminationStart(token, request_id);
    }

    JusticeFlow::ResultCode SystemManager::recordFindings(
        const char *token, int request_id,
        const char *findings, const char *report_file_path,
        const char *delivery_date)
    {
        if (auto rc = guardInitialized("recordFindings");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->recordFindings(token, request_id, findings,
                                   report_file_path, delivery_date);
    }

    JusticeFlow::ResultCode SystemManager::recordAmendment(
        const char *token, int request_id, const char *amended_findings)
    {
        if (auto rc = guardInitialized("recordAmendment");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->recordAmendment(token, request_id, amended_findings);
    }

    JusticeFlow::ResultCode SystemManager::contestReport(
        const char *token, int request_id, const char *contest_reason)
    {
        if (auto rc = guardInitialized("contestReport");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->contestReport(token, request_id, contest_reason);
    }

    JusticeFlow::ResultCode SystemManager::getForensicRequestsByCase(
        const char *token, int case_id,
        std::vector<forensic::ForensicRecord> &out)
    {
        if (auto rc = guardInitialized("getForensicRequestsByCase");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getForensicRequestsByCase(token, case_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getPendingForensicRequests(
        const char *token, int station_id,
        std::vector<forensic::ForensicRecord> &out)
    {
        if (auto rc = guardInitialized("getPendingForensicRequests");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getPendingForensicRequests(token, station_id, out);
    }

    JusticeFlow::ResultCode SystemManager::getEvidenceByForensicRequest(
        const char *token, int request_id,
        std::vector<forensic::EvidenceRef> &out)
    {
        if (auto rc = guardInitialized("getEvidenceByForensicRequest");
            rc != JusticeFlow::ResultCode::OK)
            return rc;
        return s3_->getEvidenceByForensicRequest(token, request_id, out);
    }

} // namespace system_layer