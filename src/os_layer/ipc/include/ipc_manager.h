#ifndef IPC_MANAGER_H
#define IPC_MANAGER_H

#include "unix_socket.h"
#include "fifo.h"
#include "../../../common/constants.h"
#include <memory>

namespace ipc {

class IpcManager {
private:
    std::unique_ptr<UnixSocket> db_socket;
    std::unique_ptr<Fifo> hotspot_fifo;
    std::unique_ptr<Fifo> priority_fifo;
    std::unique_ptr<Fifo> workload_fifo;

    bool is_initialized;

public:
    IpcManager();
    ~IpcManager();

    // Initializes all IPC mechanisms (Socket + 3 FIFOs)
    JusticeFlow::ResultCode initializeAll();

    // Shuts down and cleans up all IPC mechanisms
    void shutdownAll();

    // Get the database socket connection
    UnixSocket* getDatabaseSocket() const;

    // Get specific FIFOs for AI Agents
    Fifo* getHotspotFifo() const;
    Fifo* getPriorityFifo() const;
    Fifo* getWorkloadFifo() const;
};

} // namespace ipc

#endif // IPC_MANAGER_H
