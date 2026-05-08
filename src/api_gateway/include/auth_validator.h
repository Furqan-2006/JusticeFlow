#pragma once
/**
 * @file auth_validator.h
 * @brief Placeholder for future auth checks at the gateway boundary.
 *
 * Current dashboard gateway commands are read-only and served by ipc::DomainSocket.
 * When you add write commands over the same socket, ApiGateway can enforce auth
 * here (token validation, rank checks, duty checks) via auth::AuthManager.
 */

namespace api_gateway
{
    class AuthValidator
    {
    public:
        AuthValidator() = default;
        ~AuthValidator() = default;

        AuthValidator(const AuthValidator &) = delete;
        AuthValidator &operator=(const AuthValidator &) = delete;
    };
} // namespace api_gateway