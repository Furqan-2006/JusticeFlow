#include "../include/fifo.h"
#include "../../../common/logger.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ipc
{

    JusticeFlow::ResultCode Fifo::create(const std::string &path)
    {
        // Use 0600 (owner read/write only) instead of 0666 (world-writable)
        // This prevents other users from writing spoofed agent results
        if (mkfifo(path.c_str(), 0600) == -1)
        {
            // EEXIST is acceptable - FIFO may already exist from a previous run
            if (errno != EEXIST)
            {
                Logger::error(("[IPC] Failed to create FIFO: " + std::string(strerror(errno))).c_str());
                return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
            }
        }
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode Fifo::readStatus(const std::string &path, AgentStatusMessage &out_msg)
    {
        // O_NONBLOCK prevents the scheduler from hanging if no data is available
        // This is critical for the event loop to remain responsive
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd == -1)
        {
            // ENOENT is expected if the agent hasn't written yet
            if (errno != ENOENT)
            {
                Logger::debug(("[IPC] Failed to open FIFO: " + std::string(strerror(errno))).c_str());
            }
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        // Read exactly the size of the message struct
        const size_t expected_size = sizeof(AgentStatusMessage);
        ssize_t bytes = read(fd, &out_msg, expected_size);

        // Close the file descriptor before any error returns
        close(fd);

        // CRITICAL FIX: Validate that we read the COMPLETE message
        // Partial reads indicate the agent's write was incomplete or the struct is malformed
        if (bytes == 0)
        {
            // FIFO opened but no data available (expected with O_NONBLOCK)
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        if (bytes < 0)
        {
            // EAGAIN/EWOULDBLOCK: No data available (expected with O_NONBLOCK)
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return JusticeFlow::ResultCode::NOT_FOUND;
            }
            // Other errors are actual failures
            Logger::error(("[IPC] FIFO read error: " + std::string(strerror(errno))).c_str());
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        // CRITICAL: Reject partial reads to prevent garbage data in shared memory
        if (bytes != static_cast<ssize_t>(expected_size))
        {
            Logger::error(("[IPC] Partial FIFO read: got " + std::to_string(bytes) +
                           " bytes, expected " + std::to_string(expected_size))
                              .c_str());
            // Return zeros to prevent use of uninitialized/partial data
            std::memset(&out_msg, 0, expected_size);
            return JusticeFlow::ResultCode::NOT_FOUND;
        }

        // Success: Complete message read
        return JusticeFlow::ResultCode::OK;
    }

    void Fifo::destroy(const std::string &path)
    {
        if (unlink(path.c_str()) == -1)
        {
            // ENOENT is acceptable - FIFO may have already been cleaned up
            if (errno != ENOENT)
            {
                Logger::debug(("[IPC] Failed to unlink FIFO: " + std::string(strerror(errno))).c_str());
            }
        }
    }

} // namespace ipc
