#pragma once

#include "session_store.h"
#include "duty_cache.h"
#include "token_generator.h"
#include "os_layer/ipc/include/unix_socket.h"
#include "common/constants.h"

#include <memory>

namespace auth
{

    /**
     * @file auth_manager.h
     * @brief Singleton authentication and session manager
     *
     * Central authority for:
     * 1. Officer login/logout
     * 2. Token validation on every request
     * 3. Session refresh and expiry
     * 4. Duty status caching
     *
     * Owns three private components:
     * - SessionStore: In-memory session map
     * - DutyCache: Officer duty status cache (5-min TTL)
     * - Private UnixSocket: Dedicated DB connection (separate from IPC pool)
     *
     * Thread-safe by design — all RWLock contention happens inside the components.
     */
    class AuthManager
    {
    private:
        std::unique_ptr<SessionStore> session_store;
        std::unique_ptr<DutyCache> duty_cache;
        std::unique_ptr<ipc::UnixSocket> db_connection;

        // Singleton
        AuthManager();
        ~AuthManager();
        AuthManager(const AuthManager &) = delete;
        AuthManager &operator=(const AuthManager &) = delete;

    public:
        static AuthManager &getInstance();

        /**
         * Authenticates an officer via CNIC and password.
         *
         * Flow:
         * 1. Query DB: verify CNIC exists
         * 2. Send password to DB (via Unix socket, local only)
         * 3. PostgreSQL runs crypt() verification
         * 4. Generate random token via token_generator
         * 5. Create SessionContext with 8-hour hard expiry
         * 6. Insert into session_store
         * 7. Return session to caller
         *
         * CRITICAL: Password is pinned with mlock_guard and zeroed
         * immediately after use — never persists in session state.
         *
         * @param cnic The officer's CNIC number (username)
         * @param password The plaintext password (for crypt() verification)
         * @param out_session Output parameter populated on success
         * @return OK on successful login
         *         AUTHENTICATION_FAILED if credentials invalid
         *         NOT_FOUND if CNIC not in database
         *         DB_ERROR on query failure
         */
        JusticeFlow::ResultCode login(const std::string &cnic,
                                      const std::string &password,
                                      SessionContext &out_session);

        /**
         * Validates a session token and returns the session context.
         *
         * Called on EVERY incoming request from worker threads.
         * Acquires read lock on session_store — O(1) token lookup.
         * Checks both hard expiry (8 hours) and idle timeout (30 min).
         *
         * If expired, automatically refreshes the idle timeout for the next
         * request (within hard expiry bounds).
         *
         * @param token The session token from the incoming request
         * @param out_session Output parameter populated on success
         * @return OK if valid and not expired
         *         SESSION_EXPIRED if past hard or idle deadline
         *         INVALID_INPUT if token is empty
         *         NOT_FOUND if token not in session store
         */
        JusticeFlow::ResultCode validateToken(const std::string &token,
                                              SessionContext &out_session);

        /**
         * Validates an officer's rank meets a minimum requirement.
         *
         * Compares officer's rank (from session) against minimum_rank.
         * Rank hierarchy:
         * - 0: Constable
         * - 1: Inspector
         * - 2: Sub-Inspector
         * - 3: DSP
         *
         * @param session The authenticated session
         * @param minimum_rank Minimum rank required (e.g., 2 for Sub-Inspector)
         * @return OK if officer's rank >= minimum_rank
         *         PERMISSION_DENIED if rank insufficient
         */
        JusticeFlow::ResultCode validateRank(const SessionContext &session, int minimum_rank);

        /**
         * Checks if an officer's duty is currently active.
         *
         * Queries duty_cache with 5-minute TTL. On miss, queries DB and caches.
         *
         * @param officer_id The officer's ID
         * @param out_active Output: true if on duty
         * @return OK on success
         *         NOT_FOUND if officer doesn't exist
         *         DB_ERROR on query failure
         */
        JusticeFlow::ResultCode isDutyActive(int officer_id, bool &out_active);

        /**
         * Refreshes a session by resetting idle timeout.
         *
         * Called after processing a request to keep the session alive.
         * Does NOT extend hard expiry — session at 7h55m cannot reach 8h10m.
         *
         * @param session The session to refresh (modified in-place)
         * @return OK on success
         *         SESSION_EXPIRED if hard expiry already reached
         *         NOT_FOUND if token not in session store
         *         DB_ERROR on database failure
         */
        JusticeFlow::ResultCode refreshSession(SessionContext &session);

        /**
         * Logs out an officer and destroys the session.
         *
         * Removes session from session_store and marks inactive in DB.
         *
         * @param session The session to destroy
         * @return OK on success
         *         NOT_FOUND if token not in session store
         *         DB_ERROR on database failure
         */
        JusticeFlow::ResultCode logout(const SessionContext &session);

        /**
         * Gets the current active session count.
         *
         * Used for monitoring and debugging.
         *
         * @return Number of valid (non-expired) sessions
         */
        int getActiveSessionCount() const;
    };

} // namespace auth