#include "../include/command_router.h"

#include <ctime>
#include <pthread.h>
#include <sstream>
#include <vector>

#include "common/logger.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "system/shr_infra/auth/include/auth_manager.h"

namespace api_gateway
{
    namespace
    {
        using ipc::CommandType;
        using ipc::SocketRequest;
        using ipc::SocketResponse;
        using JusticeFlow::ResultCode;

        // Login throttling: after kMaxFailures wrong passwords for one CNIC inside
        // kWindowSeconds, further attempts are refused for kLockoutSeconds.
        constexpr int kMaxFailures = 5;
        constexpr long kWindowSeconds = 300;
        constexpr long kLockoutSeconds = 300;

        // Same answer for "no such CNIC", "wrong password" and "locked out", so a
        // caller learns nothing about which CNICs exist.
        constexpr const char *kBadCredentials = "Invalid credentials";

        std::string jsonEscape(const std::string &s)
        {
            std::string out;
            out.reserve(s.size() + 8);
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += static_cast<char>(c);
                }
            }
            return out;
        }

        std::string q(const std::string &s) { return "\"" + jsonEscape(s) + "\""; }

        // Data reads that must carry a valid token.
        bool isProtectedRead(CommandType c)
        {
            switch (c)
            {
            case CommandType::GET_CASE_LIST:
            case CommandType::GET_CASE_BY_ID:
            case CommandType::GET_HOTSPOT_DATA:
            case CommandType::GET_PRIORITY_CASES:
            case CommandType::GET_OFFICER_WORKLOAD:
                return true;
            default:
                return false;
            }
        }

        // Mirror the live session count into the shared status table so every
        // process that reads it sees the same number.
        void syncSessionCount()
        {
            ipc::SharedStatusTable *table = ipc::IpcManager::getInstance().getStatusTable();
            if (!table)
                return;

            const int count = auth::AuthManager::getInstance().getActiveSessionCount();
            const int rc = pthread_mutex_lock(&table->mutex);
            if (rc == EOWNERDEAD)
                pthread_mutex_consistent(&table->mutex); // previous holder died
            else if (rc != 0)
                return;
            table->active_sessions = count;
            pthread_mutex_unlock(&table->mutex);
        }

        // Name, belt and rank for a session. officer_id is an int from our own
        // session store, never client text, so embedding it is safe.
        std::string officerJson(const JusticeFlow::SessionContext &s)
        {
            std::vector<std::vector<std::string>> rows;
            const std::string sql =
                "SELECT p.full_name, o.belt_number, o.current_rank::text, o.station_id "
                "FROM officers o JOIN persons p ON p.cnic = o.cnic "
                "WHERE o.officer_id = " + std::to_string(s.officerId) + ";";
            ipc::IpcManager::getInstance().executeQuery(sql, rows);

            std::ostringstream oss;
            oss << "\"officer_id\":" << s.officerId;
            if (!rows.empty() && rows[0].size() >= 4)
            {
                oss << ",\"name\":" << q(rows[0][0])
                    << ",\"belt_number\":" << q(rows[0][1])
                    << ",\"rank\":" << q(rows[0][2])
                    << ",\"station_id\":" << q(rows[0][3]);
            }
            oss << ",\"expires_at\":" << static_cast<long>(s.expiresAt);
            return oss.str();
        }

        constexpr size_t kMaxTextBytes = 4000;

        // Turn a failed write into the response the client sees. Business-rule
        // violations raised by our own triggers (P0001) carry a readable message;
        // anything else gets a generic one so schema details do not leak.
        SocketResponse writeFailure(uint32_t id, const ipc::DbError &e)
        {
            if (e.sqlstate == "P0001")
            {
                const bool rank = e.message.find("not authorized") != std::string::npos ||
                                  e.message.find("rank") != std::string::npos;
                return SocketResponse::error(id, rank ? ResultCode::RANK_INSUFFICIENT : ResultCode::INVALID_STATE,
                                             e.message);
            }
            if (e.sqlstate == "23503")
                return SocketResponse::error(id, ResultCode::FOREIGN_KEY_VIOLATION,
                                             "A referenced record does not exist (check the complainant CNIC)");
            if (e.sqlstate == "23505")
                return SocketResponse::error(id, ResultCode::ALREADY_EXISTS, "Record already exists");
            if (e.sqlstate.rfind("22", 0) == 0 || e.sqlstate.rfind("23", 0) == 0)
                return SocketResponse::error(id, ResultCode::INVALID_INPUT, "Invalid input");
            return SocketResponse::error(id, ResultCode::DB_ERROR, "Database error");
        }

        bool tooLong(const std::string &s) { return s.size() > kMaxTextBytes; }
    } // namespace

    // ── throttle ──────────────────────────────────────────────────────────────

    bool CommandRouter::isLockedOut(const std::string &cnic)
    {
        std::lock_guard<std::mutex> lock(attempts_mutex_);
        auto it = attempts_.find(cnic);
        return it != attempts_.end() && it->second.locked_until > ::time(nullptr);
    }

    void CommandRouter::recordFailure(const std::string &cnic)
    {
        std::lock_guard<std::mutex> lock(attempts_mutex_);
        const long now = ::time(nullptr);
        Attempts &a = attempts_[cnic];
        if (now - a.window_start > kWindowSeconds)
        {
            a.failures = 0;
            a.window_start = now;
        }
        if (++a.failures >= kMaxFailures)
        {
            a.locked_until = now + kLockoutSeconds;
            a.failures = 0;
            Logger::error("[Gateway] Too many failed logins; CNIC temporarily locked");
        }
    }

    void CommandRouter::clearFailures(const std::string &cnic)
    {
        std::lock_guard<std::mutex> lock(attempts_mutex_);
        attempts_.erase(cnic);
    }

    // ── commands ──────────────────────────────────────────────────────────────

    SocketResponse CommandRouter::login(const SocketRequest &req)
    {
        const std::string cnic = req.param("cnic");
        const std::string password = req.param("password");

        if (cnic.empty() || password.empty())
            return SocketResponse::error(req.request_id, ResultCode::INVALID_INPUT,
                                         "cnic and password are required");

        if (isLockedOut(cnic))
            return SocketResponse::error(req.request_id, ResultCode::AUTH_FAILED, kBadCredentials);

        JusticeFlow::SessionContext session;
        if (auth::AuthManager::getInstance().login(cnic, password, session) != ResultCode::OK)
        {
            recordFailure(cnic);
            return SocketResponse::error(req.request_id, ResultCode::AUTH_FAILED, kBadCredentials);
        }

        clearFailures(cnic);
        syncSessionCount();

        std::ostringstream oss;
        oss << "{\"token\":" << q(session.sessionToken) << ',' << officerJson(session) << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    SocketResponse CommandRouter::logout(const SocketRequest &req)
    {
        JusticeFlow::SessionContext session;
        if (validator_.validate(req.param("token"), session) != ResultCode::OK)
            return SocketResponse::error(req.request_id, ResultCode::SESSION_EXPIRED, "Not logged in");

        auth::AuthManager::getInstance().logout(session);
        syncSessionCount();
        return SocketResponse::ok(req.request_id, "{}");
    }

    SocketResponse CommandRouter::whoami(const SocketRequest &req)
    {
        JusticeFlow::SessionContext session;
        if (validator_.validate(req.param("token"), session) != ResultCode::OK)
            return SocketResponse::error(req.request_id, ResultCode::SESSION_EXPIRED, "Not logged in");

        return SocketResponse::ok(req.request_id, "{" + officerJson(session) + "}");
    }

    SocketResponse CommandRouter::registerFir(const SocketRequest &req)
    {
        JusticeFlow::SessionContext s;
        const ResultCode rc = validator_.validate(req.param("token"), s);
        if (rc != ResultCode::OK)
            return SocketResponse::error(req.request_id,
                                         rc == ResultCode::SESSION_EXPIRED ? rc : ResultCode::AUTH_FAILED,
                                         "Authentication required");

        // Mirrors the database rule: only ASI, SI and INSPECTOR may file an FIR.
        using JusticeFlow::OfficerRank;
        if (s.rank != OfficerRank::ASI && s.rank != OfficerRank::SI && s.rank != OfficerRank::INSPECTOR)
            return SocketResponse::error(req.request_id, ResultCode::RANK_INSUFFICIENT,
                                         "Only ASI, SI or Inspector rank may register an FIR");

        const std::string cnic = req.param("complainant_cnic");
        const std::string type = req.param("case_type");
        const std::string address = req.param("incident_address");
        const std::string text = req.param("description");
        const std::string when = req.param("incident_date");
        if (cnic.empty() || type.empty() || address.empty() || text.empty() || when.empty())
            return SocketResponse::error(req.request_id, ResultCode::INVALID_INPUT,
                                         "complainant_cnic, case_type, incident_address, description and incident_date are required");
        if (tooLong(address) || tooLong(text))
            return SocketResponse::error(req.request_id, ResultCode::INVALID_INPUT, "Text too long");

        std::vector<std::vector<std::string>> rows;
        ipc::DbError err;
        // The FIR is always filed at the officer's own station, by the officer.
        const auto db = ipc::IpcManager::getInstance().executeAs(
            s.officerId,
            "INSERT INTO cases (fir_number, case_type, incident_date, incident_address, "
            "incident_description, incident_lat, incident_lon, station_id, primary_complainant_cnic, filed_by) "
            "VALUES ('', $1::case_type_enum, $2::timestamptz, $3, $4, NULLIF($5,'')::numeric, "
            "NULLIF($6,'')::numeric, $7::bigint, $8, $9::bigint) "
            "RETURNING case_id, fir_number, case_status::text",
            {type, when, address, text, req.param("lat"), req.param("lon"),
             std::to_string(s.stationId), cnic, std::to_string(s.officerId)},
            rows, &err);
        if (db != ResultCode::OK || rows.empty())
            return writeFailure(req.request_id, err);

        Logger::info("[Gateway] FIR registered");
        std::ostringstream oss;
        oss << "{\"case_id\":" << q(rows[0][0]) << ",\"fir_number\":" << q(rows[0][1])
            << ",\"status\":" << q(rows[0][2]) << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    SocketResponse CommandRouter::addEvidence(const SocketRequest &req)
    {
        JusticeFlow::SessionContext s;
        const ResultCode rc = validator_.validate(req.param("token"), s);
        if (rc != ResultCode::OK)
            return SocketResponse::error(req.request_id,
                                         rc == ResultCode::SESSION_EXPIRED ? rc : ResultCode::AUTH_FAILED,
                                         "Authentication required");

        const std::string case_id = req.param("case_id");
        const std::string type = req.param("evidence_type");
        const std::string text = req.param("description");
        if (case_id.empty() || type.empty() || text.empty())
            return SocketResponse::error(req.request_id, ResultCode::INVALID_INPUT,
                                         "case_id, evidence_type and description are required");
        if (tooLong(text) || tooLong(req.param("collection_location")) || req.param("file_path").size() > 255)
            return SocketResponse::error(req.request_id, ResultCode::INVALID_INPUT, "Text too long");
        if ((type == "DIGITAL" || type == "DOCUMENTARY") && req.param("file_path").empty())
            return SocketResponse::error(req.request_id, ResultCode::INVALID_INPUT,
                                         "file_path is required for DIGITAL and DOCUMENTARY evidence");

        std::vector<std::vector<std::string>> rows;
        ipc::DbError err;
        // Only the case's own station may add evidence, and not once it is closed.
        const auto db = ipc::IpcManager::getInstance().executeAs(
            s.officerId,
            "INSERT INTO evidence (evidence_number, case_id, evidence_type, description, quantity, "
            "file_path, collected_by, collection_location) "
            "SELECT '', c.case_id, $2::evidence_type_enum, $3, COALESCE(NULLIF($4,'')::smallint, 1), "
            "NULLIF($5,''), $6::bigint, NULLIF($7,'') "
            "FROM cases c WHERE c.case_id = $1::bigint AND c.station_id = $8::bigint "
            "AND c.case_status <> 'CLOSED' "
            "RETURNING evidence_id, evidence_number, evidence_type::text",
            {case_id, type, text, req.param("quantity"), req.param("file_path"),
             std::to_string(s.officerId), req.param("collection_location"), std::to_string(s.stationId)},
            rows, &err);
        if (db != ResultCode::OK)
            return writeFailure(req.request_id, err);
        if (rows.empty())
            return SocketResponse::error(req.request_id, ResultCode::NOT_FOUND,
                                         "No open case with that id at your station");

        Logger::info("[Gateway] Evidence added");
        std::ostringstream oss;
        oss << "{\"evidence_id\":" << q(rows[0][0]) << ",\"evidence_number\":" << q(rows[0][1])
            << ",\"evidence_type\":" << q(rows[0][2]) << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    SocketResponse CommandRouter::caseEvidence(const SocketRequest &req)
    {
        JusticeFlow::SessionContext s;
        const ResultCode rc = validator_.validate(req.param("token"), s);
        if (rc != ResultCode::OK)
            return SocketResponse::error(req.request_id,
                                         rc == ResultCode::SESSION_EXPIRED ? rc : ResultCode::AUTH_FAILED,
                                         "Authentication required");
        if (req.param("case_id").empty())
            return SocketResponse::error(req.request_id, ResultCode::INVALID_INPUT, "case_id is required");

        std::vector<std::vector<std::string>> rows;
        ipc::DbError err;
        if (ipc::IpcManager::getInstance().executeAs(
                s.officerId,
                "SELECT e.evidence_number, e.evidence_type::text, e.evidence_status::text, e.description, "
                "e.quantity, COALESCE(e.file_path,''), e.collected_at::text, p.full_name "
                "FROM evidence e JOIN officers o ON o.officer_id = e.collected_by "
                "JOIN persons p ON p.cnic = o.cnic "
                "WHERE e.case_id = $1::bigint AND NOT e.is_deleted ORDER BY e.evidence_id",
                {req.param("case_id")}, rows, &err) != ResultCode::OK)
            return writeFailure(req.request_id, err);

        std::ostringstream oss;
        oss << "{\"evidence\":[";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto &r = rows[i];
            oss << (i ? "," : "") << "{\"evidence_number\":" << q(r[0]) << ",\"evidence_type\":" << q(r[1])
                << ",\"status\":" << q(r[2]) << ",\"description\":" << q(r[3]) << ",\"quantity\":" << q(r[4])
                << ",\"file_path\":" << q(r[5]) << ",\"collected_at\":" << q(r[6])
                << ",\"collected_by\":" << q(r[7]) << '}';
        }
        oss << "]}";
        return SocketResponse::ok(req.request_id, oss.str());
    }

    bool CommandRouter::handle(const SocketRequest &req, SocketResponse &out)
    {
        if (req.command_name == "LOGIN")
        {
            out = login(req);
            return true;
        }
        if (req.command_name == "LOGOUT")
        {
            out = logout(req);
            return true;
        }
        if (req.command_name == "REGISTER_FIR")
        {
            out = registerFir(req);
            return true;
        }
        if (req.command_name == "ADD_EVIDENCE")
        {
            out = addEvidence(req);
            return true;
        }
        if (req.command_name == "GET_CASE_EVIDENCE")
        {
            out = caseEvidence(req);
            return true;
        }
        if (req.command_name == "WHOAMI")
        {
            out = whoami(req);
            return true;
        }

        if (req.command == CommandType::GET_ACTIVE_SESSIONS)
        {
            syncSessionCount(); // expired sessions drop out of the count here
            return false;       // the built-in handler reads the refreshed value
        }

        if (isProtectedRead(req.command))
        {
            JusticeFlow::SessionContext session;
            const ResultCode rc = validator_.validate(req.param("token"), session);
            if (rc != ResultCode::OK)
            {
                out = SocketResponse::error(req.request_id,
                                            (rc == ResultCode::SESSION_EXPIRED) ? rc : ResultCode::AUTH_FAILED,
                                            "Authentication required");
                return true;
            }
        }
        return false;
    }
} // namespace api_gateway
