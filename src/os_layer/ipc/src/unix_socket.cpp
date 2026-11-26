#include "../include/unix_socket.h"
#include "../../../common/logger.h"
#include <cerrno>

namespace ipc
{

    UnixSocket::UnixSocket(const std::string &connectionString)
        : conn(nullptr), conn_string(connectionString)
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        // Initialize as a regular (non-recursive, non-shared) mutex
        pthread_mutex_init(&db_mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    }

    UnixSocket::~UnixSocket()
    {
        disconnect();
        pthread_mutex_destroy(&db_mutex);
    }

    bool UnixSocket::acquireLock()
    {
        int ret = pthread_mutex_lock(&db_mutex);
        if (ret != 0)
        {
            Logger::error(("[IPC] Failed to acquire DB lock: " + std::string(strerror(ret))).c_str());
            return false;
        }
        return true;
    }

    bool UnixSocket::releaseLock()
    {
        int ret = pthread_mutex_unlock(&db_mutex);
        if (ret != 0)
        {
            Logger::error(("[IPC] Failed to release DB lock: " + std::string(strerror(ret))).c_str());
            return false;
        }
        return true;
    }

    PGconn *UnixSocket::getConnection() const
    {
        // Note: Caller must hold lock to safely access conn
        return conn;
    }

    JusticeFlow::ResultCode UnixSocket::connect()
    {
        conn = PQconnectdb(conn_string.c_str());
        if (PQstatus(conn) != CONNECTION_OK)
        {
            Logger::error(("[IPC] DB Connection Failed: " + std::string(PQerrorMessage(conn))).c_str());
            if (conn != nullptr)
            {
                PQfinish(conn);
                conn = nullptr;
            }
            return JusticeFlow::ResultCode::DB_ERROR;
        }
        Logger::info("[OS][IPC] Connected to PostgreSQL via Unix Domain Socket.");
        return JusticeFlow::ResultCode::OK;
    }

    void UnixSocket::disconnect()
    {
        // Acquire lock to ensure no execute() is in progress
        if (acquireLock())
        {
            if (conn != nullptr)
            {
                PQfinish(conn);
                conn = nullptr;
                Logger::info("[OS][IPC] PostgreSQL connection closed.");
            }
            releaseLock();
        }
        else
        {
            // If lock acquisition fails, still clean up (though this is bad)
            if (conn != nullptr)
            {
                PQfinish(conn);
                conn = nullptr;
                Logger::error("[IPC] Disconnect without lock acquired (possible concurrency issue)");
            }
        }
    }

    bool UnixSocket::isHealthy() const
    {
        // CRITICAL FIX: Acquire lock before reading conn to prevent data race
        // Note: This is a const method but needs to lock a mutable mutex
        pthread_mutex_lock(const_cast<pthread_mutex_t *>(&db_mutex));

        bool healthy = (conn != nullptr && PQstatus(conn) == CONNECTION_OK);

        pthread_mutex_unlock(const_cast<pthread_mutex_t *>(&db_mutex));

        return healthy;
    }

    JusticeFlow::ResultCode UnixSocket::execute(const std::string &query, std::vector<std::vector<std::string>> &out_results)
    {
        // CRITICAL FIX: Acquire lock before using PQexec (not thread-safe)
        if (!acquireLock())
        {
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        // Check connection health while holding lock
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
        {
            releaseLock();
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        // Execute the query
        PGresult *res = PQexec(conn, query.c_str());
        ExecStatusType status = PQresultStatus(res);

        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
        {
            Logger::error(("[IPC] Query failed: " + std::string(PQerrorMessage(conn))).c_str());
            PQclear(res);
            releaseLock();
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        // Extract rows from result
        int rows = PQntuples(res);
        int cols = PQnfields(res);
        out_results.clear();

        for (int i = 0; i < rows; ++i)
        {
            std::vector<std::string> row;
            for (int j = 0; j < cols; ++j)
            {
                const char *value = PQgetvalue(res, i, j);
                row.push_back(value != nullptr ? value : "");
            }
            out_results.push_back(row);
        }

        PQclear(res);
        releaseLock();
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode UnixSocket::executeAs(long officer_id,
                                                  const std::string &sql,
                                                  const std::vector<std::string> &params,
                                                  std::vector<std::vector<std::string>> &out_results,
                                                  DbError *err)
    {
        auto fail = [&](PGresult *res, const char *fallback)
        {
            if (err)
            {
                const char *state = res ? PQresultErrorField(res, PG_DIAG_SQLSTATE) : nullptr;
                const char *msg = res ? PQresultErrorField(res, PG_DIAG_MESSAGE_PRIMARY) : nullptr;
                err->sqlstate = state ? state : "";
                err->message = msg ? msg : fallback;
            }
            if (res)
                PQclear(res);
            // Always leave the connection clean for the next caller.
            PGresult *rb = PQexec(conn, "ROLLBACK");
            PQclear(rb);
            releaseLock();
            return JusticeFlow::ResultCode::DB_ERROR;
        };

        if (!acquireLock())
            return JusticeFlow::ResultCode::INVALID_STATE;

        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
        {
            releaseLock();
            if (err)
                *err = {"08000", "database connection unavailable"};
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        PGresult *res = PQexec(conn, "BEGIN");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            return fail(res, "could not start transaction");
        PQclear(res);

        const std::string who = std::to_string(officer_id);
        const char *who_param[1] = {who.c_str()};
        res = PQexecParams(conn, "SELECT set_config('app.current_officer_id', $1, true)",
                           1, nullptr, who_param, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
            return fail(res, "could not set acting officer");
        PQclear(res);

        std::vector<const char *> values;
        values.reserve(params.size());
        for (const auto &p : params)
            values.push_back(p.c_str());

        res = PQexecParams(conn, sql.c_str(), static_cast<int>(values.size()), nullptr,
                           values.data(), nullptr, nullptr, 0);
        const ExecStatusType st = PQresultStatus(res);
        if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK)
        {
            Logger::error(("[IPC] Write failed: " + std::string(PQerrorMessage(conn))).c_str());
            return fail(res, "statement failed");
        }

        out_results.clear();
        const int rows = PQntuples(res);
        const int cols = PQnfields(res);
        for (int i = 0; i < rows; ++i)
        {
            std::vector<std::string> row;
            for (int j = 0; j < cols; ++j)
                row.push_back(PQgetvalue(res, i, j));
            out_results.push_back(std::move(row));
        }
        PQclear(res);

        res = PQexec(conn, "COMMIT");
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
            return fail(res, "commit failed");
        PQclear(res);

        releaseLock();
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode UnixSocket::lock()
    {
        int ret = pthread_mutex_lock(&db_mutex);
        if (ret != 0)
        {
            Logger::error(("[IPC] Failed to acquire DB lock: " + std::string(strerror(ret))).c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode UnixSocket::unlock()
    {
        int ret = pthread_mutex_unlock(&db_mutex);
        if (ret != 0)
        {
            Logger::error(("[IPC] Failed to release DB lock: " + std::string(strerror(ret))).c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }
        return JusticeFlow::ResultCode::OK;
    }

} // namespace ipc
