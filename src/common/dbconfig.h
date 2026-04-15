#pragma once

#include <string>
#include <cstdlib> // For std::getenv, std::atoi
#include <fstream> // For std::ifstream
#include "constants.h"

namespace JusticeFlow
{

    /**
     * @brief Holds database connection parameters and pool settings.
     * Designed to be instantiated once at startup and passed to the DB manager/pool.
     */
    struct DBConfig
    {
        // Connection parameters
        std::string host = "127.0.0.1";
        int port = 5432;
        std::string dbname = "justiceflow_db";
        std::string user = "justice_app"; // Standard least-privilege role
        std::string password = "";

        // Connection pool and timeout settings
        int min_connections = 2;
        int max_connections = 20;
        int connect_timeout = 10; // in seconds

        /**
         * @brief Generates a libpq-compatible connection string.
         */
        std::string toConnectionString() const
        {
            return "host=" + host +
                   " port=" + std::to_string(port) +
                   " dbname=" + dbname +
                   " user=" + user +
                   " password=" + password +
                   " connect_timeout=" + std::to_string(connect_timeout);
        }

        /**
         * @brief Loads configuration from environment variables.
         * Exception-safe implementation returning ResultCode.
         */
        ResultCode loadFromEnvironment()
        {
            const char *env_host = std::getenv("JF_DB_HOST");
            if (env_host)
                host = env_host;

            const char *env_port = std::getenv("JF_DB_PORT");
            if (env_port)
                port = std::atoi(env_port);

            const char *env_name = std::getenv("JF_DB_NAME");
            if (env_name)
                dbname = env_name;

            const char *env_user = std::getenv("JF_DB_USER");
            if (env_user)
                user = env_user;

            const char *env_pass = std::getenv("JF_DB_PASS");
            if (env_pass)
                password = env_pass;

            // Validate that critical fields were actually provided
            if (host.empty() || user.empty() || password.empty() || dbname.empty())
            {
                return ResultCode::INVALID_INPUT;
            }

            return ResultCode::OK;
        }

        /**
         * @brief Loads configuration from a secure local file handled by the privilege daemon.
         */
        ResultCode loadFromFile(const std::string &filepath)
        {
            std::ifstream file(filepath);

            if (!file.is_open())
            {
                return ResultCode::NOT_FOUND;
            }

            std::string line;
            while (std::getline(file, line))
            {
                if (line.empty() || line[0] == '#')
                    continue;

                auto delimiter_pos = line.find('=');
                if (delimiter_pos == std::string::npos)
                    continue;

                std::string key = line.substr(0, delimiter_pos);
                std::string value = line.substr(delimiter_pos + 1);

                if (key == "host")
                    host = value;
                else if (key == "port")
                    port = std::atoi(value.c_str());
                else if (key == "dbname")
                    dbname = value;
                else if (key == "user")
                    user = value;
                else if (key == "password")
                    password = value;
            }

            file.close();

            if (host.empty() || user.empty() || password.empty() || dbname.empty())
            {
                return ResultCode::INVALID_INPUT;
            }

            return ResultCode::OK;
        }
    };

} // namespace JusticeFlow