#include "../include/duty_cache.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"

#include <ctime>
#include <cstdio>

namespace auth
{

    JusticeFlow::ResultCode DutyCache::check(int officer_id, bool &out_active)
    {
        long now = time(nullptr);

        // Try read lock first — most calls will hit cache
        {
            RWLockReadGuard read_lock(cache_lock);

            auto it = cache.find(officer_id);
            if (it != cache.end())
            {
                // Check if cached value is still fresh
                if (now - it->second.cached_at < TTL_SECONDS)
                {
                    // Cache hit!
                    out_active = it->second.is_active;

                    char log_buf[128];
                    std::snprintf(log_buf, sizeof(log_buf),
                                  "[DutyCache] Hit for officer %d: %s",
                                  officer_id, out_active ? "ON_DUTY" : "OFF_DUTY");
                    Logger::debug(log_buf);

                    return JusticeFlow::ResultCode::OK;
                }
                // Entry exists but stale — fall through to write lock
            }
        }

        // Cache miss or stale — acquire write lock to refresh
        RWLockWriteGuard write_lock(cache_lock);

        JusticeFlow::ResultCode result = queryDutyStatus(officer_id, out_active);

        if (result == JusticeFlow::ResultCode::OK)
        {
            // Update cache
            DutyCacheEntry entry;
            entry.is_active = out_active;
            entry.cached_at = now;
            entry.status = out_active ? "ON_DUTY" : "OFF_DUTY";
            cache[officer_id] = entry;

            char log_buf[128];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[DutyCache] Refreshed for officer %d: %s",
                          officer_id, entry.status.c_str());
            Logger::debug(log_buf);
        }

        return result;
    }

    void DutyCache::invalidate(int officer_id)
    {
        RWLockWriteGuard lock(cache_lock);
        cache.erase(officer_id);

        char log_buf[128];
        std::snprintf(log_buf, sizeof(log_buf),
                      "[DutyCache] Invalidated for officer %d", officer_id);
        Logger::info(log_buf);
    }

    int DutyCache::getCacheSize() const
    {
        RWLockReadGuard lock(cache_lock);
        return static_cast<int>(cache.size());
    }

    JusticeFlow::ResultCode DutyCache::queryDutyStatus(int officer_id, bool &out_active)
    {
        // Query: SELECT is_on_duty FROM officers WHERE officer_id = ?
        char query[256];
        std::snprintf(query, sizeof(query),
                      "SELECT status FROM officers WHERE officer_id = %d", officer_id);

        std::vector<std::vector<std::string>> results;
        JusticeFlow::ResultCode db_result =
            ipc::IpcManager::getInstance().executeQuery(query, results);

        if (db_result != JusticeFlow::ResultCode::OK)
        {
            char log_buf[128];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[DutyCache] DB query failed for officer %d", officer_id);
            Logger::error(log_buf);
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        if (results.empty() || results[0].empty())
        {
            char log_buf[128];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[DutyCache] Officer %d not found in database", officer_id);
            Logger::error(log_buf);
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        // Parse result — expecting "true" or "false"
        const std::string &duty_str = results[0][0];
        out_active = (duty_str == "true" || duty_str == "t" || duty_str == "1");

        return JusticeFlow::ResultCode::OK;
    }

} // namespace auth