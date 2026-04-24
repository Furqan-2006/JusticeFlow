#include "../include/worker.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "shr_infra/auth/include/auth_module.h"
#include "common/logger.h"

#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <ctime>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>

namespace
{

// ============================================================================
// Protocol Constants
// ============================================================================

// Message framing: [4 bytes length][4 bytes type][payload]
#define FRAME_HEADER_SIZE 8
#define MAX_FRAME_SIZE 65536

    // Message types
    enum MessageType : uint32_t
    {
        MSG_TYPE_AUTH_REQUEST = 1,   // Login request
        MSG_TYPE_QUERY_REQUEST = 2,  // SQL query execution
        MSG_TYPE_QUERY_RESULT = 3,   // Query results (success)
        MSG_TYPE_ERROR_RESPONSE = 4, // Error response
        MSG_TYPE_AUTH_RESPONSE = 5,  // Login response (token)
        MSG_TYPE_LOGOUT = 6,         // Logout request
    };

    // Error codes for error responses
    enum ErrorCode : uint32_t
    {
        ERR_AUTH_FAILED = 1,
        ERR_SESSION_EXPIRED = 2,
        ERR_PERMISSION_DENIED = 3,
        ERR_QUERY_FAILED = 4,
        ERR_PROTOCOL_ERROR = 5,
        ERR_INTERNAL_ERROR = 6,
    };

    // ============================================================================
    // Utility Functions — Protocol Framing
    // ============================================================================

    /**
     * Reads a frame from socket with timeout.
     * Frame format: [4 bytes length][4 bytes type][variable payload]
     *
     * @param fd Socket file descriptor
     * @param out_type Output parameter for message type
     * @param out_payload Output buffer for payload (caller must allocate)
     * @param out_payload_len Output parameter for payload length
     * @return true on success, false on error/timeout
     */
    static bool readFrame(int fd, uint32_t &out_type, char *out_payload, size_t &out_payload_len)
    {
        // Read frame header (8 bytes)
        unsigned char header[FRAME_HEADER_SIZE];
        ssize_t bytes_read = read(fd, header, FRAME_HEADER_SIZE);

        if (bytes_read != FRAME_HEADER_SIZE)
        {
            if (bytes_read == 0)
            {
                Logger::debug("[Worker] Client closed connection");
            }
            else if (bytes_read < 0)
            {
                Logger::error("[Worker] read() header failed");
            }
            else
            {
                Logger::error("[Worker] Partial frame header read");
            }
            return false;
        }

        // Parse frame header
        uint32_t payload_len = 0;
        std::memcpy(&payload_len, &header[0], 4);
        std::memcpy(&out_type, &header[4], 4);

        // Validate payload length
        if (payload_len > MAX_FRAME_SIZE - FRAME_HEADER_SIZE)
        {
            Logger::error("[Worker] Frame payload too large");
            return false;
        }

        // Read payload if present
        if (payload_len > 0)
        {
            bytes_read = read(fd, out_payload, payload_len);
            if (bytes_read != static_cast<ssize_t>(payload_len))
            {
                Logger::error("[Worker] Partial frame payload read");
                return false;
            }
        }

        out_payload_len = payload_len;
        return true;
    }

    /**
     * Sends a frame to socket with timeout.
     * Frame format: [4 bytes length][4 bytes type][variable payload]
     *
     * @param fd Socket file descriptor
     * @param type Message type
     * @param payload Payload buffer (may be null)
     * @param payload_len Payload length
     * @return true on success, false on error
     */
    static bool sendFrame(int fd, uint32_t type, const char *payload, size_t payload_len)
    {
        // Build frame header
        unsigned char header[FRAME_HEADER_SIZE];
        std::memcpy(&header[0], &payload_len, 4);
        std::memcpy(&header[4], &type, 4);

        // Send header
        ssize_t bytes_written = write(fd, header, FRAME_HEADER_SIZE);
        if (bytes_written != FRAME_HEADER_SIZE)
        {
            Logger::error("[Worker] Failed to write frame header");
            return false;
        }

        // Send payload if present
        if (payload_len > 0 && payload != nullptr)
        {
            bytes_written = write(fd, payload, payload_len);
            if (bytes_written != static_cast<ssize_t>(payload_len))
            {
                Logger::error("[Worker] Failed to write frame payload");
                return false;
            }
        }

        return true;
    }

    /**
     * Sends an error response to client.
     *
     * @param fd Socket file descriptor
     * @param error_code Error code enum
     * @param error_msg Human-readable error message
     */
    static void sendErrorResponse(int fd, ErrorCode error_code, const std::string &error_msg)
    {
        // Build error payload: [4 bytes error code][variable message]
        char payload[512];
        std::memcpy(&payload[0], &error_code, 4);
        std::memcpy(&payload[4], error_msg.c_str(), error_msg.length());

        size_t payload_len = 4 + error_msg.length();
        sendFrame(fd, MSG_TYPE_ERROR_RESPONSE, payload, payload_len);
    }

    // ============================================================================
    // Phase 6: Complete Query Execution with Result Serialization
    // ============================================================================

    /**
     * Executes a SQL query and sends results back to client.
     *
     * @param fd Socket file descriptor
     * @param query The SQL query string
     * @param officer_id Officer ID for logging
     */
    static void executeAndSendQuery(int fd, const std::string &query, int officer_id)
    {
        std::vector<std::vector<std::string>> results;

        JusticeFlow::ResultCode query_result =
            ipc::IpcManager::getInstance().executeQuery(query, results);

        if (query_result != JusticeFlow::ResultCode::OK)
        {
            char log_buf[256];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[Worker] Query execution failed for officer %d", officer_id);
            Logger::error(log_buf);

            sendErrorResponse(fd, ERR_QUERY_FAILED, "Query execution failed");
            return;
        }

        // Serialize results into frame payload
        // Format: [4 bytes row count][4 bytes col count for each row][cells as null-terminated strings]

        char payload[MAX_FRAME_SIZE];
        size_t offset = 0;

        uint32_t row_count = results.size();
        std::memcpy(&payload[offset], &row_count, 4);
        offset += 4;

        // For each row, write column count and then each cell as null-terminated string
        for (const auto &row : results)
        {
            uint32_t col_count = row.size();
            std::memcpy(&payload[offset], &col_count, 4);
            offset += 4;

            for (const auto &cell : row)
            {
                // Write cell as null-terminated string
                size_t cell_len = cell.length() + 1; // +1 for null terminator

                if (offset + cell_len > MAX_FRAME_SIZE)
                {
                    Logger::error("[Worker] Result payload too large, truncating");
                    break;
                }

                std::memcpy(&payload[offset], cell.c_str(), cell_len);
                offset += cell_len;
            }
        }

        // Send results frame
        if (!sendFrame(fd, MSG_TYPE_QUERY_RESULT, payload, offset))
        {
            Logger::error("[Worker] Failed to send query results");
            return;
        }

        char log_buf[256];
        std::snprintf(log_buf, sizeof(log_buf),
                      "[Worker] Query executed successfully for officer %d. Rows: %u",
                      officer_id, row_count);
        Logger::info(log_buf);
    }

    // ============================================================================
    // Phase 6: Complete Authentication Handshake
    // ============================================================================

    /**
     * Handles incoming login request and establishes session.
     *
     * Protocol:
     * 1. Client sends: MSG_TYPE_AUTH_REQUEST with [cnic][password] (null-terminated strings)
     * 2. Server validates with AuthManager
     * 3. Server sends: MSG_TYPE_AUTH_RESPONSE with token
     * 4. Client must include token in all subsequent requests
     *
     * @param fd Socket file descriptor
     * @param thread_id Worker thread ID for logging
     * @param payload Auth request payload
     * @param payload_len Payload length
     * @return SessionContext if successful, empty if failed
     */
    static auth::SessionContext handleLoginRequest(int fd, int thread_id,
                                                   const char *payload, size_t payload_len)
    {
        auth::SessionContext empty_session = {};

        // Parse payload: [cnic null term][password null term]
        const char *cnic_start = payload;
        const char *cnic_end = static_cast<const char *>(std::memchr(payload, '\0', payload_len));

        if (!cnic_end)
        {
            Logger::error("[Worker] Malformed auth request - missing cnic null terminator");
            sendErrorResponse(fd, ERR_PROTOCOL_ERROR, "Malformed auth request");
            return empty_session;
        }

        std::string cnic(cnic_start, cnic_end);
        const char *password_start = cnic_end + 1;
        size_t remaining = payload_len - (password_start - payload);

        const char *password_end = static_cast<const char *>(std::memchr(password_start, '\0', remaining));
        if (!password_end)
        {
            Logger::error("[Worker] Malformed auth request - missing password null terminator");
            sendErrorResponse(fd, ERR_PROTOCOL_ERROR, "Malformed auth request");
            return empty_session;
        }

        std::string password(password_start, password_end);

        // Call AuthManager::login()
        auth::SessionContext session;
        JusticeFlow::ResultCode auth_result =
            auth::AuthManager::getInstance().login(cnic, password, session);

        if (auth_result != JusticeFlow::ResultCode::OK)
        {
            char log_buf[256];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[Worker %d] Login failed for CNIC: %.8s...", thread_id, cnic.c_str());
            Logger::error(log_buf);

            sendErrorResponse(fd, ERR_AUTH_FAILED, "Authentication failed");
            return empty_session;
        }

        // Send token back to client
        std::string token_response = session.token;
        sendFrame(fd, MSG_TYPE_AUTH_RESPONSE, token_response.c_str(), token_response.length());

        char log_buf[256];
        std::snprintf(log_buf, sizeof(log_buf),
                      "[Worker %d] Officer %d authenticated successfully",
                      thread_id, session.officer_id);
        Logger::info(log_buf);

        return session;
    }

    // ============================================================================
    // Phase 6: Complete Request Loop
    // ============================================================================

    /**
     * Main event loop for processing client requests.
     *
     * Protocol flow:
     * 1. Client connects
     * 2. Client sends MSG_TYPE_AUTH_REQUEST with credentials
     * 3. Server responds with MSG_TYPE_AUTH_RESPONSE (token)
     * 4. Client sends MSG_TYPE_QUERY_REQUEST with (token, query)
     * 5. Server responds with MSG_TYPE_QUERY_RESULT or MSG_TYPE_ERROR_RESPONSE
     * 6. Repeat steps 4-5 until client closes or sends MSG_TYPE_LOGOUT
     *
     * @param fd Socket file descriptor
     * @param thread_id Worker thread ID
     * @param officer_id Officer ID (populated after login)
     */
    static void processClientRequests(int fd, int thread_id)
    {
        auth::SessionContext session = {};
        bool authenticated = false;

        char payload_buffer[MAX_FRAME_SIZE];

        while (true)
        {
            uint32_t msg_type = 0;
            size_t payload_len = 0;

            // Read next frame from client
            if (!readFrame(fd, msg_type, payload_buffer, payload_len))
            {
                // Client closed or read error
                break;
            }

            // Handle AUTH_REQUEST
            if (msg_type == MSG_TYPE_AUTH_REQUEST)
            {
                if (authenticated)
                {
                    char log_buf[128];
                    std::snprintf(log_buf, sizeof(log_buf),
                                  "[Worker %d] Already authenticated, rejecting duplicate login",
                                  thread_id);
                    Logger::error(log_buf);
                    sendErrorResponse(fd, ERR_PROTOCOL_ERROR, "Already authenticated");
                    continue;
                }

                session = handleLoginRequest(fd, thread_id, payload_buffer, payload_len);
                if (session.officer_id > 0)
                {
                    authenticated = true;

                    // Register session in threading session_manager for monitoring
                    SessionContext ctx = {
                        session.officer_id,
                        fd,
                        session.login_timestamp};
                    SessionManager::getInstance().register_session(thread_id, ctx);
                }
                continue;
            }

            // All other message types require authentication
            if (!authenticated)
            {
                Logger::error("[Worker] Received request without authentication");
                sendErrorResponse(fd, ERR_AUTH_FAILED, "Not authenticated");
                continue;
            }

            // Handle QUERY_REQUEST
            if (msg_type == MSG_TYPE_QUERY_REQUEST)
            {
                // CRITICAL FIX #3.1: Validate session hasn't expired
                auth::SessionContext validated_session;
                JusticeFlow::ResultCode validate_result =
                    auth::AuthManager::getInstance().validateToken(session.token, validated_session);

                if (validate_result != JusticeFlow::ResultCode::OK)
                {
                    char log_buf[128];
                    std::snprintf(log_buf, sizeof(log_buf),
                                  "[Worker %d] Session validation failed for officer %d",
                                  thread_id, session.officer_id);
                    Logger::error(log_buf);

                    if (validate_result == JusticeFlow::ResultCode::SESSION_EXPIRED)
                    {
                        sendErrorResponse(fd, ERR_SESSION_EXPIRED, "Session expired");
                    }
                    else
                    {
                        sendErrorResponse(fd, ERR_AUTH_FAILED, "Session validation failed");
                    }
                    break;
                }

                // Copy validated session back
                session = validated_session;

                // Extract query from payload (null-terminated string)
                std::string query(payload_buffer, payload_len);

                // Remove trailing null if present
                if (!query.empty() && query.back() == '\0')
                {
                    query.pop_back();
                }

                if (query.empty())
                {
                    Logger::error("[Worker] Empty query received");
                    sendErrorResponse(fd, ERR_PROTOCOL_ERROR, "Empty query");
                    continue;
                }

                char log_buf[512];
                std::snprintf(log_buf, sizeof(log_buf),
                              "[Worker %d] Officer %d executing query: %.50s...",
                              thread_id, session.officer_id, query.c_str());
                Logger::debug(log_buf);

                // The Critical Section — acquire DB slot, execute query, send results
                {
                    SemGuard gate_lock(ConnectionGate::getInstance().getSemaphore());

                    char db_log[256];
                    std::snprintf(db_log, sizeof(db_log),
                                  "[Worker %d] Officer %d acquired DB slot",
                                  thread_id, session.officer_id);
                    Logger::info(db_log);

                    // COMPLETED TODO Phase 6: Execute query and serialize results
                    executeAndSendQuery(fd, query, session.officer_id);

                } // SemGuard released here — DB slot available for next officer

                // Refresh session idle timeout after successful request
                JusticeFlow::ResultCode refresh_result =
                    auth::AuthManager::getInstance().refreshSession(session);

                if (refresh_result != JusticeFlow::ResultCode::OK)
                {
                    char log_buf[128];
                    std::snprintf(log_buf, sizeof(log_buf),
                                  "[Worker %d] Session refresh failed for officer %d",
                                  thread_id, session.officer_id);
                    Logger::warning(log_buf);
                    // Continue anyway — session may still be valid
                }

                continue;
            }

            // Handle LOGOUT
            if (msg_type == MSG_TYPE_LOGOUT)
            {
                JusticeFlow::ResultCode logout_result =
                    auth::AuthManager::getInstance().logout(session);

                if (logout_result == JusticeFlow::ResultCode::OK)
                {
                    char log_buf[128];
                    std::snprintf(log_buf, sizeof(log_buf),
                                  "[Worker %d] Officer %d logged out gracefully",
                                  thread_id, session.officer_id);
                    Logger::info(log_buf);
                }

                // Close connection after logout
                break;
            }

            // Unknown message type
            Logger::error("[Worker] Unknown message type");
            sendErrorResponse(fd, ERR_PROTOCOL_ERROR, "Unknown message type");
        }

        // Cleanup
        if (authenticated)
        {
            SessionManager::getInstance().unregister_session(thread_id);

            char log_buf[128];
            std::snprintf(log_buf, sizeof(log_buf),
                          "[Worker %d] Officer %d session cleaned up",
                          thread_id, session.officer_id);
            Logger::info(log_buf);
        }
    }

} // anonymous namespace

// ============================================================================
// Worker::process_task - Entry Point
// ============================================================================

void Worker::process_task(WorkerTask task)
{
    try
    {
        char log_buf[128];
        std::snprintf(log_buf, sizeof(log_buf),
                      "[Worker %d] Processing new connection", task.thread_id);
        Logger::info(log_buf);

        // Main event loop for this client connection
        // Handles: login -> multiple queries -> logout
        processClientRequests(task.client_socket_fd, task.thread_id);

        // Close connection
        close(task.client_socket_fd);

        std::snprintf(log_buf, sizeof(log_buf),
                      "[Worker %d] Connection closed and cleaned up", task.thread_id);
        Logger::info(log_buf);
    }
    catch (const std::exception &e)
    {
        // Architecture Mandate: Top-level catch to prevent thread death on crash
        char crash_buf[256];
        std::snprintf(crash_buf, sizeof(crash_buf),
                      "[Worker %d] CRASH INTERCEPTED: %s", task.thread_id, e.what());
        Logger::error(crash_buf);

        // Ensure cleanup on exception
        SessionManager::getInstance().unregister_session(task.thread_id);
        if (close(task.client_socket_fd) == -1 && errno != EBADF)
        {
            Logger::error("[Worker] close() failed on exception path");
        }
    }
    catch (...)
    {
        char unknown_buf[128];
        std::snprintf(unknown_buf, sizeof(unknown_buf),
                      "[Worker %d] Unknown FATAL exception caught", task.thread_id);
        Logger::error(unknown_buf);

        SessionManager::getInstance().unregister_session(task.thread_id);
        if (close(task.client_socket_fd) == -1 && errno != EBADF)
        {
            Logger::error("[Worker] close() failed on exception path");
        }
    }
}