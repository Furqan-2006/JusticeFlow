#include "subsystem1.h"
#include "../../../os_layer/ipc/include/ipc_manager.h"
#include "../../common/logger.h"
#include <string>
#include <vector>

namespace subsystem1
{

    static ipc::IpcManager &db()
    {
        return ipc::IpcManager::getInstance();
    }

    // ====================== Duty & Patrol =======================

    bool Subsystem1::scheduleDuty(
        PGconn *,
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
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }

        std::string sql = "INSERT INTO duty_roster (officer_id, station_id, patrol_route_id, shift_type, duty_date, scheduled_start, scheduled_end, status) VALUES (" + std::to_string(officer_id) + ", " + std::to_string(station_id) + ", " + std::to_string(patrol_route_id) + ", '" + std::to_string(static_cast<int>(shift_type)) + "', '" + std::string(duty_date) + "', "
                                                                                                                                                                                                                                                                                                                                                                           "to_timestamp(" +
                          std::to_string(scheduled_start) + "), "
                                                            "to_timestamp(" +
                          std::to_string(scheduled_end) + "), "
                                                          "'SCHEDULED') RETURNING duty_id;";

        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);

        if (rc != JusticeFlow::ResultCode::OK || results.empty())
        {
            Logger::error("[S1][scheduleDuty] Insert failed.");
            out_code = rc;
            return false;
        }
        out_duty_id = std::stoi(results[0][0]);
        out_code = JusticeFlow::ResultCode::OK;
        return true;
    }

    // -- The rest of the methods follow a similar pattern --

    bool Subsystem1::markDutyStart(PGconn *, const JusticeFlow::SessionContext &session, int duty_id, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "UPDATE duty_roster SET status='ON_DUTY', actual_start=now() WHERE duty_id=" + std::to_string(duty_id) + " AND status='SCHEDULED';";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        out_code = rc;
        return rc == JusticeFlow::ResultCode::OK;
    }

    bool Subsystem1::markDutyEnd(PGconn *, const JusticeFlow::SessionContext &session, int duty_id, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "UPDATE duty_roster SET status='COMPLETED', actual_end=now() WHERE duty_id=" + std::to_string(duty_id) + " AND status='ON_DUTY';";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        out_code = rc;
        return rc == JusticeFlow::ResultCode::OK;
    }

    bool Subsystem1::updateDutyStatus(PGconn *, const JusticeFlow::SessionContext &session, int duty_id,
                                      JusticeFlow::DutyStatus new_status, const char *absence_reason, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string status_str = std::to_string(static_cast<int>(new_status));
        std::string reason_str = absence_reason ? std::string(absence_reason) : "";
        std::string sql = "UPDATE duty_roster SET status='" + status_str + "', absence_reason='" + reason_str +
                          "' WHERE duty_id=" + std::to_string(duty_id) + ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        out_code = rc;
        return rc == JusticeFlow::ResultCode::OK;
    }

    bool Subsystem1::cancelDuty(PGconn *, const JusticeFlow::SessionContext &session, int duty_id, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "DELETE FROM duty_roster WHERE duty_id=" + std::to_string(duty_id) + " AND status='SCHEDULED';";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        out_code = rc;
        return rc == JusticeFlow::ResultCode::OK;
    }

    // --- Query-type methods: these fill `.out` using the database results as needed ---

    JusticeFlow::ResultCode Subsystem1::getDutyRoster(PGconn *, int station_id, const char *duty_date, std::vector<JusticeFlow::DutyRoster> &out)
    {
        std::string sql = "SELECT duty_id, officer_id, status, duty_date FROM duty_roster WHERE station_id=" + std::to_string(station_id);
        if (duty_date)
            sql += " AND duty_date='" + std::string(duty_date) + "'";
        sql += ";";

        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);

        if (rc != JusticeFlow::ResultCode::OK)
            return rc;

        // Convert results to DutyRoster struct as needed (basic example):
        for (const auto &row : results)
        {
            if (row.size() < 4)
                continue;
            JusticeFlow::DutyRoster entry{};
            entry.duty_id = std::stoi(row[0]);
            entry.officer_id = std::stoi(row[1]);
            // You would map status and date here...
            out.push_back(entry);
        }
        return rc;
    }

    // ... Repeat this pattern for other query methods ...

    // ================= Officers & Personnel =================

    JusticeFlow::ResultCode Subsystem1::getOfficerById(PGconn *, int officer_id, JusticeFlow::Officer &out)
    {
        std::string sql = "SELECT officer_id, belt_number, cnic, current_rank, status FROM officers WHERE officer_id=" +
                          std::to_string(officer_id) + ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);

        if (rc != JusticeFlow::ResultCode::OK || results.empty())
            return JusticeFlow::ResultCode::NOT_FOUND;
        out.officerId = std::stoi(results[0][0]);
        out.beltNumber = results[0][1];
        out.cnic = results[0][2];
        out.currentRank = JusticeFlow::OfficerRank(std::stoi(results[0][3]));
        out.status = JusticeFlow::OfficerStatus(std::stoi(results[0][4]));
        return rc;
    }

    // ... Repeat for getOfficerByCnic, getOfficersByStation, etc. ...

    // ================= Officer Reports (Factory Pattern) ================

    JusticeFlow::ResultCode Subsystem1::generateOfficerReport(
        PGconn *, const JusticeFlow::SessionContext &, int officer_id, JusticeFlow::ReportType type, std::string &out_report_text)
    {
        JusticeFlow::Report *report = JusticeFlow::ReportFactory::create_report(type);
        if (!report)
        {
            out_report_text.clear();
            return JusticeFlow::ResultCode::DB_ERROR;
        }
        report->generate(officer_id);
        out_report_text = "Report generated (see logs for details)";
        delete report;
        return JusticeFlow::ResultCode::OK;
    }

    // ====================== Active Duties & History =======================

    JusticeFlow::ResultCode Subsystem1::getActiveDuties(PGconn *, int station_id, std::vector<JusticeFlow::DutyRoster> &out)
    {
        std::string sql = "SELECT duty_id, officer_id, status, duty_date FROM duty_roster WHERE station_id=" +
                          std::to_string(station_id) + " AND status='ON_DUTY';";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK)
            return rc;
        for (const auto &row : results)
        {
            if (row.size() < 4)
                continue;
            JusticeFlow::DutyRoster entry{};
            entry.duty_id = std::stoi(row[0]);
            entry.officer_id = std::stoi(row[1]);
            out.push_back(entry);
        }
        return rc;
    }

    JusticeFlow::ResultCode Subsystem1::getOfficerDutyHistory(PGconn *, int officer_id, time_t from, time_t to, std::vector<JusticeFlow::DutyRoster> &out)
    {
        std::string sql = "SELECT duty_id, officer_id, status, duty_date FROM duty_roster WHERE officer_id=" +
                          std::to_string(officer_id) +
                          " AND scheduled_start >= to_timestamp(" + std::to_string(from) + ")" +
                          " AND scheduled_end <= to_timestamp(" + std::to_string(to) + ");";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK)
            return rc;
        for (const auto &row : results)
        {
            if (row.size() < 4)
                continue;
            JusticeFlow::DutyRoster entry{};
            entry.duty_id = std::stoi(row[0]);
            entry.officer_id = std::stoi(row[1]);
            out.push_back(entry);
        }
        return rc;
    }

    // ====================== Patrol Routes =================================

    bool Subsystem1::createPatrolRoute(PGconn *, const JusticeFlow::SessionContext &session,
                                       int station_id, const char *beat_code, const char *route_name,
                                       const char *area_description, int &out_route_id, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "INSERT INTO patrol_routes (station_id, beat_code, route_name, area_description, active) VALUES (" +
                          std::to_string(station_id) + ", '" + std::string(beat_code) + "', '" +
                          std::string(route_name) + "', '" + std::string(area_description) +
                          "', TRUE) RETURNING route_id;";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK || results.empty())
        {
            out_code = rc;
            return false;
        }
        out_route_id = std::stoi(results[0][0]);
        out_code = JusticeFlow::ResultCode::OK;
        return true;
    }

    bool Subsystem1::deactivatePatrolRoute(PGconn *, const JusticeFlow::SessionContext &session,
                                           int route_id, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "UPDATE patrol_routes SET active=FALSE WHERE route_id=" + std::to_string(route_id) + ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        out_code = rc;
        return rc == JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode Subsystem1::getPatrolRoutesByStation(PGconn *, int station_id, std::vector<JusticeFlow::PatrolRoute> &out)
    {
        std::string sql = "SELECT route_id, station_id, beat_code, route_name FROM patrol_routes WHERE station_id=" +
                          std::to_string(station_id) + " AND active=TRUE;";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK)
            return rc;
        for (const auto &row : results)
        {
            if (row.size() < 4)
                continue;
            JusticeFlow::PatrolRoute entry{};
            entry.route_id = std::stoi(row[0]);
            entry.station_id = std::stoi(row[1]);
            entry.beat_code = row[2];
            entry.route_name = row[3];
            out.push_back(entry);
        }
        return rc;
    }

    // ====================== Officers & Personnel ==========================

    JusticeFlow::ResultCode Subsystem1::getOfficerByCnic(PGconn *, const char *cnic, JusticeFlow::Officer &out)
    {
        std::string sql = "SELECT officer_id, belt_number, cnic, current_rank, status FROM officers WHERE cnic='" +
                          std::string(cnic) + "';";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK || results.empty())
            return JusticeFlow::ResultCode::NOT_FOUND;
        out.officerId = std::stoi(results[0][0]);
        out.beltNumber = results[0][1];
        out.cnic = results[0][2];
        out.currentRank = JusticeFlow::OfficerRank(std::stoi(results[0][3]));
        out.status = JusticeFlow::OfficerStatus(std::stoi(results[0][4]));
        return rc;
    }

    JusticeFlow::ResultCode Subsystem1::getOfficersByStation(PGconn *, int station_id, std::vector<JusticeFlow::Officer> &out)
    {
        std::string sql = "SELECT officer_id, belt_number, cnic, current_rank, status FROM officers WHERE station_id=" +
                          std::to_string(station_id) + ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK)
            return rc;
        for (const auto &row : results)
        {
            if (row.size() < 5)
                continue;
            JusticeFlow::Officer o{};
            o.officerId = std::stoi(row[0]);
            o.beltNumber = row[1];
            o.cnic = row[2];
            o.currentRank = JusticeFlow::OfficerRank(std::stoi(row[3]));
            o.status = JusticeFlow::OfficerStatus(std::stoi(row[4]));
            out.push_back(o);
        }
        return rc;
    }

    JusticeFlow::ResultCode Subsystem1::getOfficersByStatus(PGconn *, int station_id,
                                                            JusticeFlow::OfficerStatus status, std::vector<JusticeFlow::Officer> &out)
    {
        std::string sql = "SELECT officer_id, belt_number, cnic, current_rank, status FROM officers WHERE station_id=" +
                          std::to_string(station_id) + " AND status=" +
                          std::to_string(static_cast<int>(status)) + ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK)
            return rc;
        for (const auto &row : results)
        {
            if (row.size() < 5)
                continue;
            JusticeFlow::Officer o{};
            o.officerId = std::stoi(row[0]);
            o.beltNumber = row[1];
            o.cnic = row[2];
            o.currentRank = JusticeFlow::OfficerRank(std::stoi(row[3]));
            o.status = JusticeFlow::OfficerStatus(std::stoi(row[4]));       
            out.push_back(o);
        }
        return rc;
    }

    bool Subsystem1::updateOfficerStatus(PGconn *, const JusticeFlow::SessionContext &session,
                                         int officer_id, JusticeFlow::OfficerStatus new_status, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "UPDATE officers SET status=" + std::to_string(static_cast<int>(new_status)) +
                          " WHERE officer_id=" + std::to_string(officer_id) + ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        out_code = rc;
        return rc == JusticeFlow::ResultCode::OK;
    }

    bool Subsystem1::promoteOfficer(PGconn *, const JusticeFlow::SessionContext &session,
                                    int officer_id, JusticeFlow::OfficerRank new_rank,
                                    const char *new_belt_number, const char *promotion_type,
                                    const char *effective_date, const char *order_date,
                                    int &out_history_id, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "INSERT INTO officer_rank_history (officer_id, new_rank, belt_number, promotion_type, effective_date, order_date) VALUES (" +
                          std::to_string(officer_id) + ", " + std::to_string(static_cast<int>(new_rank)) +
                          ", '" + std::string(new_belt_number) + "', '" + std::string(promotion_type) +
                          "', '" + std::string(effective_date) + "', '" + std::string(order_date) +
                          "') RETURNING history_id;";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK || results.empty())
        {
            out_code = rc;
            return false;
        }
        out_history_id = std::stoi(results[0][0]);
        out_code = JusticeFlow::ResultCode::OK;
        return true;
    }

    JusticeFlow::ResultCode Subsystem1::getOfficerRankHistory(PGconn *, int officer_id, std::vector<JusticeFlow::OfficerRankHistory> &out)
    {
        std::string sql = "SELECT history_id, officer_id, new_rank, effective_date FROM officer_rank_history WHERE officer_id=" +
                          std::to_string(officer_id) + " ORDER BY effective_date DESC;";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK)
            return rc;
        for (const auto &row : results)
        {
            if (row.size() < 4)
                continue;
            JusticeFlow::OfficerRankHistory entry{};
            entry.history_id = std::stoi(row[0]);
            entry.officer_id = std::stoi(row[1]);
            out.push_back(entry);
        }
        return rc;
    }

    // ====================== Deployments ===================================

    bool Subsystem1::deployOfficer(PGconn *, const JusticeFlow::SessionContext &session,
                                   int officer_id, int to_station_id, const char *deployment_reason,
                                   const char *order_number, const char *deployed_from,
                                   const char *deployed_until, int &out_deployment_id, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "INSERT INTO officer_deployments (officer_id, station_id, reason, order_number, deployed_from, deployed_until, active) VALUES (" +
                          std::to_string(officer_id) + ", " + std::to_string(to_station_id) +
                          ", '" + std::string(deployment_reason) + "', '" + std::string(order_number) +
                          "', '" + std::string(deployed_from) + "', '" + std::string(deployed_until) +
                          "', TRUE) RETURNING deployment_id;";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK || results.empty())
        {
            out_code = rc;
            return false;
        }
        out_deployment_id = std::stoi(results[0][0]);
        out_code = JusticeFlow::ResultCode::OK;
        return true;
    }

    bool Subsystem1::endDeployment(PGconn *, const JusticeFlow::SessionContext &session,
                                   int deployment_id, JusticeFlow::ResultCode &out_code)
    {
        if (!session.isValid)
        {
            out_code = JusticeFlow::ResultCode::AUTH_FAILED;
            return false;
        }
        std::string sql = "UPDATE officer_deployments SET active=FALSE WHERE deployment_id=" +
                          std::to_string(deployment_id) + ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        out_code = rc;
        return rc == JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode Subsystem1::getOfficerDeployments(PGconn *, int officer_id,
                                                              bool active_only, std::vector<JusticeFlow::OfficerDeployment> &out)
    {
        std::string sql = "SELECT deployment_id, officer_id, station_id FROM officer_deployments WHERE officer_id=" +
                          std::to_string(officer_id);
        if (active_only)
            sql += " AND active=TRUE";
        sql += ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);
        if (rc != JusticeFlow::ResultCode::OK)
            return rc;
        for (const auto &row : results)
        {
            if (row.size() < 3)
                continue;
            JusticeFlow::OfficerDeployment entry{};
            entry.deployment_id = std::stoi(row[0]);
            entry.officer_id = std::stoi(row[1]);
            entry.from_station_id = std::stoi(row[2]);
            out.push_back(entry);
        }
        return rc;
    }

} // namespace subsystem1