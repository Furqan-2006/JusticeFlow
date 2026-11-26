// src/main.cpp
#include "main.h"
#include "system/system.h"
#include "api_gateway/include/api_gateway.h"
#include "interface/cli.h"
#include "common/logger.h"
#include "common/constants.h"
#include "os_layer/ipc/include/ipc_manager.h"

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <csignal>
#include <fstream>
#include <unistd.h>

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
            // PHASE 1b: OS layer — database connection and shared status table
            // The gateway answers status commands from these, so both must
            // exist before it starts accepting connections.
            // ================================================================
            auto &ipc_mgr = ipc::IpcManager::getInstance();

            if (ipc_mgr.connectDatabase() != JusticeFlow::ResultCode::OK)
            {
                Logger::error("IPC database connection failed");
                return false;
            }
            Logger::info("✓ IPC database connected");

            if (ipc_mgr.initSharedMemory() != JusticeFlow::ResultCode::OK)
            {
                Logger::error("Shared status table creation failed");
                return false;
            }
            Logger::info("✓ Shared status table ready");

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
     * Run headless: serve the API gateway until SIGINT or SIGTERM.
     *
     * Both signals are blocked in every thread (main() blocks them before any
     * thread exists), so sigwait() below is the single place they are received.
     * Shutdown then runs in normal context, not inside a signal handler.
     */
    int runHeadless()
    {
        if (!m_initialized)
        {
            Logger::error("System not initialized");
            return 1;
        }

        sigset_t stop_signals;
        sigemptyset(&stop_signals);
        sigaddset(&stop_signals, SIGINT);
        sigaddset(&stop_signals, SIGTERM);

        Logger::info("Headless mode: serving gateway until SIGINT/SIGTERM");

        int received = 0;
        sigwait(&stop_signals, &received);

        Logger::info(received == SIGTERM ? "Received SIGTERM, shutting down gracefully..."
                                         : "Received SIGINT, shutting down gracefully...");
        return 0;
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

        // Release OS-layer resources: no stale shared memory after a clean exit
        ipc::IpcManager::getInstance().destroySharedMemory();
        ipc::IpcManager::getInstance().disconnectDatabase();

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

void handle_sigint(int sig __attribute__((unused)))
{
    if (g_orchestrator)
    {
        Logger::info("Received SIGINT, shutting down gracefully...");
        g_orchestrator->shutdown();
    }
    exit(0);
}

void handle_sigterm(int sig __attribute__((unused)))
{
    if (g_orchestrator)
    {
        Logger::info("Received SIGTERM, shutting down gracefully...");
        g_orchestrator->shutdown();
    }
    exit(0);
}

void handle_sighup(int sig __attribute__((unused)))
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
              << "      --headless          No CLI; serve the API gateway until SIGINT/SIGTERM\n"
              << "      --pid-file PATH     Write the process id to PATH (removed on exit)\n"
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
    bool headless = false;
    std::string pid_file;
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
        else if (arg == "--headless")
        {
            args.headless = true;
        }
        else if (arg == "--pid-file" && i + 1 < argc)
        {
            args.pid_file = argv[++i];
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

        // Headless: block SIGINT/SIGTERM now, before any thread is created, so
        // every thread inherits the mask and runHeadless() receives them via sigwait.
        if (args.headless)
        {
            sigset_t stop_signals;
            sigemptyset(&stop_signals);
            sigaddset(&stop_signals, SIGINT);
            sigaddset(&stop_signals, SIGTERM);
            pthread_sigmask(SIG_BLOCK, &stop_signals, nullptr);
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

        if (!args.headless)
        {
            std::signal(SIGINT, handle_sigint);
            std::signal(SIGTERM, handle_sigterm);
        }
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

        if (!args.pid_file.empty())
        {
            std::ofstream pid_out(args.pid_file);
            pid_out << getpid() << "\n";
        }

        int exit_code = args.headless ? g_orchestrator->runHeadless()
                                      : g_orchestrator->run();

        // ====================================================================
        // PHASE 9: CLEANUP
        // ====================================================================
        Logger::info("Shutting down system...");
        g_orchestrator.reset();

        if (!args.pid_file.empty())
        {
            std::remove(args.pid_file.c_str());
        }

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