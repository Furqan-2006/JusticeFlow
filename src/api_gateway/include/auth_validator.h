#pragma once
/**
 * @file auth_validator.h
 * @brief Token check at the gateway boundary.
 *
 * Every command that reads case data or changes anything carries the session
 * token the daemon issued at LOGIN. The validator asks auth::AuthManager
 * whether that token is a live session and, if so, who it belongs to.
 */

#include <string>

#include "common/common.h"
#include "common/constants.h"

namespace api_gateway
{
    class AuthValidator
    {
    public:
        AuthValidator() = default;
        ~AuthValidator() = default;

        AuthValidator(const AuthValidator &) = delete;
        AuthValidator &operator=(const AuthValidator &) = delete;

        /**
         * @return OK and fills `out` when `token` is a live session;
         *         INVALID_INPUT when it is empty, AUTH_FAILED / SESSION_EXPIRED
         *         otherwise.
         */
        JusticeFlow::ResultCode validate(const std::string &token,
                                         JusticeFlow::SessionContext &out) const;
    };
} // namespace api_gateway
