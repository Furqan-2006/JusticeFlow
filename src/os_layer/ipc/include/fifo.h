#ifndef FIFO_H
#define FIFO_H

#include <string>
#include "../../../common/constants.h"
#include "../../../common/ipc_types.h" // Furqan's message struct

namespace ipc {

class Fifo {
public:
    Fifo() = default;
    ~Fifo() = default;

    JusticeFlow::ResultCode create(const std::string& path);
    JusticeFlow::ResultCode readStatus(const std::string& path, AgentStatusMessage& out_msg);
    void destroy(const std::string& path);
};

} // namespace ipc
#endif
