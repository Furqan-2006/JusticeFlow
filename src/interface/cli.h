// src/interface/cli.h
#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <iostream>

#include "common/common.h"
#include "system.h"
#include "api_gateway.h"

namespace cli_interface
{

    /**
     * @class CommandContext
     * @brief Session state for CLI user
     */
    class CommandContext
    {
    public:
        int officer_id = -1;
        std::string cnic;
        std::string username;
        std::string session_token;
        JusticeFlow::OfficerRank rank = JusticeFlow::OfficerRank::CONSTABLE;
        std::vector<std::string> permissions;
        std::chrono::system_clock::time_point login_time;

        bool isLoggedIn() const { return officer_id > 0 && !session_token.empty(); }

        bool hasPermission(const std::string &action) const;
    };

    /**
     * @struct CommandResult
     * @brief Unified result type for all CLI commands
     */
    struct CommandResult
    {
        bool success = false;
        std::string message;
        int error_code = 0;
        std::string data; // JSON or table formatted output

        static CommandResult ok(const std::string &msg = "", const std::string &data = "")
        {
            return {true, msg, 0, data};
        }

        static CommandResult error(const std::string &msg, int code = 1)
        {
            return {false, msg, code, ""};
        }
    };

    /**
     * @enum OutputFormat
     * @brief Output formatting options
     */
    enum class OutputFormat
    {
        TABLE, // Human-readable ASCII table
        JSON,  // Machine-readable JSON
        CSV,   // Bulk export CSV
        YAML   // Configuration YAML
    };

    /**
     * @class CLIInterface
     * @brief Primary user interaction layer
     *
     * Design: Command Pattern + Dispatcher
     * - Parses user input (command + subcommand + args + flags)
     * - Routes to appropriate handler
     * - Manages session state (login/logout)
     * - Formats output
     * - Logs all operations for audit trail
     */
    class CLIInterface
    {
    public:
        /**
         * @brief Constructor
         * @param sys Reference to SystemManager singleton
         * @param db_config Database configuration for direct queries
         */
        explicit CLIInterface(system_layer::SystemManager &sys,
                              const JusticeFlow::DBConfig &db_config);
        ~CLIInterface();

        // Deleted copy/move
        CLIInterface(const CLIInterface &) = delete;
        CLIInterface &operator=(const CLIInterface &) = delete;

        // ====================================================================
        // LIFECYCLE
        // ====================================================================

        /**
         * @brief Start interactive REPL
         * Blocks until user exits (type 'exit' or 'quit')
         * @return Exit code (0 = success)
         */
        int start();

        /**
         * @brief Execute a single command (non-blocking)
         * Used for testing and programmatic invocation
         * @param input Raw input line (e.g., "case list --office 5")
         * @return CommandResult with success/error status
         */
        CommandResult executeCommand(const std::string &input);

        /**
         * @brief Get current session context
         */
        const CommandContext &getContext() const { return m_context; }

        /**
         * @brief Set output format
         */
        void setOutputFormat(OutputFormat fmt) { m_output_format = fmt; }

        /**
         * @brief Get output format
         */
        OutputFormat getOutputFormat() const { return m_output_format; }

    private:
        // ====================================================================
        // REPL INFRASTRUCTURE
        // ====================================================================

        void printWelcome();
        void printPrompt();
        std::string readLine();
        void printHelp(const std::string &topic = "");
        void printCommandHelp(const std::string &command);

        // ====================================================================
        // COMMAND PARSING & DISPATCHING
        // ====================================================================

        std::vector<std::string> parseCommand(const std::string &input);

        CommandResult dispatchCommand(const std::vector<std::string> &tokens);

        // ====================================================================
        // SESSION & AUTH COMMANDS
        // ====================================================================

        CommandResult handleAuthCommand(const std::vector<std::string> &args);
        CommandResult handleLoginCommand(const std::vector<std::string> &args);
        CommandResult handleLogoutCommand(const std::vector<std::string> &args);
        CommandResult handleWhoamiCommand(const std::vector<std::string> &args);

        // ====================================================================
        // CASE MANAGEMENT COMMANDS
        // ====================================================================

        CommandResult handleCaseCommand(const std::vector<std::string> &args);
        CommandResult handleCaseListCommand(const std::vector<std::string> &args);
        CommandResult handleCaseCreateCommand(const std::vector<std::string> &args);
        CommandResult handleCaseViewCommand(const std::vector<std::string> &args);
        CommandResult handleCaseUpdateCommand(const std::vector<std::string> &args);
        CommandResult handleCaseCloseCommand(const std::vector<std::string> &args);
        CommandResult handleCaseReopenCommand(const std::vector<std::string> &args);

        // ====================================================================
        // EVIDENCE COMMANDS
        // ====================================================================

        CommandResult handleEvidenceCommand(const std::vector<std::string> &args);
        CommandResult handleEvidenceListCommand(const std::vector<std::string> &args);
        CommandResult handleEvidenceAddCommand(const std::vector<std::string> &args);
        CommandResult handleEvidenceViewCommand(const std::vector<std::string> &args);

        // ====================================================================
        // WARRANT COMMANDS
        // ====================================================================

        CommandResult handleWarrantCommand(const std::vector<std::string> &args);
        CommandResult handleWarrantListCommand(const std::vector<std::string> &args);
        CommandResult handleWarrantIssueCommand(const std::vector<std::string> &args);
        CommandResult handleWarrantServeCommand(const std::vector<std::string> &args);
        CommandResult handleWarrantRevokeCommand(const std::vector<std::string> &args);

        // ====================================================================
        // ARREST COMMANDS
        // ====================================================================

        CommandResult handleArrestCommand(const std::vector<std::string> &args);
        CommandResult handleArrestListCommand(const std::vector<std::string> &args);
        CommandResult handleArrestRegisterCommand(const std::vector<std::string> &args);
        CommandResult handleArrestReleaseCommand(const std::vector<std::string> &args);

        // ====================================================================
        // BAIL COMMANDS
        // ====================================================================

        CommandResult handleBailCommand(const std::vector<std::string> &args);
        CommandResult handleBailListCommand(const std::vector<std::string> &args);
        CommandResult handleBailApplyCommand(const std::vector<std::string> &args);
        CommandResult handleBailApproveCommand(const std::vector<std::string> &args);
        CommandResult handleBailDenyCommand(const std::vector<std::string> &args);

        // ====================================================================
        // FORENSIC COMMANDS
        // ====================================================================

        CommandResult handleForensicCommand(const std::vector<std::string> &args);
        CommandResult handleForensicListCommand(const std::vector<std::string> &args);
        CommandResult handleForensicCreateCommand(const std::vector<std::string> &args);

        // ====================================================================
        // AUDIT COMMANDS
        // ====================================================================

        CommandResult handleAuditCommand(const std::vector<std::string> &args);
        CommandResult handleAuditQueryCommand(const std::vector<std::string> &args);
        CommandResult handleAuditExportCommand(const std::vector<std::string> &args);

        // ====================================================================
        // PERSONNEL COMMANDS
        // ====================================================================

        CommandResult handlePersonnelCommand(const std::vector<std::string> &args);
        CommandResult handlePersonnelListCommand(const std::vector<std::string> &args);
        CommandResult handlePersonnelViewCommand(const std::vector<std::string> &args);

        // ====================================================================
        // DUTY COMMANDS
        // ====================================================================

        CommandResult handleDutyCommand(const std::vector<std::string> &args);
        CommandResult handleDutyScheduleCommand(const std::vector<std::string> &args);
        CommandResult handleDutyStartCommand(const std::vector<std::string> &args);
        CommandResult handleDutyEndCommand(const std::vector<std::string> &args);
        CommandResult handleDutyRosterCommand(const std::vector<std::string> &args);

        // ====================================================================
        // SYSTEM COMMANDS
        // ====================================================================

        CommandResult handleSystemCommand(const std::vector<std::string> &args);
        CommandResult handleStatusCommand(const std::vector<std::string> &args);
        CommandResult handleHealthCommand(const std::vector<std::string> &args);
        CommandResult handleVersionCommand(const std::vector<std::string> &args);

        // ====================================================================
        // BUILT-IN COMMANDS
        // ====================================================================

        CommandResult handleHelpCommand(const std::vector<std::string> &args);
        CommandResult handleExitCommand(const std::vector<std::string> &args);
        CommandResult handleHistoryCommand(const std::vector<std::string> &args);
        CommandResult handleClearCommand(const std::vector<std::string> &args);

        // ====================================================================
        // OUTPUT FORMATTING
        // ====================================================================

        std::string formatAsTable(const std::string &title,
                                  const std::vector<std::vector<std::string>> &rows,
                                  const std::vector<std::string> &headers);
        std::string formatAsJson(const std::string &data);
        std::string formatAsCsv(const std::vector<std::vector<std::string>> &rows,
                                const std::vector<std::string> &headers);

        // ====================================================================
        // ARGUMENT PARSING
        // ====================================================================

        struct ParsedArgs
        {
            std::vector<std::string> positional;
            std::map<std::string, std::string> flags; // --key value
            std::map<std::string, bool> bools;        // --flag (no value)
        };

        ParsedArgs parseArgs(const std::vector<std::string> &tokens, size_t start);

        // ====================================================================
        // VALIDATION & PERMISSION CHECKS
        // ====================================================================

        CommandResult requireLogin();
        CommandResult requirePermission(const std::string &action);
        CommandResult validateSession();

        // ====================================================================
        // MEMBERS
        // ====================================================================

        system_layer::SystemManager &m_system;
        JusticeFlow::DBConfig m_db_config;
        PGconn *m_db_conn = nullptr; // Persistent DB connection for CLI session

        CommandContext m_context;
        OutputFormat m_output_format = OutputFormat::TABLE;
        bool m_running = true;
        std::vector<std::string> m_history;
    };

} // namespace cli_interface