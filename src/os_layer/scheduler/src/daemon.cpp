#include "../include/daemon.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdlib>
#include <cstdio>

namespace Daemon
{
    JusticeFlow::ResultCode init(const char *pid_file_path)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        if (pid > 0)
        {
            std::exit(EXIT_SUCCESS);
        }

        if (setsid() < 0)
        {
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        pid = fork();
        if (pid < 0)
        {
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }
        if (pid > 0)
        {
            std::exit(EXIT_SUCCESS);
        }

        umask(0);

        if (chdir("/") < 0)
        {
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        long max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0)
        {
            max_fd = 1024;
        }
        for (long fd = 0; fd < max_fd; fd++)
        {
            close(fd);
        }

        int fd_null = open("/dev/null", O_RDWR);
        if (fd_null != 0)
        {
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }
        dup2(fd_null, STDIN_FILENO);
        dup2(fd_null, STDOUT_FILENO);
        dup2(fd_null, STDERR_FILENO);

        FILE *pid_file = fopen(pid_file_path, "w");
        if (!pid_file)
        {
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        fprintf(pid_file, "%d\n", getpid());
        fclose(pid_file);

        return JusticeFlow::ResultCode::OK;
    }
} // namespace Daemon
