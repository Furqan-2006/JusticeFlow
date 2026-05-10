// src/main.h
#pragma once

#include <memory>
#include <csignal>
#include <atomic>
#include <string>

// Forward declarations
namespace cli_interface
{
    class CLIInterface;
}

namespace api_gateway
{
    class ApiGateway;
}

namespace system_layer
{
    class SystemManager;
}

/**
 * @brief Global orchestrator instance (for signal handlers)
 */
extern std::unique_ptr<class JusticeFlowOrchestrator> g_orchestrator;

/**
 * @brief Signal handler for SIGINT (Ctrl+C)
 */
void handle_sigint(int sig);

/**
 * @brief Signal handler for SIGTERM
 */
void handle_sigterm(int sig);

/**
 * @brief Signal handler for SIGHUP (reload config)
 */
void handle_sighup(int sig);

/**
 * @brief Main entry point
 */
int main(int argc, char *argv[]);