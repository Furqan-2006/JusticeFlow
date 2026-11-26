#include "../include/process_spawner.h"

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common/logger.h"
#include "os_layer/ipc/include/ipc_manager.h"

extern char **environ;

namespace
{
    // The agent's environment: ours, minus anything about the daemon's own
    // database login, plus the AI role's credentials under both naming schemes
    // the agents use (JF_DB_* and the libpq PG* variables).
    std::vector<std::string> agentEnvironment()
    {
        auto get = [](const char *k, const char *fallback)
        {
            const char *v = std::getenv(k);
            return std::string(v && *v ? v : fallback);
        };
        const std::string host = get("JF_AI_DB_HOST", get("JF_DB_HOST", "127.0.0.1").c_str());
        const std::string name = get("JF_AI_DB_NAME", get("JF_DB_NAME", "justiceflow").c_str());
        const std::string user = get("JF_AI_DB_USER", "justice_ai");
        const std::string pass = get("JF_AI_DB_PASS", "");

        std::vector<std::string> env;
        for (char **e = environ; e && *e; ++e)
        {
            const std::string kv = *e;
            if (kv.rfind("JF_DB_", 0) == 0 || kv.rfind("PG", 0) == 0)
                continue;
            env.push_back(kv);
        }
        env.push_back("JF_DB_HOST=" + host);
        env.push_back("JF_DB_NAME=" + name);
        env.push_back("JF_DB_USER=" + user);
        env.push_back("JF_DB_PASS=" + pass);
        env.push_back("PGHOST=" + host);
        env.push_back("PGDATABASE=" + name);
        env.push_back("PGUSER=" + user);
        env.push_back("PGPASSWORD=" + pass);
        return env;
    }
} // namespace

AgentJob::AgentJob(int interval, const char *name, const char *script_path, int agent_index,
                   int timeout_seconds)
    : Job(interval), agent_name_(name), script_path_(script_path),
      agent_index_(agent_index), timeout_seconds_(timeout_seconds)
{
}

void AgentJob::execute()
{
    if (pid_ > 0)
    {
        Logger::info(("Agent " + agent_name_ + " is still running; skipping this run").c_str());
        return;
    }

    std::vector<std::string> env = agentEnvironment();
    std::vector<char *> envp;
    for (auto &s : env)
        envp.push_back(const_cast<char *>(s.c_str()));
    envp.push_back(nullptr);

    // -I: isolated mode, so nothing from the agent's directory or the
    // environment's PYTHON* variables can shadow the standard library.
    std::string python = "python3", flag = "-I", script = script_path_;
    char *argv[] = {const_cast<char *>(python.c_str()), const_cast<char *>(flag.c_str()),
                    const_cast<char *>(script.c_str()), nullptr};

    pid_t child = -1;
    const int rc = posix_spawnp(&child, "python3", nullptr, nullptr, argv, envp.data());
    if (rc != 0)
    {
        record(127, std::string("could not start agent: ") + std::strerror(rc));
        return;
    }

    pid_ = child;
    started_at_ = ::time(nullptr);
    Logger::info(("Agent " + agent_name_ + " started (pid " + std::to_string(child) + ")").c_str());
}

void AgentJob::onTick()
{
    if (pid_ <= 0)
        return;

    int status = 0;
    const pid_t done = ::waitpid(pid_, &status, WNOHANG);

    if (done == 0)
    {
        if (::time(nullptr) - started_at_ > timeout_seconds_)
        {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, &status, 0);
            pid_ = -1;
            record(124, "timed out after " + std::to_string(timeout_seconds_) + "s and was killed");
        }
        return;
    }

    pid_ = -1;
    if (done < 0)
    {
        record(1, "lost track of the agent process");
    }
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        record(0, "");
    }
    else if (WIFEXITED(status))
    {
        record(WEXITSTATUS(status), "exited with code " + std::to_string(WEXITSTATUS(status)));
    }
    else if (WIFSIGNALED(status))
    {
        record(128 + WTERMSIG(status), "killed by signal " + std::to_string(WTERMSIG(status)));
    }
}

void AgentJob::terminate()
{
    if (pid_ <= 0)
        return;
    ::kill(pid_, SIGTERM);
    int status = 0;
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
}

void AgentJob::record(int status_code, const std::string &detail)
{
    AgentStatus st;
    std::memset(&st, 0, sizeof(st));
    std::strncpy(st.agent_name, agent_name_.c_str(), sizeof(st.agent_name) - 1);
    st.current_status = status_code;
    std::strncpy(st.error_detail, detail.c_str(), sizeof(st.error_detail) - 1);
    st.last_updated = ::time(nullptr);

    ipc::IpcManager::getInstance().updateAgentStatus(agent_index_, st);

    const std::string line = "Agent " + agent_name_ + (status_code == 0 ? " finished OK" : " failed: " + detail);
    if (status_code == 0)
        Logger::info(line.c_str());
    else
        Logger::error(line.c_str());
}
