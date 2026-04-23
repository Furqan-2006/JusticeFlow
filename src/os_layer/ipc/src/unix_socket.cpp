#include "../include/unix_socket.h"
#include "../../../common/logger.h"

namespace ipc {

UnixSocket::UnixSocket(const std::string& connectionString) : conn(nullptr), conn_string(connectionString) {
    pthread_mutex_init(&db_mutex, nullptr); // Initiialize mutex
}
UnixSocket::~UnixSocket() { 
    disconnect(); 
    pthread_mutex_destroy(&db_mutex); // Destroy Mutex
}

// Changed JFResult to JusticeFlow::ResultCode
JusticeFlow::ResultCode UnixSocket::connect() {
    conn = PQconnectdb(conn_string.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        Logger::error("[IPC] DB Connection Failed");
        disconnect(); 
        return JusticeFlow::ResultCode::DB_ERROR;
    }
    Logger::info("[OS][IPC] Connected to PostgreSQL via Unix Domain Socket.");
    return JusticeFlow::ResultCode::OK;
}

void UnixSocket::disconnect() {
    if (conn != nullptr) {
        PQfinish(conn);
        conn = nullptr;
        Logger::info("[OS][IPC] PostgreSQL connection closed.");
    }
}

bool UnixSocket::isHealthy() const {
    return (conn != nullptr && PQstatus(conn) == CONNECTION_OK);
}

JusticeFlow::ResultCode UnixSocket::execute(const std::string& query, std::vector<std::vector<std::string>>& out_results) {
    if (!isHealthy()) return JusticeFlow::ResultCode::DB_ERROR;

    PGresult* res = PQexec(conn, query.c_str());
    ExecStatusType status = PQresultStatus(res);

    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        Logger::error(PQerrorMessage(conn));
        PQclear(res); // Memory managed internally!
        return JusticeFlow::ResultCode::DB_ERROR;
    }

    int rows = PQntuples(res);
    int cols = PQnfields(res);
    for(int i = 0; i < rows; ++i) {
        std::vector<std::string> row;
        for(int j = 0; j < cols; ++j) {
            row.push_back(PQgetvalue(res, i, j));
        }
        out_results.push_back(row);
    }

    PQclear(res); // Memory managed internally!
    return JusticeFlow::ResultCode::OK;
}

void UnixSocket::lock() {
    pthread_mutex_lock(&db_mutex);
}

void UnixSocket::unlock() {
    pthread_mutex_unlock(&db_mutex);
}

} // namespace ipc
