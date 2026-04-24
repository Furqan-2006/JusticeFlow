#include "../include/ipc_manager.h"
#include "../../../common/logger.h"
#include "../../../common/dbconfig.h"
#include "../../../common/ipc_types.h"

#include <vector>
#include <cstring>
#include <cerrno>

// If dbconfig.h already defines these, remove these defines.
const std::string HOTSPOT_FIFO_PATH = FIFO_PATH_HOTSPOT;
const std::string PRIORITY_FIFO_PATH = FIFO_PATH_PRIORITY;
const std::string WORKLOAD_FIFO_PATH = FIFO_PATH_WORKLOAD;

namespace ipc
{

    static std::string getSecureConnectionString()
    {
        JusticeFlow::DBConfig config;
        JusticeFlow::ResultCode res = config.loadFromEnvironment();
        if (res != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[IPC] Failed to load DB config from environment. Refusing insecure fallback.");
            // No hardcoded password fallback in production
            return "";
        }
        return config.toConnectionString();
    }

    IpcManager::IpcManager()
        : db_socket(getSecureConnectionString()),
          shm_handler(SHM_NAME) {}

    JusticeFlow::ResultCode IpcManager::connectDatabase()
    {
        Logger::info("[IPC] Connecting to database...");
        return db_socket.connect();
    }

    void IpcManager::disconnectDatabase()
    {
        db_socket.disconnect();
    }

    JusticeFlow::ResultCode IpcManager::executeQuery(const std::string &query,
                                                     std::vector<std::vector<std::string>> &results)
    {
        return db_socket.execute(query, results);
    }

    JusticeFlow::ResultCode IpcManager::readAgentStatus(int agent_index, AgentStatusMessage &out_msg)
    {
        std::string target_fifo;
        switch (agent_index)
        {
        case AGENT_INDEX_HOTSPOT:
            target_fifo = HOTSPOT_FIFO_PATH;
            break;
        case AGENT_INDEX_PRIORITY:
            target_fifo = PRIORITY_FIFO_PATH;
            break;
        case AGENT_INDEX_WORKLOAD:
            target_fifo = WORKLOAD_FIFO_PATH;
            break;
        default:
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }
        return fifo_handler.readStatus(target_fifo, out_msg);
    }

    JusticeFlow::ResultCode IpcManager::updateAgentStatus(int agent_index, const AgentStatus &status)
    {
        SharedStatusTable *table = shm_handler.getTable();
        if (table == nullptr)
            return JusticeFlow::ResultCode::INVALID_STATE;

        if (agent_index < 0 || agent_index >= MAX_AGENTS)
            return JusticeFlow::ResultCode::INVALID_INPUT;

        int lock_ret = pthread_mutex_lock(&table->mutex);
        if (lock_ret == EOWNERDEAD)
        {
            // previous writer died while holding lock
            int cons_ret = pthread_mutex_consistent(&table->mutex);
            if (cons_ret != 0)
            {
                Logger::error("[IPC] pthread_mutex_consistent failed; shared state may be corrupted");
                // still proceed to unlock to avoid deadlock
            }
        }
        else if (lock_ret != 0)
        {
            Logger::error("[IPC] pthread_mutex_lock failed");
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        table->agents[agent_index] = status;

        int unlock_ret = pthread_mutex_unlock(&table->mutex);
        if (unlock_ret != 0)
        {
            Logger::error("[IPC] pthread_mutex_unlock failed");
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        return JusticeFlow::ResultCode::OK;
    }

    SharedStatusTable *IpcManager::getStatusTable()
    {
        return shm_handler.getTable();
    }

} // namespace ipc