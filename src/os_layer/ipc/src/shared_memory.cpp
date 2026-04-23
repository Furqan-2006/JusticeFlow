#include "../include/shared_memory.h"
#include "../../../common/logger.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ipc {

// Constructor: Size is no longer needed, it's statically typed to the struct size
SharedMemory::SharedMemory(const std::string& name)
    : shm_name(name), shm_fd(-1), mapped_table(nullptr), is_creator(false) {}

SharedMemory::~SharedMemory() {
    if (mapped_table != nullptr && mapped_table != MAP_FAILED) {
        munmap(mapped_table, sizeof(SharedStatusTable));
        mapped_table = nullptr;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
}

JusticeFlow::ResultCode SharedMemory::create() {
    shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        Logger::error(("[OS][IPC] shm_open (create) failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    ftruncate(shm_fd, sizeof(SharedStatusTable));
    
    void* raw_ptr = mmap(NULL, sizeof(SharedStatusTable), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (raw_ptr == MAP_FAILED) {
        Logger::error(("[OS][IPC] mmap (create) failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }
    
    mapped_table = static_cast<SharedStatusTable*>(raw_ptr);

    // CRITICAL FIX: ROBUST PROCESS-SHARED MUTEX
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST); 
    
    pthread_mutex_init(&mapped_table->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

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
    void* raw_ptr = mmap(NULL, sizeof(SharedStatusTable), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (raw_ptr == MAP_FAILED) {
        Logger::error(("[OS][IPC] mmap (attach) failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }
    
    mapped_table = static_cast<SharedStatusTable*>(raw_ptr);

    is_creator = false;
    Logger::info(("[OS][IPC] Shared Memory attached: " + shm_name).c_str());
    return JusticeFlow::ResultCode::OK;
}

void SharedMemory::destroy() {
    if (is_creator) {
        shm_unlink(shm_name.c_str()); // Tells Linux to delete the RAM block
        Logger::info(("[OS][IPC] Shared Memory destroyed: " + shm_name).c_str());
    }
}

SharedStatusTable* SharedMemory::getTable() const {
    return mapped_table; 
}

} // namespace ipc