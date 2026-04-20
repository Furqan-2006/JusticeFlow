#include "../include/ipc_manager.h"
#include "../../../common/logger.h"

// Hardcoded connection string for now (matches our setup script)
const std::string DB_CONN_STR = "dbname=justiceflow user=justiceflow password=justiceflow123 host=localhost port=5432";

// FIFO Paths (From Contract Page 13)
const std::string HOTSPOT_FIFO_PATH = "/tmp/jf_hotspot.fifo";
const std::string PRIORITY_FIFO_PATH = "/tmp/jf_priority.fifo";
const std::string WORKLOAD_FIFO_PATH = "/tmp/jf_workload.fifo";

namespace os_layer {
namespace ipc {

IpcManager::IpcManager() : is_initialized(false) {
    db_socket = std::make_unique<UnixSocket>(DB_CONN_STR);
    hotspot_fifo = std::make_unique<Fifo>(HOTSPOT_FIFO_PATH);
    priority_fifo = std::make_unique<Fifo>(PRIORITY_FIFO_PATH);
    workload_fifo = std::make_unique<Fifo>(WORKLOAD_FIFO_PATH);
}

IpcManager::~IpcManager() {
    shutdownAll();
}

JusticeFlow::ResultCode IpcManager::initializeAll() {
    if (is_initialized) return JusticeFlow::ResultCode::OK;

    Logger::info("[OS][IPC] Initializing all IPC mechanisms...");

    // 1. Connect to PostgreSQL
    JusticeFlow::ResultCode res = db_socket->connect();
    if (res != JusticeFlow::ResultCode::OK) {
        Logger::error("[OS][IPC] Failed to initialize Database Unix Socket.");
        return res;
    }

    // 2. Create FIFOs for AI Agents
    if (hotspot_fifo->create() != JusticeFlow::ResultCode::OK ||
        priority_fifo->create() != JusticeFlow::ResultCode::OK ||
        workload_fifo->create() != JusticeFlow::ResultCode::OK) {
        
        Logger::error("[OS][IPC] Failed to create one or more FIFOs.");
        shutdownAll(); // Clean up if partial failure
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    is_initialized = true;
    Logger::info("[OS][IPC] All IPC mechanisms initialized successfully.");
    return JusticeFlow::ResultCode::OK;
}

void IpcManager::shutdownAll() {
    Logger::info("[OS][IPC] Shutting down all IPC mechanisms...");
    
    if (db_socket) db_socket->disconnect();
    
    // Destroy the FIFOs (unlinks them from the OS)
    if (hotspot_fifo) hotspot_fifo->destroy();
    if (priority_fifo) priority_fifo->destroy();
    if (workload_fifo) workload_fifo->destroy();

    is_initialized = false;
}

UnixSocket* IpcManager::getDatabaseSocket() const {
    return db_socket.get();
}

Fifo* IpcManager::getHotspotFifo() const {
    return hotspot_fifo.get();
}

Fifo* IpcManager::getPriorityFifo() const {
    return priority_fifo.get();
}

Fifo* IpcManager::getWorkloadFifo() const {
    return workload_fifo.get();
}

} // namespace ipc
} // namespace os_layer
