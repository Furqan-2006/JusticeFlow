#pragma once
/**
 * @file command_router.h
 * @brief Placeholder for future command dispatch.
 *
 * In Option A, the dashboard is served by ipc::DomainSocket which already
 * contains its own dispatch() for the read-only dashboard catalogue.
 *
 * This file exists as part of the api_gateway module structure so future work
 * can move/centralize routing here without changing include paths.
 */

namespace api_gateway
{
    class CommandRouter
    {
    public:
        CommandRouter() = default;
        ~CommandRouter() = default;

        CommandRouter(const CommandRouter &) = delete;
        CommandRouter &operator=(const CommandRouter &) = delete;
    };
} // namespace api_gateway