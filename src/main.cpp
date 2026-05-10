// src/main.cpp
#include "main.h"
#include "system/system.h"
#include "api_gateway/include/api_gateway.h"
#include "interface/cli.h"
#include "common/logger.h"
#include "common/constants.h"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <thread>

// Global orchestrator
std::unique_ptr<class JusticeFlowOrchestrator> g_orchestrator;

/**
 * @brief Central Orchestrator class
 * Manages lifecycle of all subsystems
 */
class JusticeFlowOrchestrator
{
public:
    explicit JusticeFlowOrchestrator(const JusticeFlow::DBConfig &db_config)
        : m_db_config(db_config) {}

    ~JusticeFlowOrchestrator()
    {
        shutdown();
    }

    // Deleted copy/move
    JusticeFlowOrchestrator(const JusticeFlowOrchestrator &) = delete;
    JusticeFlowOrchestrator &operator=(const JusticeFlowOrchestrator &) = delete;

    // ====================================================================
    // LIFECYCLE METHODS
    // ====================================================================

    /**
     * Validate prerequisites
     */
    bool validate()
    {
        Logger::info("Validating system prerequisites...");

        // Check database connectivity
        PGconn *test_conn = PQconnectdb(m_db_config.toConnectionString().c_str());
        if (PQstatus(test_conn) != CONNECTION_OK)
        {
            Logger::error("Database connection test failed");
            PQfinish(test_conn);
            return false;
        }
        PQfinish(test_conn);
        Logger::info("✓ Database connectivity verified");

        return true;
    }

    /**
     * Initialize all subsystems
     */
    bool initialize()
    {
        try
        {
            Logger::info("Initializing JusticeFlow system...");

            // ================================================================
            // PHASE 1: Initialize SystemManager
            // ================================================================
            Logger::info("Phase 1: Initializing SystemManager...");

            system_layer::SystemInitConfig sys_config;
            
            std::string conn_str = m_db_config.toConnectionString();
            sys_config.audit_db_conninfo = conn_str.c_str();

            auto &sys_mgr = system_layer::SystemManager::getInstance();
            auto init_result = sys_mgr.init(sys_config);

            if (!init_result.ok())
            {
                Logger::error("SystemManager initialization failed");
                return false;
            }

            Logger::info("✓ SystemManager initialized");

            // ================================================================
            // PHASE 2: Initialize API Gateway
            // ================================================================
            Logger::info("Phase 2: Initializing API Gateway...");

            api_gateway::ApiGatewayConfig api_cfg;
            api_cfg.socket_path = "/tmp/justiceflow.sock";

            m_api_gateway = std::make_unique<api_gateway::ApiGateway>(api_cfg);
            auto api_rc = m_api_gateway->start();

            if (api_rc != JusticeFlow::ResultCode::OK)
            {
                Logger::error(std::string(("API Gateway startup failed: code=" + std::to_string(static_cast<int>(api_rc)))).c_str());
                return false;
            }

            Logger::info(std::string(("✓ API Gateway listening on " + api_cfg.socket_path)).c_str());

            // ================================================================
            // PHASE 3: CLI is ready (but not started yet)
            // ================================================================
            Logger::info("Phase 3: CLI interface ready");

            m_initialized = true;
            return true;
        }
        catch (const std::exception &e)
        {
            Logger::error(std::string(("Initialization exception: " + std::string(e.what()))).c_str());
            return false;
        }
    }

    /**
     * Run the CLI REPL
     */
    int run()
    {
        if (!m_initialized)
        {
            Logger::error("System not initialized");
            return 1;
        }

        try
        {
            auto &sys_mgr = system_layer::SystemManager::getInstance();
            cli_interface::CLIInterface cli(sys_mgr, m_db_config);

            Logger::info("Starting CLI REPL...");
            return cli.start();
        }
        catch (const std::exception &e)
        {
            Logger::error(std::string(("CLI execution exception: " + std::string(e.what()))).c_str());
            return 1;
        }
    }

    /**
     * Graceful shutdown
     */
    void shutdown()
    {
        if (!m_initialized)
            return;

        Logger::info("Initiating graceful shutdown...");

        // Shutdown API Gateway
        if (m_api_gateway)
        {
            Logger::info("Stopping API Gateway...");
            m_api_gateway->stop();
            m_api_gateway.reset();
        }

        // Shutdown SystemManager
        auto &sys_mgr = system_layer::SystemManager::getInstance();
        sys_mgr.shutdown();
        Logger::info("✓ SystemManager shutdown complete");

        m_initialized = false;
        Logger::info("System shutdown complete");
    }

    bool isInitialized() const { return m_initialized; }

private:
    JusticeFlow::DBConfig m_db_config;
    std::unique_ptr<api_gateway::ApiGateway> m_api_gateway;
    bool m_initialized = false;
};

// ============================================================================
// SIGNAL HANDLERS
// ============================================================================

void handle_sigint(int sig)
{
    if (g_orchestrator)
    {
        Logger::info("Received SIGINT, shutting down gracefully...");
        g_orchestrator->shutdown();
    }
    exit(0);
}

void handle_sigterm(int sig)
{
    if (g_orchestrator)
    {
        Logger::info("Received SIGTERM, shutting down gracefully...");
        g_orchestrator->shutdown();
    }
    exit(0);
}

void handle_sighup(int sig)
{
    Logger::info("Received SIGHUP (reload config not yet implemented)");
}

// ============================================================================
// MAIN
// ============================================================================

void print_usage(const char *program_name)
{
    std::cerr << "Usage: " << program_name << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  -c, --config PATH       Path to config file\n"
              << "  -l, --log-level LEVEL   Log level (DEBUG, INFO, WARN, ERROR)\n"
              << "  -v, --version           Show version\n"
              << "  -h, --help              Show this help\n";
}

void print_version()
{
    std::cout << "JusticeFlow v1.0.0\n"
              << "Build: 2026-05-08\n";
}

struct CLIArgs
{
    std::string config_path = "/etc/justiceflow/config.yaml";
    std::string log_level = "INFO";
    bool show_version = false;
    bool show_help = false;
};

CLIArgs parse_args(int argc, char *argv[])
{
    CLIArgs args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg(argv[i]);

        if (arg == "-h" || arg == "--help")
        {
            args.show_help = true;
        }
        else if (arg == "-v" || arg == "--version")
        {
            args.show_version = true;
        }
        else if ((arg == "-c" || arg == "--config") && i + 1 < argc)
        {
            args.config_path = argv[++i];
        }
        else if ((arg == "-l" || arg == "--log-level") && i + 1 < argc)
        {
            args.log_level = argv[++i];
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << "\n";
            args.show_help = true;
        }
    }

    return args;
}

int main(int argc, char *argv[])
{
    try
    {
        // ====================================================================
        // PHASE 1: PARSE ARGUMENTS
        // ====================================================================
        auto args = parse_args(argc, argv);

        if (args.show_help)
        {
            print_usage(argv[0]);
            return 0;
        }

        if (args.show_version)
        {
            print_version();
            return 0;
        }

        // ====================================================================
        // PHASE 2: INITIALIZE LOGGER
        // ====================================================================
        if (!Logger::init("/var/log/justiceflow.log"))
        {
            // Fall back to stdout if log file fails
            Logger::init(nullptr);
        }

        Logger::info("========================================");
        Logger::info("JusticeFlow System Starting");
        Logger::info("Version: 1.0.0");
        Logger::info("========================================");

        // ====================================================================
        // PHASE 3: LOAD DATABASE CONFIGURATION
        // ====================================================================
        Logger::info("Loading database configuration...");

        JusticeFlow::DBConfig db_config;
        JusticeFlow::ResultCode config_rc = db_config.loadFromEnvironment();

        if (config_rc != JusticeFlow::ResultCode::OK)
        {
            Logger::error("Failed to load database configuration");
            return 1;
        }

        Logger::info(std::string(("✓ Database configured: host=" + db_config.host + ", db=" + db_config.dbname + ", user=" + db_config.user)).c_str());

        // ====================================================================
        // PHASE 4: CREATE ORCHESTRATOR
        // ====================================================================
        Logger::info("Creating system orchestrator...");

        g_orchestrator = std::make_unique<JusticeFlowOrchestrator>(db_config);

        // ====================================================================
        // PHASE 5: REGISTER SIGNAL HANDLERS
        // ====================================================================
        Logger::info("Registering signal handlers...");

        std::signal(SIGINT, handle_sigint);
        std::signal(SIGTERM, handle_sigterm);
        std::signal(SIGHUP, handle_sighup);
        std::signal(SIGPIPE, SIG_IGN);

        // ====================================================================
        // PHASE 6: VALIDATE PREREQUISITES
        // ====================================================================
        Logger::info("Validating prerequisites...");

        if (!g_orchestrator->validate())
        {
            Logger::error("Prerequisite validation failed");
            return 1;
        }

        Logger::info("✓ All prerequisites validated");

        // ====================================================================
        // PHASE 7: INITIALIZE SYSTEM
        // ====================================================================
        Logger::info("Initializing subsystems...");

        if (!g_orchestrator->initialize())
        {
            Logger::error("System initialization failed");
            return 1;
        }

        Logger::info("✓ All subsystems initialized and ready");

        // ====================================================================
        // PHASE 8: START CLI REPL
        // ====================================================================
        Logger::info("Starting CLI interface...");

        int exit_code = g_orchestrator->run();

        // ====================================================================
        // PHASE 9: CLEANUP
        // ====================================================================
        Logger::info("Shutting down system...");
        g_orchestrator.reset();

        Logger::info("========================================");
        Logger::info("JusticeFlow System Stopped");
        Logger::info("========================================");

        return exit_code;
    }
    catch (const std::exception &e)
    {
        Logger::error(std::string(("FATAL ERROR: " + std::string(e.what()))).c_str());
        return 1;
    }
    catch (...)
    {
        Logger::error("FATAL ERROR: Unknown exception");
        return 1;
    }
}