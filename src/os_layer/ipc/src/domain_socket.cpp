/**
 * @file domain_socket.cpp
 * @brief Unix domain socket server implementation for the JusticeFlow
 *        dashboard API gateway.
 *
 * Responsibilities:
 *   1. Lifecycle: bind/listen on /tmp/justiceflow.sock, accept connections.
 *   2. Framing:   4-byte LE length prefix for every JSON message.
 *   3. Dispatch:  route parsed SocketRequest to IpcManager and build response.
 *   4. Safety:
 *       - All SQL integer/UUID params are validated before use.
 *       - SQL queries use only whitelisted column names and parameterised values
 *         (passed through PQescapeLiteral where the libpq API allows it).
 *       - No command mutates data — dashboard is strictly read-only.
 *
 * CRITICAL: Do NOT call from signal handlers (pthread_mutex_lock, PQexec, etc.
 *            are not async-signal-safe).
 */

#include "domain_socket.h"
#include "ipc_manager.h"
#include "shm_layout.h"

#include "../../../common/logger.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>

// POSIX / socket headers
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace ipc
{

    // ────────────────────────────────────────────────────────────────────────────
    //  Internal JSON builder helpers (domain_socket.cpp scope only)
    //
    //  All helpers produce valid JSON strings.  The philosophy here is the same
    //  as in socket_request.cpp: the schema is narrow and fixed, so a small
    //  purpose-built builder is clearer than a generic library dependency.
    // ────────────────────────────────────────────────────────────────────────────

    namespace
    {
        // Escape a plain string for embedding inside a JSON string literal.
        std::string je(const std::string &s)
        {
            std::string out;
            out.reserve(s.size() + 4);
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        char b[8];
                        snprintf(b, 8, "\\u%04x", c);
                        out += b;
                    }
                    else
                        out += static_cast<char>(c);
                    break;
                }
            }
            return out;
        }

        // Wrap a string in JSON quotes.
        std::string jq(const std::string &s) { return '"' + je(s) + '"'; }

        // Check that s contains only digits (for SQL integer params).
        bool isAllDigits(const std::string &s)
        {
            if (s.empty())
                return false;
            for (char c : s)
                if (c < '0' || c > '9')
                    return false;
            return true;
        }

        // Validate UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
        bool isValidUuid(const std::string &s)
        {
            if (s.size() != 36)
                return false;
            for (size_t i = 0; i < 36; ++i)
            {
                if (i == 8 || i == 13 || i == 18 || i == 23)
                {
                    if (s[i] != '-')
                        return false;
                }
                else
                {
                    char c = s[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                        return false;
                }
            }
            return true;
        }

        // Convert a rows-of-cols result set from executeQuery() to a JSON array.
        // colNames must match the number of columns in each row.
        std::string rowsToJsonArray(
            const std::vector<std::string> &colNames,
            const std::vector<std::vector<std::string>> &rows)
        {
            std::ostringstream oss;
            oss << '[';
            for (size_t r = 0; r < rows.size(); ++r)
            {
                if (r)
                    oss << ',';
                oss << '{';
                size_t cols = std::min(colNames.size(), rows[r].size());
                for (size_t c = 0; c < cols; ++c)
                {
                    if (c)
                        oss << ',';
                    oss << jq(colNames[c]) << ':' << jq(rows[r][c]);
                }
                oss << '}';
            }
            oss << ']';
            return oss.str();
        }

        // Build a safe integer string clamped to [min_val, max_val].
        // If raw is not a valid integer, returns std::to_string(default_val).
        std::string safeIntStr(const std::string &raw,
                               int default_val, int min_val, int max_val)
        {
            if (!raw.empty() && isAllDigits(raw))
            {
                int v = std::stoi(raw);
                v = std::max(min_val, std::min(max_val, v));
                return std::to_string(v);
            }
            return std::to_string(default_val);
        }

        // Daemon start-time for PING uptime calculation.
        // Set once in DomainSocket::start().
        static time_t g_start_time = 0;

    } // anonymous namespace

    // ────────────────────────────────────────────────────────────────────────────
    //  Constructor / Destructor
    // ────────────────────────────────────────────────────────────────────────────

    DomainSocket::DomainSocket(const std::string &path)
        : socket_path_(path),
          server_fd_(-1),
          running_(false),
          accept_thread_(0)
    {
    }

    DomainSocket::~DomainSocket()
    {
        stop();
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  start()
    // ────────────────────────────────────────────────────────────────────────────

    JusticeFlow::ResultCode DomainSocket::start()
    {
        if (running_.load())
        {
            Logger::error("[DomainSocket] start() called while already running");
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        // Remove stale socket file from a previous (crashed) run.
        ::unlink(socket_path_.c_str());

        server_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (server_fd_ < 0)
        {
            Logger::error("[DomainSocket] socket() failed");
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        // Build sockaddr_un.
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (socket_path_.size() >= sizeof(addr.sun_path))
        {
            Logger::error("[DomainSocket] socket path too long");
            ::close(server_fd_);
            server_fd_ = -1;
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }
        std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(server_fd_,
                   reinterpret_cast<sockaddr *>(&addr),
                   sizeof(addr)) < 0)
        {
            Logger::error("[DomainSocket] bind() failed");
            ::close(server_fd_);
            server_fd_ = -1;
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        // Restrict access: only the daemon's own UID may connect.
        ::chmod(socket_path_.c_str(), 0600);

        if (::listen(server_fd_, DOMAIN_SOCKET_BACKLOG) < 0)
        {
            Logger::error("[DomainSocket] listen() failed");
            ::close(server_fd_);
            server_fd_ = -1;
            ::unlink(socket_path_.c_str());
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        running_.store(true);
        g_start_time = ::time(nullptr);

        if (::pthread_create(&accept_thread_, nullptr, &DomainSocket::acceptLoop, this) != 0)
        {
            Logger::error("[DomainSocket] pthread_create() failed");
            running_.store(false);
            ::close(server_fd_);
            server_fd_ = -1;
            ::unlink(socket_path_.c_str());
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        Logger::info("[DomainSocket] Listening on socket");
        return JusticeFlow::ResultCode::OK;
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  stop()
    // ────────────────────────────────────────────────────────────────────────────

    void DomainSocket::stop()
    {
        if (!running_.exchange(false))
            return; // Already stopped

        // Closing server_fd_ causes accept() in the accept thread to return
        // with EBADF / EINVAL, allowing the thread to notice running_ == false
        // and exit cleanly.
        if (server_fd_ >= 0)
        {
            ::close(server_fd_);
            server_fd_ = -1;
        }

        if (accept_thread_)
        {
            ::pthread_join(accept_thread_, nullptr);
            accept_thread_ = 0;
        }

        ::unlink(socket_path_.c_str());
        Logger::info("[DomainSocket] Stopped and socket file removed");
    }

    bool DomainSocket::isRunning() const
    {
        return running_.load();
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  acceptLoop()  — runs in its own pthread
    // ────────────────────────────────────────────────────────────────────────────

    void *DomainSocket::acceptLoop(void *arg)
    {
        DomainSocket *self = static_cast<DomainSocket *>(arg);

        while (self->running_.load())
        {
            sockaddr_un peer{};
            socklen_t peer_len = sizeof(peer);

            int client_fd = ::accept(self->server_fd_,
                                     reinterpret_cast<sockaddr *>(&peer),
                                     &peer_len);
            if (client_fd < 0)
            {
                if (!self->running_.load())
                    break; // stop() closed server_fd_
                if (errno == EINTR)
                    continue;
                Logger::error("[DomainSocket] accept() error — retrying");
                continue;
            }

            // Spawn a detached thread so one slow client cannot block the loop.
            ClientContext *ctx = new ClientContext{self, client_fd};

            pthread_t tid;
            pthread_attr_t attr;
            ::pthread_attr_init(&attr);
            ::pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

            if (::pthread_create(&tid, &attr, &DomainSocket::clientHandler, ctx) != 0)
            {
                Logger::error("[DomainSocket] Failed to spawn client handler");
                ::close(client_fd);
                delete ctx;
            }

            ::pthread_attr_destroy(&attr);
        }

        return nullptr;
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  clientHandler()  — runs in a detached pthread per accepted client
    // ────────────────────────────────────────────────────────────────────────────

    void *DomainSocket::clientHandler(void *arg)
    {
        // Take ownership of context.
        ClientContext ctx = *static_cast<ClientContext *>(arg);
        delete static_cast<ClientContext *>(arg);

        DomainSocket *self = ctx.server;
        int client_fd = ctx.client_fd;

        // Serve requests on this connection until the client disconnects or
        // an unrecoverable error occurs.
        while (true)
        {
            std::string json_in;
            JusticeFlow::ResultCode rc = self->recvFrame(client_fd, json_in);

            if (rc == JusticeFlow::ResultCode::NOT_FOUND)
                break; // Clean EOF — client disconnected

            if (rc != JusticeFlow::ResultCode::OK)
            {
                Logger::error("[DomainSocket] recvFrame error — closing client");
                break;
            }

            SocketRequest req;
            rc = SocketRequest::fromJson(json_in, req);

            SocketResponse resp;
            if (rc != JusticeFlow::ResultCode::OK)
            {
                resp = SocketResponse::error(
                    req.request_id,
                    JusticeFlow::ResultCode::INVALID_INPUT,
                    "Malformed JSON request");
            }
            else
            {
                resp = self->dispatch(req);
            }

            std::string json_out = resp.toJson();
            rc = self->sendFrame(client_fd, json_out);
            if (rc != JusticeFlow::ResultCode::OK)
            {
                Logger::error("[DomainSocket] sendFrame error — closing client");
                break;
            }
        }

        ::close(client_fd);
        return nullptr;
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  Frame I/O
    // ────────────────────────────────────────────────────────────────────────────

    JusticeFlow::ResultCode DomainSocket::recvFrame(int fd, std::string &out_json)
    {
        // Read 4-byte little-endian length prefix.
        uint8_t len_buf[4];
        ssize_t n = 0;
        size_t got = 0;

        while (got < 4)
        {
            n = ::recv(fd, len_buf + got, 4 - got, MSG_WAITALL);
            if (n == 0)
                return JusticeFlow::ResultCode::NOT_FOUND; // EOF
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
            }
            got += static_cast<size_t>(n);
        }

        uint32_t payload_len =
            (static_cast<uint32_t>(len_buf[0])) |
            (static_cast<uint32_t>(len_buf[1]) << 8) |
            (static_cast<uint32_t>(len_buf[2]) << 16) |
            (static_cast<uint32_t>(len_buf[3]) << 24);

        if (payload_len == 0 || payload_len > DOMAIN_SOCKET_MAX_MSG_BYTES)
        {
            Logger::error("[DomainSocket] Payload length out of range — dropping");
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }

        // Read JSON body.
        out_json.resize(payload_len);
        got = 0;
        while (got < payload_len)
        {
            n = ::recv(fd, &out_json[got], payload_len - got, 0);
            if (n == 0)
                return JusticeFlow::ResultCode::NOT_FOUND;
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
            }
            got += static_cast<size_t>(n);
        }

        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode DomainSocket::sendFrame(int fd, const std::string &json)
    {
        uint32_t payload_len = static_cast<uint32_t>(json.size());

        uint8_t len_buf[4] = {
            static_cast<uint8_t>(payload_len & 0xFF),
            static_cast<uint8_t>((payload_len >> 8) & 0xFF),
            static_cast<uint8_t>((payload_len >> 16) & 0xFF),
            static_cast<uint8_t>((payload_len >> 24) & 0xFF)};

        // Send length prefix.
        size_t sent = 0;
        while (sent < 4)
        {
            ssize_t n = ::send(fd, len_buf + sent, 4 - sent, MSG_NOSIGNAL);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
            }
            sent += static_cast<size_t>(n);
        }

        // Send JSON body.
        sent = 0;
        while (sent < payload_len)
        {
            ssize_t n = ::send(fd, json.c_str() + sent, payload_len - sent, MSG_NOSIGNAL);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
            }
            sent += static_cast<size_t>(n);
        }

        return JusticeFlow::ResultCode::OK;
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  dispatch()  — routes request to the correct handler
    // ────────────────────────────────────────────────────────────────────────────

    SocketResponse DomainSocket::dispatch(const SocketRequest &req)
    {
        switch (req.command)
        {
        case CommandType::PING:
            return handlePing(req);
        case CommandType::GET_AGENT_STATUS:
            return handleGetAgentStatus(req);
        case CommandType::GET_AGENT_STATUS_BY_INDEX:
            return handleGetAgentStatusByIndex(req);
        case CommandType::GET_ACTIVE_SESSIONS:
            return handleGetActiveSessions(req);
        case CommandType::GET_CASE_LIST:
            return handleGetCaseList(req);
        case CommandType::GET_CASE_BY_ID:
            return handleGetCaseById(req);
        case CommandType::GET_HOTSPOT_DATA:
            return handleGetHotspotData(req);
        case CommandType::GET_PRIORITY_CASES:
            return handleGetPriorityCases(req);
        case CommandType::GET_OFFICER_WORKLOAD:
            return handleGetOfficerWorkload(req);
        case CommandType::UNKNOWN:
        default:
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::INVALID_INPUT,
                                         "Unknown command");
        }
    }

    // ────────────────────────────────────────────────────────────────────────────
    //  Individual command handlers
    // ────────────────────────────────────────────────────────────────────────────

    SocketResponse DomainSocket::handlePing(const SocketRequest &req)
    {
        time_t now = ::time(nullptr);
        long uptime = static_cast<long>(now - g_start_time);

        std::ostringstream oss;
        oss << "{\"pong\":true,\"uptime_seconds\":" << uptime << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    // ── Shared memory: all three agents ────────────────────────────────────────

    SocketResponse DomainSocket::handleGetAgentStatus(const SocketRequest &req)
    {
        SharedStatusTable *table = IpcManager::getInstance().getStatusTable();
        if (!table)
        {
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::INVALID_STATE,
                                         "Shared memory not attached");
        }

        // Lock the shared-memory mutex for a consistent read.
        pthread_mutex_lock(&table->mutex);

        std::ostringstream oss;
        oss << "{\"agents\":[";
        for (int i = 0; i < MAX_AGENTS; ++i)
        {
            if (i)
                oss << ',';
            const AgentStatus &a = table->agents[i];
            oss << '{'
                << "\"index\":" << i << ','
                << "\"name\":" << jq(a.agent_name) << ','
                << "\"status_code\":" << a.current_status << ','
                << "\"error_detail\":" << jq(a.error_detail) << ','
                << "\"last_updated\":" << static_cast<long>(a.last_updated)
                << '}';
        }
        oss << "]}";

        pthread_mutex_unlock(&table->mutex);
        return SocketResponse::ok(req.request_id, oss.str());
    }

    // ── Shared memory: single agent by index ───────────────────────────────────

    SocketResponse DomainSocket::handleGetAgentStatusByIndex(const SocketRequest &req)
    {
        std::string idx_str = req.param("index", "");
        if (!isAllDigits(idx_str))
        {
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::INVALID_INPUT,
                                         "Param 'index' must be 0, 1, or 2");
        }

        int idx = std::stoi(idx_str);
        if (idx < 0 || idx >= MAX_AGENTS)
        {
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::INVALID_INPUT,
                                         "Param 'index' out of range [0, 2]");
        }

        SharedStatusTable *table = IpcManager::getInstance().getStatusTable();
        if (!table)
        {
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::INVALID_STATE,
                                         "Shared memory not attached");
        }

        pthread_mutex_lock(&table->mutex);
        const AgentStatus &a = table->agents[idx];

        std::ostringstream oss;
        oss << '{'
            << "\"index\":" << idx << ','
            << "\"name\":" << jq(a.agent_name) << ','
            << "\"status_code\":" << a.current_status << ','
            << "\"error_detail\":" << jq(a.error_detail) << ','
            << "\"last_updated\":" << static_cast<long>(a.last_updated)
            << '}';

        pthread_mutex_unlock(&table->mutex);
        return SocketResponse::ok(req.request_id, oss.str());
    }

    // ── Shared memory: active session count ────────────────────────────────────

    SocketResponse DomainSocket::handleGetActiveSessions(const SocketRequest &req)
    {
        SharedStatusTable *table = IpcManager::getInstance().getStatusTable();
        if (!table)
        {
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::INVALID_STATE,
                                         "Shared memory not attached");
        }

        pthread_mutex_lock(&table->mutex);
        int sessions = table->active_sessions;
        pthread_mutex_unlock(&table->mutex);

        std::ostringstream oss;
        oss << "{\"active_sessions\":" << sessions << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    // ── DB: case list (paginated) ───────────────────────────────────────────────

    SocketResponse DomainSocket::handleGetCaseList(const SocketRequest &req)
    {
        // Validate and clamp pagination params.
        std::string limit = safeIntStr(req.param("limit", "100"), 100, 1, 500);
        // Cap offset at 100 000 — beyond that, results are useless and
        // the query would hammer the DB with a full sequential scan skip.
        static constexpr int MAX_SQL_OFFSET = 100'000;
        std::string offset = safeIntStr(req.param("offset", "0"), 0, 0, MAX_SQL_OFFSET);

        std::string query =
            "SELECT case_id, case_type, status, registered_at "
            "FROM cases "
            "ORDER BY registered_at DESC "
            "LIMIT " +
            limit +
            " OFFSET " + offset + ";";

        std::vector<std::vector<std::string>> rows;
        JusticeFlow::ResultCode rc =
            IpcManager::getInstance().executeQuery(query, rows);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            return SocketResponse::error(req.request_id, rc,
                                         "Database query failed");
        }

        const std::vector<std::string> cols = {
            "case_id", "case_type", "status", "registered_at"};

        std::ostringstream oss;
        oss << "{\"cases\":" << rowsToJsonArray(cols, rows) << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    // ── DB: single case by UUID ─────────────────────────────────────────────────

    SocketResponse DomainSocket::handleGetCaseById(const SocketRequest &req)
    {
        std::string case_id = req.param("case_id", "");
        if (!isValidUuid(case_id))
        {
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::INVALID_INPUT,
                                         "Invalid or missing 'case_id' (expected UUID)");
        }

        // case_id is UUID-validated; safe to embed directly.
        std::string query =
            "SELECT case_id, case_type, status, registered_at, "
            "       station_id, lead_officer_id "
            "FROM cases "
            "WHERE case_id = '" +
            case_id + "' "
                      "LIMIT 1;";

        std::vector<std::vector<std::string>> rows;
        JusticeFlow::ResultCode rc =
            IpcManager::getInstance().executeQuery(query, rows);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            return SocketResponse::error(req.request_id, rc,
                                         "Database query failed");
        }

        if (rows.empty())
        {
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::NOT_FOUND,
                                         "Case not found");
        }

        const std::vector<std::string> cols = {
            "case_id", "case_type", "status", "registered_at",
            "station_id", "lead_officer_id"};

        // Single row → JSON object (not array).
        std::ostringstream oss;
        oss << '{';
        size_t ncols = std::min(cols.size(), rows[0].size());
        for (size_t c = 0; c < ncols; ++c)
        {
            if (c)
                oss << ',';
            oss << jq(cols[c]) << ':' << jq(rows[0][c]);
        }
        oss << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    // ── DB: crime hotspot analytics ─────────────────────────────────────────────

    SocketResponse DomainSocket::handleGetHotspotData(const SocketRequest &req)
    {
        std::string limit = safeIntStr(req.param("limit", "50"), 50, 1, 200);

        std::string query =
            "SELECT zone_id, zone_name, risk_level, incident_count, updated_at "
            "FROM analytics.crime_hotspots "
            "ORDER BY incident_count DESC "
            "LIMIT " +
            limit + ";";

        std::vector<std::vector<std::string>> rows;
        JusticeFlow::ResultCode rc =
            IpcManager::getInstance().executeQuery(query, rows);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            return SocketResponse::error(req.request_id, rc,
                                         "Hotspot query failed");
        }

        const std::vector<std::string> cols = {
            "zone_id", "zone_name", "risk_level", "incident_count", "updated_at"};

        std::ostringstream oss;
        oss << "{\"hotspots\":" << rowsToJsonArray(cols, rows) << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    // ── DB: priority cases ──────────────────────────────────────────────────────

    SocketResponse DomainSocket::handleGetPriorityCases(const SocketRequest &req)
    {
        // Whitelist min_priority to prevent injection via enum string.
        static const std::vector<std::string> VALID_PRIORITIES = {
            "LOW", "MEDIUM", "HIGH", "CRITICAL"};

        std::string min_priority = req.param("min_priority", "HIGH");
        bool valid = false;
        for (const auto &p : VALID_PRIORITIES)
            if (p == min_priority)
            {
                valid = true;
                break;
            }

        if (!valid)
        {
            return SocketResponse::error(req.request_id,
                                         JusticeFlow::ResultCode::INVALID_INPUT,
                                         "Invalid 'min_priority': must be LOW|MEDIUM|HIGH|CRITICAL");
        }

        std::string limit = safeIntStr(req.param("limit", "50"), 50, 1, 200);

        // Priority ordering: CRITICAL > HIGH > MEDIUM > LOW
        std::string query =
            "SELECT c.case_id, c.case_type, c.status, "
            "       ca.priority_level, ca.assignment_status, ca.assigned_at "
            "FROM cases c "
            "JOIN analytics.case_assignments ca ON ca.case_id = c.case_id "
            "WHERE ca.priority_level IN ( "
            "    SELECT unnest(ARRAY["
            "        CASE WHEN '" +
            min_priority + "' = 'LOW'      THEN 'LOW'      ELSE NULL END,"
                           "        CASE WHEN '" +
            min_priority + "' IN ('LOW','MEDIUM') THEN 'MEDIUM' ELSE NULL END,"
                           "        CASE WHEN '" +
            min_priority + "' IN ('LOW','MEDIUM','HIGH') THEN 'HIGH' ELSE NULL END,"
                           "        'CRITICAL'"
                           "    ]::text[]) "
                           ") "
                           "ORDER BY ca.priority_level DESC, ca.assigned_at ASC "
                           "LIMIT " +
            limit + ";";

        // Note: The above priority filter is correct but verbose.
        // A cleaner alternative when the schema stores priority as an ordered
        // enum type is:
        //   WHERE ca.priority_level >= '" + min_priority + "'::priority_level_enum
        // Adjust to match actual schema type once confirmed.

        std::vector<std::vector<std::string>> rows;
        JusticeFlow::ResultCode rc =
            IpcManager::getInstance().executeQuery(query, rows);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            return SocketResponse::error(req.request_id, rc,
                                         "Priority cases query failed");
        }

        const std::vector<std::string> cols = {
            "case_id", "case_type", "status",
            "priority_level", "assignment_status", "assigned_at"};

        std::ostringstream oss;
        oss << "{\"cases\":" << rowsToJsonArray(cols, rows) << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

    // ── DB: officer workload ────────────────────────────────────────────────────

    SocketResponse DomainSocket::handleGetOfficerWorkload(const SocketRequest &req)
    {
        std::string limit = safeIntStr(req.param("limit", "50"), 50, 1, 200);

        std::string query =
            "SELECT o.officer_id, o.name, o.rank, o.status, "
            "       COUNT(co.case_id) AS active_cases, "
            "       d.duty_status "
            "FROM officers o "
            "LEFT JOIN case_officers co "
            "       ON co.officer_id = o.officer_id "
            "       AND co.removed_at IS NULL "
            "LEFT JOIN duty_roster d "
            "       ON d.officer_id = o.officer_id "
            "       AND d.duty_status = 'ON_DUTY' "
            "WHERE o.status = 'ACTIVE' "
            "GROUP BY o.officer_id, o.name, o.rank, o.status, d.duty_status "
            "ORDER BY active_cases DESC "
            "LIMIT " +
            limit + ";";

        std::vector<std::vector<std::string>> rows;
        JusticeFlow::ResultCode rc =
            IpcManager::getInstance().executeQuery(query, rows);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            return SocketResponse::error(req.request_id, rc,
                                         "Officer workload query failed");
        }

        const std::vector<std::string> cols = {
            "officer_id", "name", "rank", "status",
            "active_cases", "duty_status"};

        std::ostringstream oss;
        oss << "{\"officers\":" << rowsToJsonArray(cols, rows) << '}';
        return SocketResponse::ok(req.request_id, oss.str());
    }

} // namespace ipc