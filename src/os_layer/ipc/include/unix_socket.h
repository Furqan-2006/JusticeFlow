#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

#include <pthread.h> // adding this library to ensure thread safety
#include <string>
#include <libpq-fe.h> 
#include "../../../common/constants.h" // Includes JusticeFlow::ResultCode

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
    bool isConnected() const;
    PGconn* get() const;

    void lock();
    void unlock();
};

} // namespace ipc

#endif // UNIX_SOCKET_H
