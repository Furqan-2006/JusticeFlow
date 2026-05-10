#pragma once
/**
 * @file api_gateway.h
 * @brief Public API Gateway module boundary.
 *
 * This module abstracts the OS-layer IPC details and only provides a
 * dashboard connection endpoint for the daemon to start/stop.
 *
 * IMPORTANT:
 * - This header intentionally does NOT expose ipc::SocketRequest, DomainSocket,
 *   or any other IPC internals.
 * - The on-wire protocol is documented in src/api_gateway/README.md.
 */

#include <string>
#include "common/constants.h"

namespace api_gateway
{
    struct ApiGatewayConfig
    {
        /// Filesystem path of the Unix domain socket exposed to the dashboard.
        std::string socket_path = "/tmp/justiceflow.sock";
    };

    class ApiGateway
    {
    public:
        explicit ApiGateway(ApiGatewayConfig cfg = {});
        ~ApiGateway();

        ApiGateway(const ApiGateway &) = delete;
        ApiGateway &operator=(const ApiGateway &) = delete;

        /**
         * @brief Start listening for dashboard connections.
         */
        JusticeFlow::ResultCode start();

        /**
         * @brief Stop listening and remove the socket file.
         * Idempotent.
         */
        void stop();

        /**
         * @return true if the gateway accept thread is running.
         */
        bool isRunning() const;

        const ApiGatewayConfig &config() const noexcept { return cfg_; }

    private:
        ApiGatewayConfig cfg_;

        // PIMPL: hides ipc::DomainSocket and other IPC internals.
        struct Impl;
        Impl *impl_;
    };

} // namespace api_gateway