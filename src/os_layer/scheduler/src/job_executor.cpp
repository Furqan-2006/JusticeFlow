#include "../include/job_executor.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"

void ExpireWarrantsJob::execute()
{
    Logger::info("Executing ExpireWarrantsJob...");
    
    // CRITICAL FIX #14.1: Now passes TWO arguments (query + results vector)
    std::vector<std::vector<std::string>> results;
    JusticeFlow::ResultCode res = ipc::IpcManager::getInstance().executeQuery(
        "UPDATE warrants SET status='EXPIRED' WHERE expiry_date < NOW()", 
        results
    );
    
    if (res != JusticeFlow::ResultCode::OK)
    {
        Logger::error("ExpireWarrantsJob failed!");
    }
    else
    {
        Logger::info("ExpireWarrantsJob completed successfully");
    }
}

void CheckDatabaseHealthJob::execute()
{
    Logger::debug("Executing CheckDatabaseHealthJob...");
    
    // CRITICAL FIX #14.1: Now passes TWO arguments (query + results vector)
    std::vector<std::vector<std::string>> results;
    JusticeFlow::ResultCode res = ipc::IpcManager::getInstance().executeQuery(
        "SELECT 1", 
        results
    );
    
    if (res != JusticeFlow::ResultCode::OK)
    {
        Logger::error("Database health check failed! DB unreachable.");
    }
    else
    {
        Logger::debug("Database health check passed");
    }
}