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
