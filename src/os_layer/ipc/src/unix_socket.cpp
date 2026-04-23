#include "../include/unix_socket.h"
#include "../../../common/logger.h"

namespace ipc {

UnixSocket::UnixSocket(const std::string& connectionString) : conn(nullptr), conn_string(connectionString) {}
UnixSocket::~UnixSocket() { disconnect(); }

JusticeFlow::ResultCode UnixSocket::connect() {
    conn = PQconnectdb(conn_string.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        Logger::error("[IPC] DB Connection Failed");
        disconnect(); 
        return JusticeFlow::ResultCode::DB_ERROR;
    }
    return JusticeFlow::ResultCode::OK;
}

void UnixSocket::disconnect() {
    if (conn != nullptr) {
        PQfinish(conn);
        conn = nullptr;
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

} // namespace ipc
