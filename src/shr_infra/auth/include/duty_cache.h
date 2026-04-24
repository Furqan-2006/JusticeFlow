#pragma once

#include <unordered_map>
#include <ctime>
#include "os_layer/threading/include/sync.h"
#include "common/constants.h"

namespace auth
{

    /**
     * @brief Cached entry for officer duty status
     */
    struct DutyCacheEntry
    {
        bool is_active;     // Is officer currently on duty?
        long cached_at;     // When was this cached (UTC timestamp)
        std::string status; // Full status string (for logging)
    };

    /**
     * @file duty_cache.h
     * @brief Officer duty status cache with TTL
     *
     * Caches the "is officer on duty?" decision with a 5-minute TTL.
     * On cache miss or expiry, queries PostgreSQL and refreshes.
     * Avoids a DB round-trip on every privileged operation.
     *
     * Protected by RWLock — read-heavy (validation), rare writes (duty changes).
     *
     * Owned as private member of AuthManager, not a singleton.
     */
    class DutyCache
    {
    public:
        /**
         * Checks if an officer's duty is currently active.
         *
         * On cache hit and fresh (< 5 min), returns cached value immediately.
         * On miss or stale (> 5 min), queries DB and refreshes cache.
         *
         * @param officer_id The officer's ID
         * @param out_active Output parameter: true if on duty
         * @return OK on success (result in out_active)
         *         NOT_FOUND if officer doesn't exist in DB
         *         DB_ERROR on database query failure
         */
        JusticeFlow::ResultCode check(int officer_id, bool &out_active);

        /**
         * Invalidates the cache entry for an officer.
         *
         * Called when duty status changes (officer goes on/off duty).
         * Next call to check() will query DB immediately.
         *
         * @param officer_id The officer's ID
         */
        void invalidate(int officer_id);

        /**
         * Gets the current cache size (for debugging).
         *
         * @return Number of cached entries
         */
        int getCacheSize() const;

    private:
        std::unordered_map<int, DutyCacheEntry> cache;
        mutable RWLock cache_lock;

        // TTL for duty status cache
        static constexpr long TTL_SECONDS = 5 * 60; // 5 minutes

        /**
         * Internal helper — query database for officer duty status.
         * Must be called while holding write lock.
         *
         * @param officer_id The officer's ID
         * @param out_active Output: true if on duty
         * @return OK on success, DB_ERROR on failure
         */
        JusticeFlow::ResultCode queryDutyStatus(int officer_id, bool &out_active);
    };

} // namespace auth