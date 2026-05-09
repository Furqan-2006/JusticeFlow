#pragma once

/**
 * @file subsystem1.h
 * @brief Public API facade for Subsystem 1 (Crime Intelligence & Resource Optimization).
 *
 * This is the only header that the API gateway / router should include for S1.
 *
 * Subsystem 1 responsibilities:
 *   - Duty & Patrol    — schedule and manage officer duty shifts; patrol routes.
 *   - Officers & Personnel — officer profile, rank history, deployments, status.
 *   - Officer report generation.
 *
 * Note: All legal/case/evidence CRUD is handled in Subsystem 2.
 */

#include <vector>
#include <ctime>
#include <string>
#include <postgresql/libpq-fe.h>
#include "common/constants.h"
#include "common/common.h"
#include "include/officer_manager.h"
#include "include/report_factory.h"

namespace subsystem1
{

    class Subsystem1
    {
    public:
        // ======================= Duty & Patrol ========================

        static bool scheduleDuty(
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
            JusticeFlow::ResultCode &out_code);

        static bool markDutyStart(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        static bool markDutyEnd(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        static bool updateDutyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::DutyStatus new_status,
            const char *absence_reason,
            JusticeFlow::ResultCode &out_code);

        static bool cancelDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getDutyRoster(
            PGconn *conn,
            int station_id,
            const char *duty_date,
            std::vector<JusticeFlow::DutyRoster> &out);

        static JusticeFlow::ResultCode getActiveDuties(
            PGconn *conn,
            int station_id,
            std::vector<JusticeFlow::DutyRoster> &out);

        static JusticeFlow::ResultCode getOfficerDutyHistory(
            PGconn *conn,
            int officer_id,
            time_t from,
            time_t to,
            std::vector<JusticeFlow::DutyRoster> &out);

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

        // ================= Officers & Personnel =======================

        static JusticeFlow::ResultCode getOfficerById(
            PGconn *conn,
            int officer_id,
            JusticeFlow::Officer &out);

        static JusticeFlow::ResultCode getOfficerByCnic(
            PGconn *conn,
            const char *cnic,
            JusticeFlow::Officer &out);

        static JusticeFlow::ResultCode getOfficersByStation(
            PGconn *conn,
            int station_id,
            std::vector<JusticeFlow::Officer> &out);

        static JusticeFlow::ResultCode getOfficersByStatus(
            PGconn *conn,
            int station_id,
            JusticeFlow::OfficerStatus status,
            std::vector<JusticeFlow::Officer> &out);

        static bool updateOfficerStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerStatus new_status,
            JusticeFlow::ResultCode &out_code);

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

        static JusticeFlow::ResultCode getOfficerRankHistory(
            PGconn *conn,
            int officer_id,
            std::vector<JusticeFlow::OfficerRankHistory> &out);

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

        static bool endDeployment(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int deployment_id,
            JusticeFlow::ResultCode &out_code);

        static JusticeFlow::ResultCode getOfficerDeployments(
            PGconn *conn,
            int officer_id,
            bool active_only,
            std::vector<JusticeFlow::OfficerDeployment> &out);

        // ================ Officer Reports (Factory) =====================

        static JusticeFlow::ResultCode generateOfficerReport(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::ReportType type,
            std::string &out_report_text);
    };

} // namespace subsystem1