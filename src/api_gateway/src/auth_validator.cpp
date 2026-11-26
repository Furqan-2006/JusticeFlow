#include "../include/auth_validator.h"

#include "system/shr_infra/auth/include/auth_manager.h"

namespace api_gateway
{
    JusticeFlow::ResultCode AuthValidator::validate(const std::string &token,
                                                    JusticeFlow::SessionContext &out) const
    {
        if (token.empty())
            return JusticeFlow::ResultCode::INVALID_INPUT;
        return auth::AuthManager::getInstance().validateToken(token, out);
    }
} // namespace api_gateway
