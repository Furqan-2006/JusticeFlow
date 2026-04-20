#include "../include/unix_socket.h"
#include "../../../common/logger.h"

namespace os_layer {
namespace ipc {

UnixSocket::UnixSocket(const std::string& connectionString) 
    : conn(nullptr), conn_string(connectionString) {}

UnixSocket::~UnixSocket() {
    disconnect();
}

// Changed JFResult to JusticeFlow::ResultCode
JusticeFlow::ResultCode UnixSocket::connect() {
    conn = PQconnectdb(conn_string.c_str());

    if (PQstatus(conn) != CONNECTION_OK) {
        std::string err = "[OS][IPC] PostgreSQL Connection Failed: " + std::string(PQerrorMessage(conn));
        Logger::error(err.c_str()); // Pass as C-string
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

bool UnixSocket::isConnected() const {
    return (conn != nullptr && PQstatus(conn) == CONNECTION_OK);
}

PGconn* UnixSocket::get() const {
    return conn;
}

} // namespace ipc
} // namespace os_layer
