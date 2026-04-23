#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

#include <string>
#include <vector>
#include <libpq-fe.h>
#include "../../../common/constants.h"

namespace ipc {

class UnixSocket {
private:
    PGconn* conn; 
    std::string conn_string;

public:
    explicit UnixSocket(const std::string& connectionString);
    ~UnixSocket();

    JusticeFlow::ResultCode connect();
    void disconnect();
    bool isHealthy() const; // Renamed from isConnected per TeamLead

    // Executes query, safely cleans up PQresult, passes data by reference
    JusticeFlow::ResultCode execute(const std::string& query, std::vector<std::vector<std::string>>& out_results);
};

} // namespace ipc
#endif
