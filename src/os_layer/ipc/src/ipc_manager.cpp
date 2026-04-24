#include "../include/ipc_manager.h"
#include "../../../common/logger.h"
#include "../../../common/dbconfig.h"
#include "../../../common/ipc_types.h"
#include <vector>

// FIFO paths - these should be configurable, but are defined here for now
// TODO: Move these to a configuration file or environment variables
const std::string HOTSPOT_FIFO_PATH = "/tmp/jf_hotspot.fifo";
const std::string PRIORITY_FIFO_PATH = "/tmp/jf_priority.fifo";
const std::string WORKLOAD_FIFO_PATH = "/tmp/jf_workload.fifo";

namespace ipc
{

    /**
     * CRITICAL SECURITY FIX: Database credentials must never be hardcoded.
     *
     * This function attempts to load credentials from environment variables or
     * a secure configuration file. The development fallback should NEVER be
     * committed to source control with a real password.
     *
     * Environment variables expected:
     * - JUSTICEFLOW_DB_NAME
     * - JUSTICEFLOW_DB_USER
     * - JUSTICEFLOW_DB_PASSWORD
     * - JUSTICEFLOW_DB_HOST
     * - JUSTICEFLOW_DB_PORT
     */
    static std::string getSecureConnectionString()
    {
        JusticeFlow::DBConfig config;

        // Attempt to load from OS Environment Variables
        JusticeFlow::ResultCode res = config.loadFromEnvironment();

        if (res != JusticeFlow::ResultCode::OK)
        {
            Logger::warn("[IPC] Failed to load DB config from environment. Check JUSTICEFLOW_DB_* variables.");

            // DEVELOPMENT ONLY: This fallback should only be used in testing.
            // For production, the deployment process MUST set environment variables.
            // Do NOT use a hardcoded password in production binaries.
            Logger::error("[IPC] *** DATABASE CREDENTIALS NOT CONFIGURED ***");
            Logger::error("[IPC] *** Set JUSTICEFLOW_DB_* environment variables before running ***");

            // Still provide a default to allow testing without full env setup
            config.dbname = "justiceflow";
            config.user = "justice_app";
            config.host = "localhost";
            config.port = 5432;
            // INTENTIONALLY DO NOT SET PASSWORD - will fail unless env var is set
            config.password = "";
        }

        return config.toConnectionString();
    }

    // Private Constructor for Singleton
    IpcManager::IpcManager()
        : db_socket(getSecureConnectionString()),
          shm_handler(SHM_NAME)
    {
        Logger::info("[IPC] IpcManager initialized (Singleton)");
    }

    // ========== Database Interface ==========

    JusticeFlow::ResultCode IpcManager::connectDatabase()
    {
        Logger::info("[IPC] Connecting to database...");
        return db_socket.connect();
    }

    void IpcManager::disconnectDatabase()
    {
        Logger::info("[IPC] Disconnecting from database...");
        db_socket.disconnect();
    }

    JusticeFlow::ResultCode IpcManager::executeQuery(
        const std::string &query,
        std::vector<std::vector<std::string>> &results)
    {
        return db_socket.execute(query, results);
    }

    // ========== Agent Status Interface ==========

    JusticeFlow::ResultCode IpcManager::readAgentStatus(int agent_index, AgentStatusMessage &out_msg)
    {
        if (agent_index < 0 || agent_index > 2)
        {
            Logger::error(("[IPC] Invalid agent index: " + std::to_string(agent_index)).c_str());
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }

        // Determine which FIFO to read based on the agent index
        const std::string *target_fifo = nullptr;
        const char *agent_name = nullptr;

        switch (agent_index)
        {
        case 0:
            target_fifo = &HOTSPOT_FIFO_PATH;
            agent_name = "hotspot";
            break;
        case 1:
            target_fifo = &PRIORITY_FIFO_PATH;
            agent_name = "priority";
            break;
        case 2:
            target_fifo = &WORKLOAD_FIFO_PATH;
            agent_name = "workload";
            break;
        default:
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }

        JusticeFlow::ResultCode ret = fifo_handler.readStatus(*target_fifo, out_msg);

        if (ret != JusticeFlow::ResultCode::OK)
        {
            Logger::debug(("[IPC] No data from " + std::string(agent_name) + " agent FIFO yet").c_str());
        }

        return ret;
    }

    JusticeFlow::ResultCode IpcManager::updateAgentStatus(int agent_index, const AgentStatus &status)
    {
        // Validate input
        if (agent_index < 0 || agent_index > 2)
        {
            Logger::error(("[IPC] Invalid agent index: " + std::to_string(agent_index)).c_str());
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }

        // Get the shared memory table
        SharedStatusTable *table = shm_handler.getTable();
        if (table == nullptr)
        {
            Logger::error("[IPC] Shared memory table not attached");
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        // Acquire the robust, process-shared mutex
        int ret = pthread_mutex_lock(&table->mutex);
        if (ret != 0)
        {
            if (ret == EOWNERDEAD)
            {
                // Previous owner crashed while holding the lock.
                // Make the mutex consistent and continue.
                Logger::warn("[IPC] Detected crashed process holding mutex - recovering");
                pthread_mutex_consistent(&table->mutex);
            }
            else
            {
                Logger::error(("[IPC] Failed to lock shared mutex: " + std::string(strerror(ret))).c_str());
                return JusticeFlow::ResultCode::INVALID_STATE;
            }
        }

        // Copy the status struct into shared memory (struct copy is atomic for small structs)
        table->agents[agent_index] = status;

        // Unlock and check for errors
        ret = pthread_mutex_unlock(&table->mutex);
        if (ret != 0)
        {
            Logger::error(("[IPC] Failed to unlock shared mutex: " + std::string(strerror(ret))).c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        // Optionally signal any waiters (if using condition variables)
        // pthread_cond_broadcast(&table->cond_var);

        return JusticeFlow::ResultCode::OK;
    }

    // ========== Shared Memory Interface ==========

    SharedStatusTable *IpcManager::getStatusTable()
    {
        return shm_handler.getTable();
    }

} // namespace ipc
