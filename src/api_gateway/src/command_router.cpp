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
