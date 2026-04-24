#pragma once

#include "common/constants.h"

namespace Daemon
{
    /**
     * @brief Daemonize the current process using double-fork approach
     *        Closes all file descriptors and redirects stdio to /dev/null
     *        Writes the final daemon PID to the specified file
     *        
     * @param pid_file_path Path where daemon PID will be written (default: /var/run/justiceflow.pid)
     * @return ResultCode::OK on success, or an error code on failure
     * 
     * WARNING: Sets umask to 0022 (not 0) to prevent world-writable files
     */
    JusticeFlow::ResultCode init(const char *pid_file_path = "/var/run/justiceflow.pid");
}