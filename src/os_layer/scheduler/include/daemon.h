#pragma once

#include "common/constants.h"

namespace Daemon
{
    /**
     * @brief Daemonize the current process using double-fork approach
     *        Writes the final daemon to the process PID to the specified file
     *        Returns ResultCode::OK on success, or an error on failure
     */

    JusticeFlow::ResultCode init(const char *pid_file_path = "/var/run/justiceflow.pid");
}