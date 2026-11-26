#include "../include/job_executor.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"

void ExpireWarrantsJob::execute()
{
    // Warrants carry a valid_until date; once it has passed, an ISSUED warrant
    // is EXPIRED. Executed and cancelled warrants are left as they are.
    std::vector<std::vector<std::string>> results;
    JusticeFlow::ResultCode res = ipc::IpcManager::getInstance().executeQuery(
        "UPDATE warrants SET warrant_status = 'EXPIRED', updated_at = NOW() "
        "WHERE warrant_status = 'ISSUED' AND valid_until < CURRENT_DATE "
        "RETURNING warrant_number",
        results);

    if (res != JusticeFlow::ResultCode::OK)
    {
        Logger::error("ExpireWarrantsJob failed");
        return;
    }
    if (!results.empty())
    {
        Logger::info(("ExpireWarrantsJob expired " + std::to_string(results.size()) + " warrant(s)").c_str());
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