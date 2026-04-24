#pragma once

#include <string>
#include <pthread.h> // adding this lib to ensure thread safety
#include <vector>
#include <libpq-fe.h>
#include "../../../common/constants.h"

namespace ipc {

class UnixSocket {
private:
    PGconn* conn; 
    std::string conn_string;
    pthread_mutex_t db_mutex;

public:
    explicit UnixSocket(const std::string& connectionString);
    ~UnixSocket();

    // Changed JFResult to JusticeFlow::ResultCode
    JusticeFlow::ResultCode connect();
    void disconnect();
    bool isHealthy() const; // Renamed from isConnected per TeamLead

    // Executes query, safely cleans up PQresult, passes data by reference
    JusticeFlow::ResultCode execute(const std::string& query, std::vector<std::vector<std::string>>& out_results);

    void lock();
    void unlock();
};

} // namespace ipc
