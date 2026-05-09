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
        std::string sql = "SELECT officer_id, belt_number, cnic, currentRank, status FROM officers WHERE officer_id=" +
                          std::to_string(officer_id) + ";";
        std::vector<std::vector<std::string>> results;
        auto rc = db().executeQuery(sql, results);

        if (rc != JusticeFlow::ResultCode::OK || results.empty())
            return JusticeFlow::ResultCode::NOT_FOUND;
        out.officerId = std::stoi(results[0][0]);
        out.beltNumber = results[0][1];
        out.cnic = results[0][2];
        // Fill in other fields as needed...
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

} // namespace subsystem1