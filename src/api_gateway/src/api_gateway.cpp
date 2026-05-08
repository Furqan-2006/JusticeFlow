#include "../include/api_gateway.h"

#include "common/logger.h"

// Hidden implementation detail:
#include "os_layer/ipc/include/domain_socket.h"

#include <new> // std::nothrow

namespace api_gateway
{
    struct ApiGateway::Impl
    {
        explicit Impl(const std::string &path) : server(path) {}
        ipc::DomainSocket server;
    };

    ApiGateway::ApiGateway(ApiGatewayConfig cfg)
        : cfg_(std::move(cfg)), impl_(nullptr)
    {
        impl_ = new (std::nothrow) Impl(cfg_.socket_path);
        if (!impl_)
        {
            // If allocation fails, start() will return FILE_SYSTEM_ERROR.
            Logger::error("[ApiGateway] Failed to allocate Impl");
        }
    }

    ApiGateway::~ApiGateway()
    {
        stop();
        delete impl_;
        impl_ = nullptr;
    }

    JusticeFlow::ResultCode ApiGateway::start()
    {
        if (!impl_)
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;

        Logger::info("[ApiGateway] start()");
        return impl_->server.start();
    }

    void ApiGateway::stop()
    {
        if (!impl_)
            return;

        Logger::info("[ApiGateway] stop()");
        impl_->server.stop();
    }

    bool ApiGateway::isRunning() const
    {
        return impl_ ? impl_->server.isRunning() : false;
    }

} // namespace api_gateway