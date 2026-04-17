#include "../include/shared_memory.h"
#include "../../../common/logger.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace os_layer {
namespace ipc {

SharedMemory::SharedMemory(const std::string& name, size_t size)
    : shm_name(name), shm_fd(-1), shm_size(size), mapped_addr(MAP_FAILED), is_creator(false) {}

SharedMemory::~SharedMemory() {
    if (mapped_addr != MAP_FAILED) {
        munmap(mapped_addr, shm_size);
        mapped_addr = MAP_FAILED;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
}

JusticeFlow::ResultCode SharedMemory::create() {
    // 1. Create the POSIX shared memory object
    shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        Logger::error(("[OS][IPC] shm_open (create) failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    // 2. Set the size of the memory segment
    if (ftruncate(shm_fd, shm_size) == -1) {
        Logger::error(("[OS][IPC] ftruncate failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    // 3. Map the memory into this process's address space
    mapped_addr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (mapped_addr == MAP_FAILED) {
        Logger::error(("[OS][IPC] mmap (create) failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    is_creator = true;
    Logger::info(("[OS][IPC] Shared Memory created: " + shm_name).c_str());
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode SharedMemory::attach() {
    // 1. Open existing shared memory object
    shm_fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
    if (shm_fd == -1) {
        Logger::error(("[OS][IPC] shm_open (attach) failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    // 2. Map the memory into this process's address space
    mapped_addr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (mapped_addr == MAP_FAILED) {
        Logger::error(("[OS][IPC] mmap (attach) failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    is_creator = false;
    Logger::info(("[OS][IPC] Shared Memory attached: " + shm_name).c_str());
    return JusticeFlow::ResultCode::OK;
}

void* SharedMemory::getPointer() const {
    return (mapped_addr == MAP_FAILED) ? nullptr : mapped_addr;
}

void SharedMemory::destroy() {
    if (is_creator) {
        shm_unlink(shm_name.c_str()); // Tells Linux to delete the RAM block
        Logger::info(("[OS][IPC] Shared Memory destroyed: " + shm_name).c_str());
    }
}

} // namespace ipc
} // namespace os_layer
