/**
 * @file subsystem1.cpp
 * @brief Implementation of the Subsystem1 facade.
 *
 * All public methods delegate directly to the appropriate internal manager:
 *   - Case Management  → case_mgmt::CaseManager
 *   - Duty & Patrol    → duty::DutyManager
 *   - Officers         → personnel::OfficerManager
 *
 * Design patterns engaged here:
 *   - Facade    : this file; thin delegation layer.
 *   - Strategy  : CaseManager internally selects a CaseTransitionStrategy.
 *   - Observer  : EvidenceMgr notifies audit/AI observers (inside CaseManager).
 *   - Factory   : generateReport() uses ReportFactory internally.
 */

#include "subsystem1.h"

using namespace case_mgmt; // == JusticeFlow
using namespace duty;      // == JusticeFlow
using namespace personnel; // == JusticeFlow

namespace subsystem1
{

    // =======================
    // Case Management Methods
    // =======================

    bool Subsystem1::registerCase(
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
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::registerCase(
            conn, session, case_type, incident_date,
            incident_address, description, lat, lon,
            station_id, complainant_cnic,
            out_case_id, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getCaseById(
        PGconn *conn,
        int case_id,
        JusticeFlow::Case &out)
    {
        return Subsystem1::getCaseById(conn, case_id, out);
    }

    JusticeFlow::ResultCode Subsystem1::getCasesByStation(
        PGconn *conn,
        int station_id,
        std::vector<JusticeFlow::Case> &out)
    {
        return Subsystem1::getCasesByStation(conn, station_id, out);
    }

    JusticeFlow::ResultCode Subsystem1::getCasesByStatus(
        PGconn *conn,
        int station_id,
        JusticeFlow::CaseStatus status,
        std::vector<JusticeFlow::Case> &out)
    {
        return  Subsystem1::getCasesByStatus(conn, station_id, status, out);
    }

    bool Subsystem1::updateCaseStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        JusticeFlow::CaseStatus new_status,
        const char *reason,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::updateCaseStatus(
            conn, session, case_id, new_status, reason, out_code);
    }

    bool Subsystem1::closeCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        const char *closure_reason,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::closeCase(
            conn, session, case_id, closure_reason, out_code);
    }

    bool Subsystem1::reopenCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        const char *reopen_reason,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::reopenCase(
            conn, session, case_id, reopen_reason, out_code);
    }

    bool Subsystem1::transferCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        int to_station_id,
        const char *transfer_reason,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::transferCase(
            conn, session, case_id, to_station_id, transfer_reason, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getCaseStatusLog(
        PGconn *conn,
        int case_id,
        std::vector<JusticeFlow::CaseStatusLog> &out)
    {
        return Subsystem1::getCaseStatusLog(conn, case_id, out);
    }

    bool Subsystem1::assignOfficerToCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        int officer_id,
        JusticeFlow::CaseOfficerRole role,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::assignOfficerToCase(
            conn, session, case_id, officer_id, role, out_code);
    }

    bool Subsystem1::relieveOfficerFromCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        int officer_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::relieveOfficerFromCase(
            conn, session, case_id, officer_id, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getAssignedOfficers(
        PGconn *conn,
        int case_id,
        std::vector<JusticeFlow::CaseOfficer> &out)
    {
        return Subsystem1::getAssignedOfficers(conn, case_id, out);
    }

    bool Subsystem1::addComplainant(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        const char *person_cnic,
        JusticeFlow::RelationshipToVictim relation,
        bool notify_on_update,
        int &out_complainant_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::addComplainant(
            conn, session, case_id, person_cnic,
            relation, notify_on_update, out_complainant_id, out_code);
    }

    bool Subsystem1::updateComplainantStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int complainant_id,
        JusticeFlow::ComplainantStatus new_status,
        const char *reason,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::updateComplainantStatus(
            conn, session, complainant_id, new_status, reason, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getComplainantsByCase(
        PGconn *conn,
        int case_id,
        std::vector<JusticeFlow::Complainant> &out)
    {
        return Subsystem1::getComplainantsByCase(conn, case_id, out);
    }

    bool Subsystem1::addVictim(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        const char *person_cnic,
        const char *injury_type,
        JusticeFlow::InjurySeverity injury_severity,
        JusticeFlow::VulnerabilityCategory vulnerability,
        const char *medical_report_ref,
        int &out_victim_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::addVictim(
            conn, session, case_id, person_cnic, injury_type, injury_severity,
            vulnerability, medical_report_ref, out_victim_id, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getVictimsByCase(
        PGconn *conn,
        int case_id,
        std::vector<JusticeFlow::Victim> &out)
    {
        return Subsystem1::getVictimsByCase(conn, case_id, out);
    }

    bool Subsystem1::addWitness(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        const char *person_cnic,
        const char *statement_text,
        const char *statement_file_path,
        JusticeFlow::WitnessProtection protection_status,
        bool conceal_identity,
        int &out_witness_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::addWitness(
            conn, session, case_id, person_cnic, statement_text,
            statement_file_path, protection_status, conceal_identity,
            out_witness_id, out_code);
    }

    bool Subsystem1::updateWitnessProtection(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int witness_id,
        JusticeFlow::WitnessProtection new_status,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::updateWitnessProtection(
            conn, session, witness_id, new_status, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getWitnessesByCase(
        PGconn *conn,
        int case_id,
        std::vector<JusticeFlow::Witness> &out)
    {
        return Subsystem1::getWitnessesByCase(conn, case_id, out);
    }

    bool Subsystem1::addAccused(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        const char *person_cnic,
        JusticeFlow::InvolvementType involvement,
        int &out_accused_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::addAccused(
            conn, session, case_id, person_cnic, involvement, out_accused_id, out_code);
    }

    bool Subsystem1::linkAccusedAssociation(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int accused_id,
        int associated_accused_id,
        JusticeFlow::AssociationType association_type,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::linkAccusedAssociation(
            conn, session, accused_id, associated_accused_id, association_type, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getAccusedByCase(
        PGconn *conn,
        int case_id,
        std::vector<JusticeFlow::Accused> &out)
    {
        return Subsystem1::getAccusedByCase(conn, case_id, out);
    }

    bool Subsystem1::linkVehicleToCase(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int case_id,
        int vehicle_id,
        JusticeFlow::VehicleRole role,
        const char *condition_notes,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::linkVehicleToCase(
            conn, session, case_id, vehicle_id, role, condition_notes, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getVehiclesByCase(
        PGconn *conn,
        int case_id,
        std::vector<JusticeFlow::VehicleCase> &out)
    {
        return Subsystem1::getVehiclesByCase(conn, case_id, out);
    }

    // ==========================
    // Duty & Patrol Methods
    // ==========================

    bool Subsystem1::scheduleDuty(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id,
        int station_id,
        int patrol_route_id,
        JusticeFlow::ShiftType shift_type,
        const char *duty_date,
        time_t scheduled_start,
        time_t scheduled_end,
        int &out_duty_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::scheduleDuty(
            conn, session, officer_id, station_id, patrol_route_id,
            shift_type, duty_date, scheduled_start, scheduled_end,
            out_duty_id, out_code);
    }

    bool Subsystem1::markDutyStart(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int duty_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::markDutyStart(conn, session, duty_id, out_code);
    }

    bool Subsystem1::markDutyEnd(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int duty_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::markDutyEnd(conn, session, duty_id, out_code);
    }

    bool Subsystem1::updateDutyStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int duty_id,
        JusticeFlow::DutyStatus new_status,
        const char *absence_reason,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::updateDutyStatus(
            conn, session, duty_id, new_status, absence_reason, out_code);
    }

    bool Subsystem1::cancelDuty(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int duty_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::cancelDuty(conn, session, duty_id, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getDutyRoster(
        PGconn *conn,
        int station_id,
        const char *duty_date,
        std::vector<JusticeFlow::DutyRoster> &out)
    {
        return Subsystem1::getDutyRoster(conn, station_id, duty_date, out);
    }

    JusticeFlow::ResultCode Subsystem1::getActiveDuties(
        PGconn *conn,
        int station_id,
        std::vector<JusticeFlow::DutyRoster> &out)
    {
        return Subsystem1::getActiveDuties(conn, station_id, out);
    }

    JusticeFlow::ResultCode Subsystem1::getOfficerDutyHistory(
        PGconn *conn,
        int officer_id,
        time_t from,
        time_t to,
        std::vector<JusticeFlow::DutyRoster> &out)
    {
        return Subsystem1::getOfficerDutyHistory(conn, officer_id, from, to, out);
    }

    bool Subsystem1::createPatrolRoute(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int station_id,
        const char *beat_code,
        const char *route_name,
        const char *area_description,
        int &out_route_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::createPatrolRoute(
            conn, session, station_id, beat_code, route_name, area_description,
            out_route_id, out_code);
    }

    bool Subsystem1::deactivatePatrolRoute(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int route_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::deactivatePatrolRoute(conn, session, route_id, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getPatrolRoutesByStation(
        PGconn *conn,
        int station_id,
        std::vector<JusticeFlow::PatrolRoute> &out)
    {
        return Subsystem1::getPatrolRoutesByStation(conn, station_id, out);
    }

    // ================
    // Officer Methods
    // ================

    JusticeFlow::ResultCode Subsystem1::getOfficerById(
        PGconn *conn,
        int officer_id,
        JusticeFlow::Officer &out)
    {
        return Subsystem1::getOfficerById(conn, officer_id, out);
    }

    JusticeFlow::ResultCode Subsystem1::getOfficerByCnic(
        PGconn *conn,
        const char *cnic,
        JusticeFlow::Officer &out)
    {
        return Subsystem1::getOfficerByCnic(conn, cnic, out);
    }

    JusticeFlow::ResultCode Subsystem1::getOfficersByStation(
        PGconn *conn,
        int station_id,
        std::vector<JusticeFlow::Officer> &out)
    {
        return Subsystem1::getOfficersByStation(conn, station_id, out);
    }

    JusticeFlow::ResultCode Subsystem1::getOfficersByStatus(
        PGconn *conn,
        int station_id,
        JusticeFlow::OfficerStatus status,
        std::vector<JusticeFlow::Officer> &out)
    {
        return Subsystem1::getOfficersByStatus(conn, station_id, status, out);
    }

    bool Subsystem1::updateOfficerStatus(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id,
        JusticeFlow::OfficerStatus new_status,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::updateOfficerStatus(conn, session, officer_id, new_status, out_code);
    }

    bool Subsystem1::promoteOfficer(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id,
        JusticeFlow::OfficerRank new_rank,
        const char *new_belt_number,
        const char *promotion_type,
        const char *effective_date,
        const char *order_date,
        int &out_history_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::promoteOfficer(
            conn, session, officer_id, new_rank, new_belt_number, promotion_type,
            effective_date, order_date, out_history_id, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getOfficerRankHistory(
        PGconn *conn,
        int officer_id,
        std::vector<JusticeFlow::OfficerRankHistory> &out)
    {
        return Subsystem1::getOfficerRankHistory(conn, officer_id, out);
    }

    bool Subsystem1::deployOfficer(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id,
        int to_station_id,
        const char *deployment_reason,
        const char *order_number,
        const char *deployed_from,
        const char *deployed_until,
        int &out_deployment_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::deployOfficer(
            conn, session, officer_id, to_station_id, deployment_reason,
            order_number, deployed_from, deployed_until, out_deployment_id, out_code);
    }

    bool Subsystem1::endDeployment(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int deployment_id,
        JusticeFlow::ResultCode &out_code)
    {
        return Subsystem1::endDeployment(conn, session, deployment_id, out_code);
    }

    JusticeFlow::ResultCode Subsystem1::getOfficerDeployments(
        PGconn *conn,
        int officer_id,
        bool active_only,
        std::vector<JusticeFlow::OfficerDeployment> &out)
    {
        return Subsystem1::getOfficerDeployments(conn, officer_id, active_only, out);
    }

    // ===================
    // Report Factory Usage
    // ===================
    JusticeFlow::ResultCode Subsystem1::generateOfficerReport(
        PGconn *conn,
        const JusticeFlow::SessionContext &session,
        int officer_id,
        JusticeFlow::ReportType type,
        std::string &out_report_text)
    {
        (void)conn;
        (void)session;
        JusticeFlow::Report *r = JusticeFlow::ReportFactory::create_report(type);
        if (!r)
        {
            out_report_text.clear();
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        r->generate(officer_id);
        out_report_text = "Report generated";
        delete r;
        return JusticeFlow::ResultCode::OK;
    }

} // namespace subsystem1