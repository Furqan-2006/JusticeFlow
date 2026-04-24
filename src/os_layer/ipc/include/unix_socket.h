#ifndef UNIX_SOCKET_H
#define UNIX_SOCKET_H

#include <string>
#include <libpq-fe.h> 
#include "../../../common/constants.h" // Includes JusticeFlow::ResultCode

namespace os_layer {
namespace ipc {

class UnixSocket {
private:
    PGconn* conn; 
    std::string conn_string;

public:
    explicit UnixSocket(const std::string& connectionString);
    ~UnixSocket();

    // Changed JFResult to JusticeFlow::ResultCode
    JusticeFlow::ResultCode connect();
    
    void disconnect();
    bool isConnected() const;
    PGconn* get() const;
};

} // namespace ipc
} // namespace os_layer

#endif // UNIX_SOCKET_H
