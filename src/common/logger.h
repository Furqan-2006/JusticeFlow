#pragma once

#include <pthread.h>
#include <cstdio>
#include <ctime>

class Logger
{
private:
    FILE *logFile;
    pthread_mutex_t mutex;

    Logger() : logFile(nullptr)
    {
        pthread_mutex_init(&mutex, nullptr);
    }

    ~Logger()
    {
        pthread_mutex_lock(&mutex);
        if (logFile)
        {
            fclose(logFile);
            logFile = nullptr;
        }
        pthread_mutex_unlock(&mutex);
        pthread_mutex_destroy(&mutex);
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void writeLog(const char *level, const char *message)
    {
        pthread_mutex_lock(&mutex);

        if (logFile)
        {
            time_t now = time(nullptr);
            struct tm tm_info;

            /**
             * @brief Using localtime_r for thread safety if available (POSIX standard)
             */

#if defined(_WIN32) || defined(_WIN64)
            localtime_s(&tm_info, &now);
#else
            localtime_r(&now, &tm_info);
#endif
            char time_buffer[26];
            strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &tm_info);
            fprintf(logFile, "[%s] [%s] %s\n", time_buffer, level, message);
            fflush(logFile);
        }

        pthread_mutex_unlock(&mutex);
    }

public:
    static Logger &getInstance() // singleton obj
    {
        static Logger instance;
        return instance;
    }

    static bool init(const char *filePath = "/var/log/justiceflow.log")
    {
        Logger &logger = getInstance();

        pthread_mutex_lock(&logger.mutex);
        if (logger.logFile)
        {
            fclose(logger.logFile);
        }

        logger.logFile = fopen(filePath, "a");
        bool success = (logger.logFile != nullptr);

        pthread_mutex_unlock(&logger.mutex);
        return success;
    }

    static void info(const char *message)
    {
        getInstance().writeLog("INFO", message);
    }

    static void error(const char *message)
    {
        getInstance().writeLog("ERROR", message);
    }
    static void debug(const char *message)
    {
        getInstance().writeLog("DEBUG", message);
    }
};
