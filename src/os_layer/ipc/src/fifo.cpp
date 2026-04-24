#include "../include/fifo.h"
#include "../../../common/logger.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

namespace ipc {

JusticeFlow::ResultCode Fifo::create(const std::string& path) {
    if (mkfifo(path.c_str(), 0666) == -1 && errno != EEXIST) {
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode Fifo::readStatus(const std::string& path, AgentStatusMessage& out_msg) {
    // CRITICAL FIX: O_NONBLOCK prevents the scheduler from hanging
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd == -1) return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;

    ssize_t bytes = read(fd, &out_msg, sizeof(AgentStatusMessage));
    close(fd);

    if (bytes <= 0) {
        return JusticeFlow::ResultCode::NOT_FOUND; // Tells scheduler "No data yet, try later"
    }
    return JusticeFlow::ResultCode::OK;
}

void Fifo::destroy(const std::string& path) {
    unlink(path.c_str());
}

} // namespace ipc
