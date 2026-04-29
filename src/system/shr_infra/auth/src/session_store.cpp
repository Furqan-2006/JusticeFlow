#include "../include/session_store.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"

#include <ctime>

namespace auth
{

    JusticeFlow::ResultCode SessionStore::insert(const SessionContext &session)
    {
        RWLockWriteGuard lock(map_lock);

        // Insert into in-memory map
        session_map[session.token] = session;

        // Write to PostgreSQL sessions table for persistence
        char query[512];
        std::snprintf(query, sizeof(query),
                      "INSERT INTO sessions (token, officer_id, login_timestamp, expires_at, "
                      "last_active_at, is_active) VALUES ('%s', %d, %ld, %ld, %ld, true)",
                      session.token.c_str(), session.officer_id, session.login_timestamp,
                      session.expires_at, session.last_active_at);

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_result =
            ipc::IpcManager::getInstance().executeQuery(query, results);

        if (db_result != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[SessionStore] Failed to persist session to database");
            // Still return OK since in-memory store succeeded
            // DB write is best-effort for audit trail
        }

        char log_buf[256];
        std::snprintf(log_buf, sizeof(log_buf),
                      "[SessionStore] Session created for officer %d, token: %.8s...",
                      session.officer_id, session.token.c_str());
        Logger::info(log_buf);

        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode SessionStore::validate(const std::string &token, SessionContext &out_session)
    {
        RWLockReadGuard lock(map_lock);

        auto it = session_map.find(token);
        if (it == session_map.end())
        {
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        if (isExpired(it->second))
        {
            return JusticeFlow::ResultCode::SESSION_EXPIRED;
        }

        out_session = it->second;
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode SessionStore::refresh(const std::string &token)
    {
        RWLockWriteGuard lock(map_lock);

        auto it = session_map.find(token);
        if (it == session_map.end())
        {
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        // Check hard expiry — cannot extend past it
        long now = time(nullptr);
        if (now >= it->second.expires_at)
        {
            return JusticeFlow::ResultCode::SESSION_EXPIRED;
        }

        // Reset idle timeout but do NOT extend hard expiry
        it->second.last_active_at = now;

        // Update database
        char query[512];
        std::snprintf(query, sizeof(query),
                      "UPDATE sessions SET last_active_at = %ld WHERE token = '%s'",
                      now, token.c_str());

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_result =
            ipc::IpcManager::getInstance().executeQuery(query, results);

        if (db_result != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[SessionStore] Failed to refresh session in database");
        }

        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode SessionStore::remove(const std::string &token)
    {
        RWLockWriteGuard lock(map_lock);

        auto it = session_map.find(token);
        if (it == session_map.end())
        {
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        int officer_id = it->second.officer_id;
        session_map.erase(it);

        // Mark as inactive in database for audit trail
        char query[512];
        std::snprintf(query, sizeof(query),
                      "UPDATE sessions SET is_active = false, logout_timestamp = %ld WHERE token = '%s'",
                      time(nullptr), token.c_str());

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_result =
            ipc::IpcManager::getInstance().executeQuery(query, results);

        if (db_result != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[SessionStore] Failed to mark session as inactive in database");
        }

        char log_buf[256];
        std::snprintf(log_buf, sizeof(log_buf),
                      "[SessionStore] Session removed for officer %d", officer_id);
        Logger::info(log_buf);

        return JusticeFlow::ResultCode::OK;
    }

    int SessionStore::getActiveCount() const
    {
        RWLockReadGuard lock(map_lock);
        return static_cast<int>(session_map.size());
    }

    bool SessionStore::isExpired(const SessionContext &session) const
    {
        long now = time(nullptr);

        // Check hard expiry
        if (now >= session.expires_at)
        {
            return true;
        }

        // Check idle timeout
        if (now - session.last_active_at >= IDLE_TIMEOUT_SECONDS)
        {
            return true;
        }

        return false;
    }

} // namespace auth