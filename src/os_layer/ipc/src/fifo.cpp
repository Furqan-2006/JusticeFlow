#include "../include/fifo.h"
#include "../../../common/logger.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace os_layer {
namespace ipc {

Fifo::Fifo(const std::string& path) : fifo_path(path), fd(-1), is_reader(false) {}

Fifo::~Fifo() {
    closeFifo();
}

JusticeFlow::ResultCode Fifo::create() {
    // mkfifo creates the named pipe. 0666 gives rw permissions.
    if (mkfifo(fifo_path.c_str(), 0666) == -1) {
        // If it already exists, that's fine (EEXIST)
        if (errno != EEXIST) {
            Logger::error(("[OS][IPC] Failed to create FIFO: " + std::string(strerror(errno))).c_str());
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }
    }
    Logger::info(("[OS][IPC] FIFO initialized at " + fifo_path).c_str());
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode Fifo::openForRead() {
    // O_RDONLY blocks until a writer opens the other end
    fd = open(fifo_path.c_str(), O_RDONLY);
    if (fd == -1) {
        Logger::error(("[OS][IPC] Failed to open FIFO for reading: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }
    is_reader = true;
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode Fifo::openForWrite() {
    // O_WRONLY blocks until a reader opens the other end
    fd = open(fifo_path.c_str(), O_WRONLY);
    if (fd == -1) {
        Logger::error(("[OS][IPC] Failed to open FIFO for writing: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }
    is_reader = false;
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode Fifo::readMessage(void* buffer, size_t size) {
    if (fd == -1 || !is_reader) return JusticeFlow::ResultCode::INVALID_STATE;

    ssize_t bytes_read = read(fd, buffer, size);
    if (bytes_read == -1) {
        Logger::error("[OS][IPC] Failed to read from FIFO.");
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode Fifo::writeMessage(const void* buffer, size_t size) {
    if (fd == -1 || is_reader) return JusticeFlow::ResultCode::INVALID_STATE;

    ssize_t bytes_written = write(fd, buffer, size);
    if (bytes_written == -1) {
        Logger::error("[OS][IPC] Failed to write to FIFO.");
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }
    return JusticeFlow::ResultCode::OK;
}

void Fifo::closeFifo() {
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
}

void Fifo::destroy() {
    closeFifo();
    unlink(fifo_path.c_str()); // Deletes the pipe file from /tmp
    Logger::info(("[OS][IPC] FIFO destroyed: " + fifo_path).c_str());
}

} // namespace ipc
} // namespace os_layer
