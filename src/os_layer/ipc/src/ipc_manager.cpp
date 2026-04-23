#include "../include/ipc_manager.h"
#include "../../../common/logger.h"
#include "../../../common/dbconfig.h"   // Furqan's secure DB config
#include "../../../common/ipc_types.h"  // For SHM_NAME

#include <vector>

// --- FURQAN FORGOT THESE IN dbconfig.h, SO WE DEFINE THEM HERE FOR NOW ---
const std::string HOTSPOT_FIFO_PATH = "/tmp/jf_hotspot.fifo";
const std::string PRIORITY_FIFO_PATH = "/tmp/jf_priority.fifo";
const std::string WORKLOAD_FIFO_PATH = "/tmp/jf_workload.fifo";
// -------------------------------------------------------------------------

namespace ipc {

// --- SECURITY HELPER ---
// This safely generates the connection string using Furqan's DBConfig
static std::string getSecureConnectionString() {
    JusticeFlow::DBConfig config;
    
    // Attempt to load from OS Environment Variables
    JusticeFlow::ResultCode res = config.loadFromEnvironment();
    
    if (res != JusticeFlow::ResultCode::OK) {
        Logger::error("[IPC] Failed to load secure DB config from environment. Using local dev fallback.");
        // Development fallback (still respects his security rules)
        config.dbname = "justiceflow";
        config.user = "justice_app";
        config.password = "justiceflow123";
    }
    
    return config.toConnectionString();
}

// Private Constructor for Singleton
IpcManager::IpcManager() 
    : db_socket(getSecureConnectionString()), 
    shm_handler(SHM_NAME) {
}

// 1. Database High-Level Interface
JusticeFlow::ResultCode IpcManager::connectDatabase() {
    Logger::info("[IPC] Connecting to database...");
    return db_socket.connect();
}

void IpcManager::disconnectDatabase() {
    db_socket.disconnect();
}

JusticeFlow::ResultCode IpcManager::executeQuery(const std::string& query, std::vector<std::vector<std::string>>& results) {
    return db_socket.execute(query, results);
}

// 2. AI Agents High-Level Interface
JusticeFlow::ResultCode IpcManager::readAgentStatus(int agent_index, AgentStatusMessage& out_msg) {
    // Determine which FIFO to read based on the agent index
    std::string target_fifo;
    switch(agent_index) {
        case 0: target_fifo = HOTSPOT_FIFO_PATH; break;
        case 1: target_fifo = PRIORITY_FIFO_PATH; break;
        case 2: target_fifo = WORKLOAD_FIFO_PATH; break;
        default: return JusticeFlow::ResultCode::INVALID_INPUT;
    }
    
    return fifo_handler.readStatus(target_fifo, out_msg);
}

JusticeFlow::ResultCode IpcManager::updateAgentStatus(int agent_index, const AgentStatus& status) {
    // Get the shared memory table safely
    SharedStatusTable* table = shm_handler.getTable();
    if (table == nullptr) return JusticeFlow::ResultCode::INVALID_STATE;

    if (agent_index < 0 || agent_index > 2) return JusticeFlow::ResultCode::INVALID_INPUT;

    // Lock the robust mutex before writing to shared memory
    pthread_mutex_lock(&table->mutex);
    
    // Copy the status struct into shared memory
    table->agents[agent_index] = status;
    
    pthread_mutex_unlock(&table->mutex);
    
    return JusticeFlow::ResultCode::OK;
}

// 3. Shared Memory High-Level Interface
SharedStatusTable* IpcManager::getStatusTable() {
    return shm_handler.getTable();
}

} // namespace ipc