#pragma once

#include <string>
#include <unordered_map>
#include <ctime>
#include "os_layer/threading/include/sync.h"
#include "common/constants.h"

namespace auth {

/**
 * @brief In-memory session context
 * 
 * Represents an authenticated officer session. Owned by session_store.
 */
struct SessionContext {
    std::string token;              // UUID v4 token for this session
    int officer_id;                 // Officer's unique ID from DB
    std::string officer_rank;       // Officer's rank (Constable, Inspector, etc.)
    long login_timestamp;           // When session started (UTC)
    long expires_at;                // Hard expiry (8 hours from login)
    long last_active_at;            // Last request time (for 30-min idle timeout)
    bool is_duty_active;            // Cached duty status at login time
};

/**
 * @file session_store.h
 * @brief In-memory session storage with DB persistence
 * 
 * Owns the authoritative session map — keyed by token string.
 * Protected by RWLock for concurrent read-heavy validation.
 * 
 * On every incoming request:
 *   1. Worker calls session_store::validate(token)
 *   2. Read lock acquired, O(1) map lookup
 *   3. Expiry checked (hard + idle)
 *   4. SessionContext returned or SESSION_EXPIRED
 * 
 * Writes (login/logout/refresh) acquire write lock.
 */
class SessionStore {
public:
    /**
     * Inserts a new session into the store.
     * 
     * Acquires write lock. Also writes to PostgreSQL sessions table
     * for persistence and audit trail.
     * 
     * @param session The SessionContext to insert
     * @return OK on success, DB_ERROR on database failure
     */
    JusticeFlow::ResultCode insert(const SessionContext& session);
    
    /**
     * Validates a token and returns the session context.
     * 
     * Acquires read lock. Checks both hard expiry (8 hours)
     * and idle timeout (30 minutes).
     * 
     * @param token The session token string
     * @param out_session Output parameter populated on success
     * @return OK if valid and not expired
     *         SESSION_EXPIRED if past hard or idle expiry
     *         NOT_FOUND if token not in map
     */
    JusticeFlow::ResultCode validate(const std::string& token, SessionContext& out_session);
    
    /**
     * Refreshes a session by resetting idle timeout.
     * 
     * Acquires write lock. Resets last_active_at to current time.
     * Does NOT extend hard expiry — a session at 7h55m cannot reach 8h10m.
     * Also updates PostgreSQL sessions table.
     * 
     * @param token The session token to refresh
     * @return OK on success
     *         SESSION_EXPIRED if hard expiry already reached
     *         NOT_FOUND if token not in map
     *         DB_ERROR on database failure
     */
    JusticeFlow::ResultCode refresh(const std::string& token);
    
    /**
     * Removes a session from the store (logout).
     * 
     * Acquires write lock. Marks the session as inactive in PostgreSQL
     * for audit trail. Erases from in-memory map.
     * 
     * @param token The session token to remove
     * @return OK on success
     *         NOT_FOUND if token not in map
     *         DB_ERROR on database failure
     */
    JusticeFlow::ResultCode remove(const std::string& token);
    
    /**
     * Gets the current active session count.
     * 
     * @return Number of valid (non-expired) sessions in memory
     */
    int getActiveCount() const;
    
private:
    std::unordered_map<std::string, SessionContext> session_map;
    mutable RWLock map_lock;
    
    // Constants
    static constexpr long HARD_EXPIRY_SECONDS = 8 * 3600;       // 8 hours
    static constexpr long IDLE_TIMEOUT_SECONDS = 30 * 60;       // 30 minutes
    
    /**
     * Internal helper — check if a session has expired.
     * Must be called while holding the read lock.
     * 
     * @return true if hard expiry OR idle timeout reached
     */
    bool isExpired(const SessionContext& session) const;
};

} // namespace auth