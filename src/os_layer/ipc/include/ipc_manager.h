#ifndef IPC_MANAGER_H
#define IPC_MANAGER_H

#include "unix_socket.h"
#include "fifo.h"
#include "shared_memory.h"
#include "../../../common/constants.h"
#include "../../../common/ipc_types.h"

namespace ipc {

class IpcManager {
private:
    UnixSocket db_socket;
    Fifo fifo_handler;
    SharedMemory shm_handler;

    // Private Constructor for Singleton
    IpcManager();

public:
    // Singleton Instance
    static IpcManager& getInstance() {
        static IpcManager instance;
        return instance;
    }

    // Delete copy constructor (Singleton rule)
    IpcManager(const IpcManager&) = delete;
    void operator=(const IpcManager&) = delete;

    // High-Level Interface
    JusticeFlow::ResultCode connectDatabase();
    void disconnectDatabase();
    JusticeFlow::ResultCode executeQuery(const std::string& query, std::vector<std::vector<std::string>>& results);
    
    JusticeFlow::ResultCode readAgentStatus(int agent_index, AgentStatusMessage& out_msg);
    JusticeFlow::ResultCode updateAgentStatus(int agent_index, const AgentStatus& status);
    SharedStatusTable* getStatusTable();
};

} // namespace ipc
#endif