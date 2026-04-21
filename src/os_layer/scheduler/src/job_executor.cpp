#include "../include/job_executor.h"
#include "../../ipc/include/ipc_manager.h"
#include "common/logger.h"

void ExpireWarrantsJob::execute()
{
    Logger::info("Executing ExpireWarrantsJob...");
    JusticeFlow::ResultCode res = ipc::IpcManager::getInstance().executeQuery("UPDATE warrants SET status='EXPIRED' WHERE expiry_date < NOW()");
    if (res != JusticeFlow::ResultCode::OK)
    {
        Logger::error("ExpireWarrantsJob failed!");
    }
}

void CheckDatabaseHealthJob::execute()
{
    Logger::debug("Executing CheckDatabaseHealthJob...");
    JusticeFlow::ResultCode res = ipc::IpcManager::getInstance().executeQuery("SELECT 1");
    if (res != JusticeFlow::ResultCode::OK)
    {
        Logger::error("Database health check failed! DB unreachable.");
    }
}