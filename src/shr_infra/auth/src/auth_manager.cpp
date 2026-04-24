#include "../include/auth_manager.h"
#include "../../../os_layer/memory/include/mlock_guard.h"
#include "common/logger.h"

#include <ctime>
#include <cstdio>
#include <cstring>

namespace auth
{

    // Forward declarations for private connection management
    // (Not exposed in header)
    static std::unique_ptr<ipc::UnixSocket> createAuthConnection();

    // ============================================================================
    // Implementation
    // ============================================================================

    AuthManager::AuthManager()
        : session_store(std::make_unique<SessionStore>()),
          duty_cache(std::make_unique<DutyCache>()),
          db_connection(createAuthConnection())
    {
        Logger::info("[AuthManager] Initialized");
    }

    AuthManager::~AuthManager()
    {
        Logger::info("[AuthManager] Shutting down");
    }

    AuthManager &AuthManager::getInstance()
    {
        static AuthManager instance;
        return instance;
    }

    JusticeFlow::ResultCode AuthManager::login(const std::string &cnic,
                                               const std::string &password,
                                               SessionContext &out_session)
    {
        // Pin password buffer in memory and zero after use
        mlock_guard pwd_guard(const_cast<char *>(password.c_str()), password.length());

        // Query: SELECT officer_id, rank FROM officers WHERE cnic = ? AND crypt(?, password_hash) = password_hash
        char query[512];
        std::snprintf(query, sizeof(query),
                      "SELECT officer_id, rank FROM officers WHERE cnic = '%s' "
                      "AND crypt('%s', password_hash) = password_hash",
                      cnic.c_str(), password.c_str());

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_result = db_connection->execute(query, results);

        // mlock_guard destructor will zero the password buffer here

        if (db_result != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[AuthManager] Login query failed");
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        if (results.empty() || results[0].empty())
        {
            char log_buf[128];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[AuthManager] Authentication failed for CNIC: %.8s...", cnic.c_str());
            Logger::error(log_buf);
            return JusticeFlow::ResultCode::AUTHENTICATION_FAILED;
        }

        // Extract officer_id and rank
        int officer_id = std::stoi(results[0][0]);
        std::string rank = results[0][1];

        // Generate session token
        std::string token = token_generator::generate();
        if (token.empty())
        {
            Logger::error("[AuthManager] Failed to generate session token");
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        // Create session context
        long now = time(nullptr);
        out_session.token = token;
        out_session.officer_id = officer_id;
        out_session.officer_rank = rank;
        out_session.login_timestamp = now;
        out_session.expires_at = now + (8 * 3600); // 8 hours from now
        out_session.last_active_at = now;
        out_session.is_duty_active = false; // Will be populated on first check

        // Insert into session store
        JusticeFlow::ResultCode insert_result = session_store->insert(out_session);
        if (insert_result != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[AuthManager] Failed to insert session into store");
            return insert_result;
        }

        char log_buf[256];
        std::snprintf(log_buf, sizeof(log_buf),
                      "[AuthManager] Officer %d logged in successfully, token: %.8s...",
                      officer_id, token.c_str());
        Logger::info(log_buf);

        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode AuthManager::validateToken(const std::string &token,
                                                       SessionContext &out_session)
    {
        if (token.empty())
        {
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }

        return session_store->validate(token, out_session);
    }

    JusticeFlow::ResultCode AuthManager::validateRank(const SessionContext &session, int minimum_rank)
    {
        // Map rank string to numeric value
        int rank_value = 0;
        if (session.officer_rank == "Constable")
            rank_value = 0;
        else if (session.officer_rank == "Inspector")
            rank_value = 1;
        else if (session.officer_rank == "Sub-Inspector")
            rank_value = 2;
        else if (session.officer_rank == "DSP")
            rank_value = 3;
        else
        {
            Logger::error("[AuthManager] Unknown rank in session");
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        if (rank_value >= minimum_rank)
        {
            return JusticeFlow::ResultCode::OK;
        }
        else
        {
            char log_buf[128];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[AuthManager] Officer %d rank insufficient: %d < %d",
                          session.officer_id, rank_value, minimum_rank);
            Logger::error(log_buf);
            return JusticeFlow::ResultCode::PERMISSION_DENIED;
        }
    }

    JusticeFlow::ResultCode AuthManager::isDutyActive(int officer_id, bool &out_active)
    {
        return duty_cache->check(officer_id, out_active);
    }

    JusticeFlow::ResultCode AuthManager::refreshSession(SessionContext &session)
    {
        JusticeFlow::ResultCode result = session_store->refresh(session.token);
        if (result == JusticeFlow::ResultCode::OK)
        {
            // Update the session object's last_active_at timestamp
            session.last_active_at = time(nullptr);
        }
        return result;
    }

    JusticeFlow::ResultCode AuthManager::logout(const SessionContext &session)
    {
        JusticeFlow::ResultCode result = session_store->remove(session.token);

        if (result == JusticeFlow::ResultCode::OK)
        {
            char log_buf[128];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[AuthManager] Officer %d logged out", session.officer_id);
            Logger::info(log_buf);
        }

        return result;
    }

    int AuthManager::getActiveSessionCount() const
    {
        return session_store->getActiveCount();
    }

    // ============================================================================
    // Private helper — create dedicated DB connection for auth
    // ============================================================================

    static std::unique_ptr<ipc::UnixSocket> createAuthConnection()
    {
        // Build connection string from environment (same as IpcManager)
        std::string conn_string;

        const char *db_host = std::getenv("JF_DB_HOST");
        const char *db_name = std::getenv("JF_DB_NAME");
        const char *db_user = std::getenv("JF_DB_USER");
        const char *db_pass = std::getenv("JF_DB_PASSWORD");

        char conn_buf[512];
        std::snprintf(conn_buf, sizeof(conn_buf),
                      "hostaddr=%s dbname=%s user=%s password=%s",
                      db_host ? db_host : "/var/run/postgresql",
                      db_name ? db_name : "justiceflow",
                      db_user ? db_user : "justice_app",
                      db_pass ? db_pass : "");

        conn_string = conn_buf;

        auto socket = std::make_unique<ipc::UnixSocket>(conn_string);

        if (socket->connect() != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[AuthManager] Failed to establish dedicated DB connection");
        }

        return socket;
    }

} // namespace auth