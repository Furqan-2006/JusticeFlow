/**
 * @file audit_manager.cpp
 * @brief AuditManager singleton implementation.
 *
 * Owns the dedicated read-only PGconn* for all audit queries.
 * Delegates every query to the stateless AuditQuery class.
 *
 * Connection Lifecycle
 * --------------------
 * connect()    — called once during process startup. Stores conninfo for
 *                reconnect attempts. Sets connection options for read-only
 *                access (default_transaction_read_only = on).
 *
 * disconnect() — called during clean shutdown. Idempotent.
 *
 * _reconnect() — called automatically when a query detects a broken
 *                connection (PQstatus != CONNECTION_OK). Attempts one
 *                synchronous reconnect. If it fails, DB_ERROR is returned
 *                to the caller — no retry loop here; the OS-layer health
 *                monitor handles persistent connection failures.
 *
 * Read-only guarantee
 * -------------------
 * After connect(), the session runs:
 *   SET default_transaction_read_only = on;
 * This prevents any accidental write even if a future code path calls
 * PQexec with a mutating statement. The session will reject it at the DB.
 */

#include "../include/audit_manager.h"
#include "../../../common/logger.h"

#include <cstring>
#include <cstdio>

using namespace JusticeFlow;

namespace audit
{

    // -----------------------------------------------------------------------
    // Singleton
    // -----------------------------------------------------------------------

    AuditManager &AuditManager::getInstance()
    {
        // C++11 guarantees this initialisation is thread-safe and executes
        // exactly once across all threads.
        static AuditManager instance;
        return instance;
    }

    AuditManager::AuditManager()
        : conn_(nullptr)
    {
        conninfo_[0] = '\0';
    }

    AuditManager::~AuditManager()
    {
        disconnect();
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    ResultCode AuditManager::connect(const char *conninfo)
    {
        if (conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK)
        {
            // Already connected — idempotent
            return ResultCode::OK;
        }

        if (conninfo == nullptr || conninfo[0] == '\0')
        {
            Logger::error("audit_manager: Empty conninfo");
            return ResultCode::DB_ERROR;
        }

        // Store conninfo for future reconnect attempts
        std::strncpy(conninfo_, conninfo, sizeof(conninfo_) - 1);
        conninfo_[sizeof(conninfo_) - 1] = '\0';

        conn_ = PQconnectdb(conninfo_);

        if (PQstatus(conn_) != CONNECTION_OK)
        {
            Logger::error("audit_manager: PQconnectdb failed");
            Logger::error(PQerrorMessage(conn_));
            PQfinish(conn_);
            conn_ = nullptr;
            return ResultCode::DB_ERROR;
        }

        // Enforce read-only session — belt-and-suspenders on top of DB role permissions
        PGresult *res = PQexec(conn_, "SET default_transaction_read_only = on;");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("audit_manager: Failed to set read-only transaction mode");
            PQclear(res);
            PQfinish(conn_);
            conn_ = nullptr;
            return ResultCode::DB_ERROR;
        }
        PQclear(res);

        Logger::info("audit_manager: Connected (read-only session established)");
        return ResultCode::OK;
    }

    void AuditManager::disconnect()
    {
        if (conn_ != nullptr)
        {
            PQfinish(conn_);
            conn_ = nullptr;
            Logger::info("audit_manager: Disconnected");
        }
    }

    bool AuditManager::_reconnect()
    {
        if (conninfo_[0] == '\0')
        {
            Logger::error("audit_manager: Cannot reconnect — no conninfo stored");
            return false;
        }

        Logger::info("audit_manager: Attempting reconnect…");

        if (conn_ != nullptr)
        {
            PQfinish(conn_);
            conn_ = nullptr;
        }

        conn_ = PQconnectdb(conninfo_);

        if (PQstatus(conn_) != CONNECTION_OK)
        {
            Logger::error("audit_manager: Reconnect failed");
            Logger::error(PQerrorMessage(conn_));
            PQfinish(conn_);
            conn_ = nullptr;
            return false;
        }

        // Re-apply read-only mode on the new connection
        PGresult *res = PQexec(conn_, "SET default_transaction_read_only = on;");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            Logger::error("audit_manager: Reconnect succeeded but failed to set read-only mode");
            PQclear(res);
            PQfinish(conn_);
            conn_ = nullptr;
            return false;
        }
        PQclear(res);

        Logger::info("audit_manager: Reconnect succeeded");
        return true;
    }

    // -----------------------------------------------------------------------
    // Internal helper — validates connection, attempts one reconnect if broken
    // -----------------------------------------------------------------------

    /**
     * Ensures conn_ is ready for use. Returns DB_ERROR if connection cannot
     * be established. Called at the top of every public query method.
     */
    static ResultCode ensureConnected(PGconn *&conn,
                                      bool (AuditManager::*reconnect_fn)(),
                                      AuditManager *self)
    {
        if (conn == nullptr)
        {
            Logger::error("audit_manager: Not connected — call connect() first");
            return ResultCode::DB_ERROR;
        }

        if (PQstatus(conn) != CONNECTION_OK)
        {
            Logger::debug("audit_manager: Connection lost — attempting reconnect");
            if (!(self->*reconnect_fn)())
            {
                return ResultCode::DB_ERROR;
            }
        }

        return ResultCode::OK;
    }

    // -----------------------------------------------------------------------
    // Query methods — each validates connection then delegates to AuditQuery
    // -----------------------------------------------------------------------

    ResultCode AuditManager::getChangeHistory(int case_id,
                                              std::vector<AuditRecord> &out)
    {
        ResultCode conn_check = ensureConnected(conn_, &AuditManager::_reconnect, this);
        if (conn_check != ResultCode::OK)
            return conn_check;

        return AuditQuery::getChangeHistory(conn_, case_id, out);
    }

    ResultCode AuditManager::getOfficerActions(int officer_id,
                                               time_t from,
                                               time_t to,
                                               std::vector<AuditRecord> &out)
    {
        ResultCode conn_check = ensureConnected(conn_, &AuditManager::_reconnect, this);
        if (conn_check != ResultCode::OK)
            return conn_check;

        return AuditQuery::getOfficerActions(conn_, officer_id, from, to, out);
    }

    ResultCode AuditManager::getTableChanges(const char *table_name,
                                             int record_id,
                                             std::vector<AuditRecord> &out)
    {
        ResultCode conn_check = ensureConnected(conn_, &AuditManager::_reconnect, this);
        if (conn_check != ResultCode::OK)
            return conn_check;

        return AuditQuery::getTableChanges(conn_, table_name, record_id, out);
    }

    ResultCode AuditManager::queryByTimeWindow(time_t from,
                                               time_t to,
                                               std::vector<AuditRecord> &out)
    {
        ResultCode conn_check = ensureConnected(conn_, &AuditManager::_reconnect, this);
        if (conn_check != ResultCode::OK)
            return conn_check;

        return AuditQuery::queryByTimeWindow(conn_, from, to, out);
    }

    ResultCode AuditManager::detectSuspiciousActivity(int station_id,
                                                      std::vector<AuditRecord> &out)
    {
        ResultCode conn_check = ensureConnected(conn_, &AuditManager::_reconnect, this);
        if (conn_check != ResultCode::OK)
            return conn_check;

        return AuditQuery::detectSuspiciousActivity(conn_, station_id, out);
    }

} // namespace audit
