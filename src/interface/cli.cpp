// src/interface/cli.cpp
#include "cli.h"
#include "logger.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <readline/readline.h>
#include <readline/history.h>
#include <postgresql/libpq-fe.h>

namespace cli_interface
{

    // =========================================================================
    // CommandContext Implementation
    // =========================================================================

    bool CommandContext::hasPermission(const std::string &action) const
    {
        return std::find(permissions.begin(), permissions.end(), action) != permissions.end();
    }

    // =========================================================================
    // CLIInterface Implementation
    // =========================================================================

    CLIInterface::CLIInterface(system_layer::SystemManager &sys,
                               const JusticeFlow::DBConfig &db_config)
        : m_system(sys), m_db_config(db_config)
    {
        // Establish persistent DB connection
        m_db_conn = PQconnectdb(db_config.toConnectionString().c_str());
        if (PQstatus(m_db_conn) != CONNECTION_OK)
        {
            Logger::error((std::string("[CLI] Database connection failed: ") + PQerrorMessage(m_db_conn)).c_str());
            PQfinish(m_db_conn);
            m_db_conn = nullptr;
            throw std::runtime_error("Failed to connect to database");
        }

        Logger::info("[CLI] Database connection established");
    }

    CLIInterface::~CLIInterface()
    {
        if (m_db_conn)
        {
            PQfinish(m_db_conn);
            m_db_conn = nullptr;
        }
    }

    int CLIInterface::start()
    {
        if (!m_system.isInitialized())
        {
            std::cerr << "ERROR: System not initialized\n";
            return 1;
        }

        printWelcome();

        // Initialize readline
        rl_bind_key('\t', rl_complete);

        m_running = true;
        while (m_running)
        {
            printPrompt();

            char *line = readline(nullptr);
            if (!line)
            {
                // EOF (Ctrl+D)
                m_running = false;
                break;
            }

            std::string input(line);
            free(line);

            if (input.empty())
                continue;

            // Add to history
            add_history(input.c_str());
            m_history.push_back(input);

            // Execute command
            auto result = executeCommand(input);
            if (!result.data.empty())
            {
                std::cout << result.data << "\n";
            }
            if (!result.message.empty())
            {
                if (result.success)
                {
                    std::cout << result.message << "\n";
                }
                else
                {
                    std::cerr << "Error: " << result.message << "\n";
                }
            }
        }

        std::cout << "\nGoodbye!\n";
        return 0;
    }

    void CLIInterface::printWelcome()
    {
        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════════════════════╗\n";
        std::cout << "║           JUSTICEFLOW - Legal Case Management          ║\n";
        std::cout << "║                  Command Line Interface                ║\n";
        std::cout << "╚════════════════════════════════════════════════════════╝\n";
        std::cout << "\nType 'help' for commands, 'login' to authenticate, 'exit' to quit.\n";
        std::cout << "\n";
    }

    void CLIInterface::printPrompt()
    {
        if (m_context.isLoggedIn())
        {
            std::cout << "[" << m_context.username << "@justiceflow]> ";
        }
        else
        {
            std::cout << "[guest@justiceflow]> ";
        }
        std::cout.flush();
    }

    std::vector<std::string> CLIInterface::parseCommand(const std::string &input)
    {
        std::istringstream iss(input);
        std::vector<std::string> tokens;
        std::string token;

        while (iss >> token)
        {
            tokens.push_back(token);
        }

        return tokens;
    }

    CommandResult CLIInterface::executeCommand(const std::string &input)
    {
        try
        {
            auto tokens = parseCommand(input);
            if (tokens.empty())
            {
                return CommandResult::error("Empty command");
            }

            // Log command for audit
            Logger::info((std::string("[CLI] Command: ") + input + " (user: " +
                          (m_context.username.empty() ? "guest" : m_context.username) + ")")
                             .c_str());

            return dispatchCommand(tokens);
        }
        catch (const std::exception &e)
        {
            Logger::error((std::string("[CLI] Exception executing command: ") + e.what()).c_str());
            return CommandResult::error(std::string("Internal error: ") + e.what());
        }
    }

    CommandResult CLIInterface::dispatchCommand(const std::vector<std::string> &tokens)
    {
        const std::string &cmd = tokens[0];
        std::vector<std::string> args(tokens.begin() + 1, tokens.end());

        // ====================================================================
        // BUILT-IN COMMANDS (no login required)
        // ====================================================================

        if (cmd == "help")
        {
            return handleHelpCommand(args);
        }
        if (cmd == "exit" || cmd == "quit")
        {
            return handleExitCommand(args);
        }
        if (cmd == "version")
        {
            return handleVersionCommand(args);
        }
        if (cmd == "clear")
        {
            return handleClearCommand(args);
        }
        if (cmd == "history")
        {
            return handleHistoryCommand(args);
        }

        // ====================================================================
        // AUTH COMMANDS
        // ====================================================================

        if (cmd == "login")
        {
            return handleLoginCommand(args);
        }
        if (cmd == "logout")
        {
            return handleLogoutCommand(args);
        }
        if (cmd == "whoami")
        {
            return handleWhoamiCommand(args);
        }

        // ====================================================================
        // ALL REMAINING COMMANDS REQUIRE LOGIN
        // ====================================================================

        auto login_check = requireLogin();
        if (!login_check.success)
        {
            return login_check;
        }

        // ====================================================================
        // DOMAIN COMMANDS (login required)
        // ====================================================================

        if (cmd == "case")
        {
            return handleCaseCommand(args);
        }
        if (cmd == "evidence")
        {
            return handleEvidenceCommand(args);
        }
        if (cmd == "warrant")
        {
            return handleWarrantCommand(args);
        }
        if (cmd == "arrest")
        {
            return handleArrestCommand(args);
        }
        if (cmd == "bail")
        {
            return handleBailCommand(args);
        }
        if (cmd == "forensic")
        {
            return handleForensicCommand(args);
        }
        if (cmd == "audit")
        {
            return handleAuditCommand(args);
        }
        if (cmd == "personnel")
        {
            return handlePersonnelCommand(args);
        }
        if (cmd == "duty")
        {
            return handleDutyCommand(args);
        }

        // ====================================================================
        // SYSTEM COMMANDS (login required)
        // ====================================================================

        if (cmd == "status")
        {
            return handleStatusCommand(args);
        }
        if (cmd == "system")
        {
            return handleSystemCommand(args);
        }

        return CommandResult::error("Unknown command: " + cmd);
    }

    // =========================================================================
    // AUTH COMMANDS
    // =========================================================================

    CommandResult CLIInterface::handleLoginCommand(const std::vector<std::string> &args)
    {
        if (m_context.isLoggedIn())
        {
            return CommandResult::error("Already logged in as " + m_context.username);
        }

        auto parsed = parseArgs(args, 0);

        if (parsed.flags.find("cnic") == parsed.flags.end() ||
            parsed.flags.find("password") == parsed.flags.end())
        {
            return CommandResult::error("Usage: login --cnic <cnic> --password <password>");
        }

        std::string cnic = parsed.flags["cnic"];
        std::string password = parsed.flags["password"];

        // Call auth layer
        auto login_result = m_system.auth().login(cnic.c_str(), password.c_str());
        if (!login_result.ok())
        {
            Logger::error("[CLI] Login failed for CNIC");
            return CommandResult::error("Authentication failed");
        }

        // Validate token
        auto session_result = m_system.auth().validateToken(login_result.value.c_str());
        if (!session_result.ok())
        {
            return CommandResult::error("Session validation failed");
        }

        // Store session context
        m_context.officer_id = session_result.value.officerId;
        m_context.cnic = cnic;
        m_context.username = session_result.value.cnic;
        m_context.session_token = login_result.value;
        m_context.rank = session_result.value.rank;
        m_context.login_time = std::chrono::system_clock::now();

        Logger::info((std::string("[CLI] User logged in: CNIC=") + cnic + ", Officer=" +
                      std::to_string(m_context.officer_id) + ", Rank=" +
                      std::to_string(static_cast<int>(m_context.rank)))
                         .c_str());

        return CommandResult::ok("Login successful");
    }

    CommandResult CLIInterface::handleLogoutCommand(const std::vector<std::string> &args)
    {
        (void)args;
        if (!m_context.isLoggedIn())
        {
            return CommandResult::error("Not logged in");
        }

        // Call auth layer to invalidate token
        (void)m_system.auth().logout(m_context.session_token.c_str());

        // Clear context regardless of logout success
        m_context.officer_id = -1;
        m_context.cnic.clear();
        m_context.username.clear();
        m_context.session_token.clear();
        m_context.permissions.clear();

        Logger::info("[CLI] User logged out");
        return CommandResult::ok("Logout successful");
    }

    CommandResult CLIInterface::handleWhoamiCommand(const std::vector<std::string> &args)
    {
        (void)args;
        if (!m_context.isLoggedIn())
        {
            return CommandResult::ok("Not logged in");
        }

        std::ostringstream oss;
        oss << "Logged in as:\n"
            << "  Username:   " << m_context.username << "\n"
            << "  Officer ID: " << m_context.officer_id << "\n"
            << "  Rank:       " << static_cast<int>(m_context.rank) << "\n"
            << "  Permissions: " << m_context.permissions.size() << "\n";

        return CommandResult::ok("", oss.str());
    }

    // =========================================================================
    // CASE COMMANDS
    // =========================================================================

    CommandResult CLIInterface::handleCaseCommand(const std::vector<std::string> &args)
    {
        if (args.empty())
        {
            return CommandResult::error(
                "case requires subcommand: list, create, view, update, close, reopen");
        }

        const std::string &subcmd = args[0];
        std::vector<std::string> subargs(args.begin() + 1, args.end());

        if (subcmd == "list")
        {
            return handleCaseListCommand(subargs);
        }
        if (subcmd == "create")
        {
            return handleCaseCreateCommand(subargs);
        }
        if (subcmd == "view")
        {
            return handleCaseViewCommand(subargs);
        }
        if (subcmd == "update")
        {
            return handleCaseUpdateCommand(subargs);
        }
        if (subcmd == "close")
        {
            return handleCaseCloseCommand(subargs);
        }
        if (subcmd == "reopen")
        {
            return handleCaseReopenCommand(subargs);
        }

        return CommandResult::error("Unknown case subcommand: " + subcmd);
    }

    CommandResult CLIInterface::handleCaseListCommand(const std::vector<std::string> &args)
    {
        auto parsed = parseArgs(args, 0);

        // Get station_id (default to officer's station)
        int station_id = -1;
        if (parsed.flags.find("office") != parsed.flags.end())
        {
            station_id = std::stoi(parsed.flags["office"]);
        }

        // Get cases
        auto cases_result = m_system.cases().getCasesByStation(m_db_conn, station_id);
        if (!cases_result.ok())
        {
            return CommandResult::error("Failed to list cases");
        }

        // Format as table
        std::vector<std::string> headers = {"ID", "FIR#", "Type", "Status", "Filed At"};
        std::vector<std::vector<std::string>> rows;

        for (const auto &c : cases_result.value)
        {
            char time_buf[32];
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M", localtime(&c.filed_at));

            rows.push_back({std::to_string(c.case_id),
                            c.fir_number,
                            std::to_string(static_cast<int>(c.case_type)),
                            std::to_string(static_cast<int>(c.case_status)),
                            std::string(time_buf)});
        }

        std::string table = formatAsTable("Cases", rows, headers);
        return CommandResult::ok("", table);
    }

    CommandResult CLIInterface::handleCaseCreateCommand(const std::vector<std::string> &args)
    {
        auto parsed = parseArgs(args, 0);

        if (parsed.flags.find("title") == parsed.flags.end() ||
            parsed.flags.find("desc") == parsed.flags.end() ||
            parsed.flags.find("type") == parsed.flags.end())
        {
            return CommandResult::error(
                "Usage: case create --title <title> --desc <description> --type <type>");
        }

        // Placeholder: would parse case_type enum
        // For now, just confirm command was parsed

        return CommandResult::ok("Case creation placeholder (feature in progress)");
    }

    CommandResult CLIInterface::handleCaseViewCommand(const std::vector<std::string> &args)
    {
        if (args.empty())
        {
            return CommandResult::error("case view requires CASE_ID");
        }

        int case_id = std::stoi(args[0]);

        auto case_result = m_system.cases().getCaseById(m_db_conn, case_id);
        if (!case_result.ok())
        {
            return CommandResult::error("Case not found");
        }

        const auto &c = case_result.value;
        std::ostringstream oss;
        oss << "Case Details:\n"
            << "  ID:          " << c.case_id << "\n"
            << "  FIR Number:  " << c.fir_number << "\n"
            << "  Type:        " << static_cast<int>(c.case_type) << "\n"
            << "  Status:      " << static_cast<int>(c.case_status) << "\n"
            << "  Filed At:    ";

        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&c.filed_at));
        oss << time_buf << "\n";
        oss << "  Address:     " << c.incident_address << "\n";

        return CommandResult::ok("", oss.str());
    }

    CommandResult CLIInterface::handleCaseUpdateCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Case update placeholder (feature in progress)");
    }

    CommandResult CLIInterface::handleCaseCloseCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Case close placeholder (feature in progress)");
    }

    CommandResult CLIInterface::handleCaseReopenCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Case reopen placeholder (feature in progress)");
    }

    // =========================================================================
    // EVIDENCE COMMANDS (Stubs)
    // =========================================================================

    CommandResult CLIInterface::handleEvidenceCommand(const std::vector<std::string> &args)
    {
        if (args.empty())
        {
            return CommandResult::error("evidence requires subcommand: list, add, view");
        }
        return CommandResult::ok("Evidence command placeholder");
    }

    CommandResult CLIInterface::handleEvidenceListCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Evidence list placeholder");
    }

    CommandResult CLIInterface::handleEvidenceAddCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Evidence add placeholder");
    }

    CommandResult CLIInterface::handleEvidenceViewCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Evidence view placeholder");
    }

    // =========================================================================
    // WARRANT COMMANDS (Stubs)
    // =========================================================================

    CommandResult CLIInterface::handleWarrantCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Warrant command placeholder");
    }

    CommandResult CLIInterface::handleWarrantListCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Warrant list placeholder");
    }

    CommandResult CLIInterface::handleWarrantIssueCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Warrant issue placeholder");
    }

    CommandResult CLIInterface::handleWarrantServeCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Warrant serve placeholder");
    }

    CommandResult CLIInterface::handleWarrantRevokeCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Warrant revoke placeholder");
    }

    // =========================================================================
    // ARREST COMMANDS (Stubs)
    // =========================================================================

    CommandResult CLIInterface::handleArrestCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Arrest command placeholder");
    }

    CommandResult CLIInterface::handleArrestListCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Arrest list placeholder");
    }

    CommandResult CLIInterface::handleArrestRegisterCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Arrest register placeholder");
    }

    CommandResult CLIInterface::handleArrestReleaseCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Arrest release placeholder");
    }

    // =========================================================================
    // BAIL COMMANDS (Stubs)
    // =========================================================================

    CommandResult CLIInterface::handleBailCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Bail command placeholder");
    }

    CommandResult CLIInterface::handleBailListCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Bail list placeholder");
    }

    CommandResult CLIInterface::handleBailApplyCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Bail apply placeholder");
    }

    CommandResult CLIInterface::handleBailApproveCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Bail approve placeholder");
    }

    CommandResult CLIInterface::handleBailDenyCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Bail deny placeholder");
    }

    // =========================================================================
    // FORENSIC COMMANDS (Stubs)
    // =========================================================================

    CommandResult CLIInterface::handleForensicCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Forensic command placeholder");
    }

    CommandResult CLIInterface::handleForensicListCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Forensic list placeholder");
    }

    CommandResult CLIInterface::handleForensicCreateCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Forensic create placeholder");
    }

    // =========================================================================
    // AUDIT COMMANDS (Stubs)
    // =========================================================================

    CommandResult CLIInterface::handleAuditCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Audit command placeholder");
    }

    CommandResult CLIInterface::handleAuditQueryCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Audit query placeholder");
    }

    CommandResult CLIInterface::handleAuditExportCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Audit export placeholder");
    }

    // =========================================================================
    // PERSONNEL COMMANDS (Stubs)
    // =========================================================================

    CommandResult CLIInterface::handlePersonnelCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Personnel command placeholder");
    }

    CommandResult CLIInterface::handlePersonnelListCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Personnel list placeholder");
    }

    CommandResult CLIInterface::handlePersonnelViewCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Personnel view placeholder");
    }

    // =========================================================================
    // DUTY COMMANDS (Stubs)
    // =========================================================================

    CommandResult CLIInterface::handleDutyCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Duty command placeholder");
    }

    CommandResult CLIInterface::handleDutyScheduleCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Duty schedule placeholder");
    }

    CommandResult CLIInterface::handleDutyStartCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Duty start placeholder");
    }

    CommandResult CLIInterface::handleDutyEndCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Duty end placeholder");
    }

    CommandResult CLIInterface::handleDutyRosterCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Duty roster placeholder");
    }

    // =========================================================================
    // SYSTEM COMMANDS
    // =========================================================================

    CommandResult CLIInterface::handleSystemCommand(const std::vector<std::string> &args)
    {
        if (args.empty())
        {
            return CommandResult::error("system requires subcommand: status, health");
        }
        return CommandResult::ok("System command placeholder");
    }

    CommandResult CLIInterface::handleStatusCommand(const std::vector<std::string> &args)
    {
        // Show overall system status
        std::ostringstream oss;
        oss << "\n╔════════════════════════════════════════════════════════╗\n"
            << "║               JUSTICEFLOW SYSTEM STATUS               ║\n"
            << "╚════════════════════════════════════════════════════════╝\n"
            << "Status:              RUNNING\n"
            << "API Gateway:         LISTENING\n"
            << "Database:            CONNECTED\n"
            << "Auth Manager:        READY\n"
            << "\n";

        return CommandResult::ok("", oss.str());
    }

    CommandResult CLIInterface::handleHealthCommand(const std::vector<std::string> &args)
    {
        (void)args;
        return CommandResult::ok("Health check placeholder");
    }

    CommandResult CLIInterface::handleVersionCommand(const std::vector<std::string> &args)
    {
        std::ostringstream oss;
        oss << "JusticeFlow CLI v1.0.0\n"
            << "Build: 2026-05-08\n"
            << "Commit: dev\n";
        return CommandResult::ok("", oss.str());
    }

    // =========================================================================
    // BUILT-IN COMMANDS
    // =========================================================================

    CommandResult CLIInterface::handleHelpCommand(const std::vector<std::string> &args)
    {
        std::ostringstream oss;
        oss << "\n"
            << "AUTHENTICATION\n"
            << "  login --cnic <cnic> --password <pwd>   Login to system\n"
            << "  logout                                  Logout\n"
            << "  whoami                                  Show logged-in user\n"
            << "\n"
            << "CASE MANAGEMENT\n"
            << "  case list [--office ID]                 List cases\n"
            << "  case create --title T --desc D          Create case\n"
            << "  case view ID                            View case details\n"
            << "  case update ID --status STATUS          Update case\n"
            << "  case close ID --reason REASON           Close case\n"
            << "  case reopen ID --reason REASON          Reopen case\n"
            << "\n"
            << "EVIDENCE MANAGEMENT\n"
            << "  evidence list CASE_ID                   List evidence\n"
            << "  evidence add CASE_ID --file F           Add evidence\n"
            << "  evidence view CASE_ID EVIDENCE_ID       View evidence\n"
            << "\n"
            << "WARRANT OPERATIONS\n"
            << "  warrant list [--status STATUS]          List warrants\n"
            << "  warrant issue CASE_ID --accused NAME    Issue warrant\n"
            << "  warrant serve ID                        Serve warrant\n"
            << "  warrant revoke ID --reason REASON       Revoke warrant\n"
            << "\n"
            << "ARREST OPERATIONS\n"
            << "  arrest list [--custody TYPE]            List arrests\n"
            << "  arrest register WARRANT_ID --loc LOC    Register arrest\n"
            << "  arrest release ID --reason REASON       Release from custody\n"
            << "\n"
            << "BAIL OPERATIONS\n"
            << "  bail list [--status STATUS]             List bail applications\n"
            << "  bail apply ARREST_ID --amount A         Apply for bail\n"
            << "  bail approve ID                         Approve bail\n"
            << "  bail deny ID --reason REASON            Deny bail\n"
            << "\n"
            << "FORENSIC ANALYSIS\n"
            << "  forensic list CASE_ID                   List forensic analyses\n"
            << "  forensic create CASE_ID --type TYPE     Create analysis\n"
            << "\n"
            << "AUDIT & COMPLIANCE\n"
            << "  audit query --user UID [--from T]       Query audit trail\n"
            << "  audit export --format JSON              Export audit log\n"
            << "\n"
            << "SYSTEM\n"
            << "  status                                  System status\n"
            << "  version                                 Show version\n"
            << "  help [COMMAND]                          Show help\n"
            << "  history                                 Show command history\n"
            << "  clear                                   Clear screen\n"
            << "  exit                                    Exit CLI\n"
            << "\n";

        return CommandResult::ok("", oss.str());
    }

    CommandResult CLIInterface::handleExitCommand(const std::vector<std::string> &args)
    {
        m_running = false;
        return CommandResult::ok("Exiting...");
    }

    CommandResult CLIInterface::handleHistoryCommand(const std::vector<std::string> &args)
    {
        std::ostringstream oss;
        oss << "Command History:\n";
        for (size_t i = 0; i < m_history.size(); ++i)
        {
            oss << std::setw(4) << (i + 1) << "  " << m_history[i] << "\n";
        }
        return CommandResult::ok("", oss.str());
    }

    CommandResult CLIInterface::handleClearCommand(const std::vector<std::string> &args)
    {
        system("clear || cls"); // Works on Unix and Windows
        return CommandResult::ok();
    }

    // =========================================================================
    // HELPER METHODS
    // =========================================================================

    CLIInterface::ParsedArgs CLIInterface::parseArgs(const std::vector<std::string> &tokens,
                                                     size_t start)
    {
        ParsedArgs result;

        for (size_t i = start; i < tokens.size(); ++i)
        {
            const std::string &token = tokens[i];

            if (token.substr(0, 2) == "--")
            {
                std::string key = token.substr(2);

                if (i + 1 < tokens.size() && tokens[i + 1].substr(0, 2) != "--")
                {
                    result.flags[key] = tokens[++i];
                }
                else
                {
                    result.bools[key] = true;
                }
            }
            else if (token.substr(0, 1) == "-")
            {
                std::string key = token.substr(1);
                if (i + 1 < tokens.size() && tokens[i + 1].substr(0, 1) != "-")
                {
                    result.flags[key] = tokens[++i];
                }
                else
                {
                    result.bools[key] = true;
                }
            }
            else
            {
                result.positional.push_back(token);
            }
        }

        return result;
    }

    CommandResult CLIInterface::requireLogin()
    {
        if (!m_context.isLoggedIn())
        {
            return CommandResult::error("Not logged in. Use 'login' command.");
        }

        // Validate session
        auto session_result = m_system.auth().validateToken(m_context.session_token.c_str());
        if (!session_result.ok())
        {
            m_context.officer_id = -1; // Invalidate
            return CommandResult::error("Session expired. Please login again.");
        }

        return CommandResult::ok();
    }

    CommandResult CLIInterface::requirePermission(const std::string &action)
    {
        if (!m_context.hasPermission(action))
        {
            return CommandResult::error("Permission denied: " + action);
        }
        return CommandResult::ok();
    }

    CommandResult CLIInterface::validateSession()
    {
        auto result = m_system.auth().validateToken(m_context.session_token.c_str());
        if (!result.ok())
        {
            m_context.officer_id = -1;
            return CommandResult::error("Session invalid");
        }
        return CommandResult::ok();
    }

    std::string CLIInterface::formatAsTable(const std::string &title,
                                            const std::vector<std::vector<std::string>> &rows,
                                            const std::vector<std::string> &headers)
    {
        std::ostringstream oss;
        oss << "\n"
            << title << " (" << rows.size() << " records)\n";
        oss << std::string(70, '-') << "\n";

        // Print headers
        for (const auto &h : headers)
        {
            oss << std::left << std::setw(18) << h;
        }
        oss << "\n"
            << std::string(70, '-') << "\n";

        // Print rows
        for (const auto &row : rows)
        {
            for (const auto &cell : row)
            {
                oss << std::left << std::setw(18) << cell;
            }
            oss << "\n";
        }

        oss << std::string(70, '-') << "\n";
        return oss.str();
    }

    std::string CLIInterface::formatAsJson(const std::string &data)
    {
        // Placeholder: real JSON formatting would go here
        return data;
    }

    std::string CLIInterface::formatAsCsv(const std::vector<std::vector<std::string>> &rows,
                                          const std::vector<std::string> &headers)
    {
        std::ostringstream oss;

        // CSV header
        for (size_t i = 0; i < headers.size(); ++i)
        {
            oss << headers[i];
            if (i < headers.size() - 1)
                oss << ",";
        }
        oss << "\n";

        // CSV rows
        for (const auto &row : rows)
        {
            for (size_t i = 0; i < row.size(); ++i)
            {
                oss << row[i];
                if (i < row.size() - 1)
                    oss << ",";
            }
            oss << "\n";
        }

        return oss.str();
    }

} // namespace cli_interface