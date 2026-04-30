#pragma once

/**
 * @file domain_socket.h
 * @brief Unix domain socket server: listens on /tmp/justiceflow.sock for
 *        the Flask/Streamlit dashboard (read-only API gateway).
 *
 * Architecture role (DESIGN.md §6 / system diagram):
 *   Flask Dashboard
 *       │  Unix Domain Socket IPC
 *       ▼
 *   DomainSocket  ──► dispatch() ──► IpcManager (DB + shared memory)
 *
 * Wire protocol (per connection):
 *   Each message is length-prefixed:
 *     [4 bytes LE uint32 = payload length][payload bytes = UTF-8 JSON]
 *   Both requests and responses use this framing.
 *
 * Threading model:
 *   - One persistent accept thread (start() / stop()).
 *   - Each accepted client gets its own detached handler thread so the
 *     dashboard can pipeline requests without blocking the accept loop.
 *   - All IpcManager calls are already serialized internally (see ipc_manager.h).
 *
 * CRITICAL: DomainSocket must NOT be called from signal handlers.
 *   pthread_mutex_lock and PQexec (via IpcManager) are NOT async-signal-safe.
 *
 * SECURITY: The socket file is created with mode 0600. Only processes running
 *   as the same UID (the daemon) may connect. The Flask app must run as the
 *   same OS user as the C++ daemon, or the path must be behind a reverse proxy
 *   that enforces access control.
 *
 * Usage:
 * @code
 *   ipc::DomainSocket gateway;
 *   if (gateway.start() != JusticeFlow::ResultCode::OK) { ... }
 *   // ... daemon event loop ...
 *   gateway.stop();
 * @endcode
 */

#include <atomic>
#include <pthread.h>
#include <string>

#include "../../../common/constants.h"
#include "socket_request.h"

namespace ipc
{

    // ── Socket file path (matches diagram: /tmp/justiceflow.sock) ──────────────
    static constexpr const char *DOMAIN_SOCKET_PATH = "/tmp/justiceflow.sock";
    static constexpr int DOMAIN_SOCKET_BACKLOG = 8;

    // ── Per-message hard limit ──────────────────────────────────────────────────
    // 64 KiB is generous for any dashboard query result; guards against runaway
    // clients trying to exhaust server memory.
    static constexpr uint32_t DOMAIN_SOCKET_MAX_MSG_BYTES = 65536U;

    // ───────────────────────────────────────────────────────────────────────────
    class DomainSocket
    {
    public:
        /**
         * @param path  Filesystem path for the socket file.
         *              Defaults to DOMAIN_SOCKET_PATH (/tmp/justiceflow.sock).
         */
        explicit DomainSocket(const std::string &path = DOMAIN_SOCKET_PATH);
        ~DomainSocket();

        // Non-copyable (owns OS resources and a pthread_t)
        DomainSocket(const DomainSocket &) = delete;
        DomainSocket &operator=(const DomainSocket &) = delete;

        // ── Lifecycle ───────────────────────────────────────────────────────────

        /**
         * @brief Creates the socket file, binds, listens, and starts the accept
         *        thread.  Safe to call exactly once during daemon initialisation.
         *
         * @return ResultCode::OK            on success.
         *         ResultCode::FILE_SYSTEM_ERROR  if bind/listen fails (check errno /
         *                                        logs for detail).
         *         ResultCode::INVALID_STATE  if already running.
         */
        JusticeFlow::ResultCode start();

        /**
         * @brief Signals the accept thread to exit, joins it, closes the server
         *        fd, and unlinks the socket file.
         *
         * Idempotent: safe to call even if start() was never called or failed.
         */
        void stop();

        /**
         * @return true while the accept thread is running.
         */
        bool isRunning() const;

    private:
        // ── Internal helpers ────────────────────────────────────────────────────

        /**
         * @brief Entry point for the accept thread.
         *        Loops: accept() → spawn handler thread → repeat.
         *        Exits when running_ becomes false.
         */
        static void *acceptLoop(void *self);

        /**
         * @brief Entry point for a per-client handler thread.
         *        Receives one request frame, dispatches it, sends response frame,
         *        then closes the client fd.
         * @param arg  Heap-allocated ClientContext* (ownership transferred here).
         */
        static void *clientHandler(void *arg);

        /**
         * @brief Routes a parsed SocketRequest to the appropriate IpcManager
         *        call and returns a fully-populated SocketResponse.
         *
         * All commands are READ-ONLY from the dashboard's perspective.
         * SQL queries use whitelisted templates; params are validated before use.
         */
        SocketResponse dispatch(const SocketRequest &request);

        // ── Frame I/O ───────────────────────────────────────────────────────────
        // Both functions block until the full frame is transferred (EINTR-safe
        // via retry) or an error occurs.

        /**
         * @brief Reads one length-prefixed JSON frame from fd into out_json.
         *
         * @return ResultCode::OK            complete frame received.
         *         ResultCode::NOT_FOUND     peer closed connection (EOF).
         *         ResultCode::INVALID_INPUT payload exceeds DOMAIN_SOCKET_MAX_MSG_BYTES.
         *         ResultCode::FILE_SYSTEM_ERROR  I/O error (check errno).
         */
        JusticeFlow::ResultCode recvFrame(int fd, std::string &out_json);

        /**
         * @brief Writes one length-prefixed JSON frame from json to fd.
         *
         * @return ResultCode::OK            frame fully sent.
         *         ResultCode::FILE_SYSTEM_ERROR  I/O error or partial write.
         */
        JusticeFlow::ResultCode sendFrame(int fd, const std::string &json);

        // ── Dispatch helpers ────────────────────────────────────────────────────

        SocketResponse handlePing(const SocketRequest &req);
        SocketResponse handleGetAgentStatus(const SocketRequest &req);
        SocketResponse handleGetAgentStatusByIndex(const SocketRequest &req);
        SocketResponse handleGetActiveSessions(const SocketRequest &req);
        SocketResponse handleGetCaseList(const SocketRequest &req);
        SocketResponse handleGetCaseById(const SocketRequest &req);
        SocketResponse handleGetHotspotData(const SocketRequest &req);
        SocketResponse handleGetPriorityCases(const SocketRequest &req);
        SocketResponse handleGetOfficerWorkload(const SocketRequest &req);

        // ── Members ─────────────────────────────────────────────────────────────
        std::string socket_path_;
        int server_fd_;
        std::atomic<bool> running_;
        pthread_t accept_thread_;
    };

    // ── Internal: passed to the per-client thread via void* ────────────────────
    struct ClientContext
    {
        DomainSocket *server;
        int client_fd;
    };

} // namespace ipc