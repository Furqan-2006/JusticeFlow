#pragma once
/**
 * @file command_router.h
 * @brief Gateway commands that go beyond the read-only catalogue.
 *
 * Installed on the DomainSocket as its extension handler. It
 *   - serves LOGIN, LOGOUT and WHOAMI;
 *   - serves the write commands REGISTER_FIR and ADD_EVIDENCE, which run as the
 *     officer who owns the token so the audit trail names the real actor;
 *   - rejects data reads (cases, analytics, workload) that carry no valid
 *     session token, so nothing sensitive is served anonymously;
 *   - leaves PING and the system-status commands open, and lets everything
 *     it does not own fall through to the built-in handlers.
 */

#include <map>
#include <mutex>
#include <string>

#include "auth_validator.h"
#include "os_layer/ipc/include/socket_request.h"

namespace api_gateway
{
    class CommandRouter
    {
    public:
        CommandRouter() = default;
        ~CommandRouter() = default;

        CommandRouter(const CommandRouter &) = delete;
        CommandRouter &operator=(const CommandRouter &) = delete;

        /// Extension-handler entry point: true = `out` holds the answer.
        bool handle(const ipc::SocketRequest &req, ipc::SocketResponse &out);

    private:
        ipc::SocketResponse login(const ipc::SocketRequest &req);
        ipc::SocketResponse logout(const ipc::SocketRequest &req);
        ipc::SocketResponse whoami(const ipc::SocketRequest &req);
        ipc::SocketResponse registerFir(const ipc::SocketRequest &req);
        ipc::SocketResponse addEvidence(const ipc::SocketRequest &req);
        ipc::SocketResponse caseEvidence(const ipc::SocketRequest &req);
        ipc::SocketResponse issueWarrant(const ipc::SocketRequest &req);
        ipc::SocketResponse caseWarrants(const ipc::SocketRequest &req);

        bool isLockedOut(const std::string &cnic);
        void recordFailure(const std::string &cnic);
        void clearFailures(const std::string &cnic);

        AuthValidator validator_;

        struct Attempts
        {
            int failures{0};
            long window_start{0};
            long locked_until{0};
        };
        std::mutex attempts_mutex_;
        std::map<std::string, Attempts> attempts_;
    };
} // namespace api_gateway
