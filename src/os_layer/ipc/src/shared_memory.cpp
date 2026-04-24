#include "../include/shared_memory.h"
#include "../../../common/logger.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace ipc
{

    SharedMemory::SharedMemory(const std::string &name)
        : shm_name(name), shm_fd(-1), mapped_table(nullptr), is_creator(false) {}

    SharedMemory::~SharedMemory()
    {
        if (mapped_table != nullptr && mapped_table != MAP_FAILED)
        {
            munmap(mapped_table, sizeof(SharedStatusTable));
            mapped_table = nullptr;
        }
        if (shm_fd != -1)
        {
            close(shm_fd);
            shm_fd = -1;
        }
    }

    JusticeFlow::ResultCode SharedMemory::create()
    {
        shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0600);
        if (shm_fd == -1)
        {
            Logger::error(("[OS][IPC] shm_open (create) failed: " + std::string(strerror(errno))).c_str());
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        // Size the shared memory segment to exactly one SharedStatusTable
        if (ftruncate(shm_fd, sizeof(SharedStatusTable)) == -1)
        {
            Logger::error(("[OS][IPC] ftruncate failed: " + std::string(strerror(errno))).c_str());
            close(shm_fd);
            shm_fd = -1;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        void *raw_ptr = mmap(NULL, sizeof(SharedStatusTable), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (raw_ptr == MAP_FAILED)
        {
            Logger::error(("[OS][IPC] mmap (create) failed: " + std::string(strerror(errno))).c_str());
            close(shm_fd);
            shm_fd = -1;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        mapped_table = static_cast<SharedStatusTable *>(raw_ptr);

        // CRITICAL: Initialize the robust, process-shared mutex in shared memory
        pthread_mutexattr_t mutex_attr;
        if (pthread_mutexattr_init(&mutex_attr) != 0)
        {
            Logger::error("[OS][IPC] pthread_mutexattr_init failed");
            munmap(mapped_table, sizeof(SharedStatusTable));
            close(shm_fd);
            shm_fd = -1;
            mapped_table = nullptr;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        // Allow other processes to acquire this mutex via shared memory
        if (pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED) != 0)
        {
            Logger::error("[OS][IPC] pthread_mutexattr_setpshared failed");
            pthread_mutexattr_destroy(&mutex_attr);
            munmap(mapped_table, sizeof(SharedStatusTable));
            close(shm_fd);
            shm_fd = -1;
            mapped_table = nullptr;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        // Make the mutex robust: if a process dies while holding the lock,
        // the next lock acquirer will receive EOWNERDEAD and can call pthread_mutex_consistent
        if (pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST) != 0)
        {
            Logger::error("[OS][IPC] pthread_mutexattr_setrobust failed");
            pthread_mutexattr_destroy(&mutex_attr);
            munmap(mapped_table, sizeof(SharedStatusTable));
            close(shm_fd);
            shm_fd = -1;
            mapped_table = nullptr;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        if (pthread_mutex_init(&mapped_table->mutex, &mutex_attr) != 0)
        {
            Logger::error("[OS][IPC] pthread_mutex_init failed");
            pthread_mutexattr_destroy(&mutex_attr);
            munmap(mapped_table, sizeof(SharedStatusTable));
            close(shm_fd);
            shm_fd = -1;
            mapped_table = nullptr;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        pthread_mutexattr_destroy(&mutex_attr);

        // CRITICAL: Initialize the process-shared condition variable in shared memory
        pthread_condattr_t cond_attr;
        if (pthread_condattr_init(&cond_attr) != 0)
        {
            Logger::error("[OS][IPC] pthread_condattr_init failed");
            pthread_mutex_destroy(&mapped_table->mutex);
            munmap(mapped_table, sizeof(SharedStatusTable));
            close(shm_fd);
            shm_fd = -1;
            mapped_table = nullptr;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        // Allow other processes to wait on this condition variable via shared memory
        if (pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED) != 0)
        {
            Logger::error("[OS][IPC] pthread_condattr_setpshared failed");
            pthread_condattr_destroy(&cond_attr);
            pthread_mutex_destroy(&mapped_table->mutex);
            munmap(mapped_table, sizeof(SharedStatusTable));
            close(shm_fd);
            shm_fd = -1;
            mapped_table = nullptr;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        if (pthread_cond_init(&mapped_table->cond_var, &cond_attr) != 0)
        {
            Logger::error("[OS][IPC] pthread_cond_init failed");
            pthread_condattr_destroy(&cond_attr);
            pthread_mutex_destroy(&mapped_table->mutex);
            munmap(mapped_table, sizeof(SharedStatusTable));
            close(shm_fd);
            shm_fd = -1;
            mapped_table = nullptr;
            shm_unlink(shm_name.c_str());
            return JusticeFlow::ResultCode::INVALID_STATE;
        }

        pthread_condattr_destroy(&cond_attr);

        // Initialize other fields
        mapped_table->active_sessions = 0;
        std::memset(mapped_table->agents, 0, sizeof(mapped_table->agents));

        is_creator = true;
        Logger::info(("[OS][IPC] Shared Memory created and initialized: " + shm_name).c_str());
        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode SharedMemory::attach()
    {
        // 1. Open existing shared memory object (note: no O_CREAT flag)
        shm_fd = shm_open(shm_name.c_str(), O_RDWR, 0600);
        if (shm_fd == -1)
        {
            Logger::error(("[OS][IPC] shm_open (attach) failed: " + std::string(strerror(errno))).c_str());
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        // 2. Map the memory into this process's address space
        void *raw_ptr = mmap(NULL, sizeof(SharedStatusTable), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (raw_ptr == MAP_FAILED)
        {
            Logger::error(("[OS][IPC] mmap (attach) failed: " + std::string(strerror(errno))).c_str());
            close(shm_fd);
            shm_fd = -1;
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        mapped_table = static_cast<SharedStatusTable *>(raw_ptr);

        is_creator = false;
        Logger::info(("[OS][IPC] Shared Memory attached: " + shm_name).c_str());
        return JusticeFlow::ResultCode::OK;
    }

    void SharedMemory::destroy()
    {
        // Only the creator process should unlink the shared memory object
        if (is_creator && !shm_name.empty())
        {
            if (shm_unlink(shm_name.c_str()) == -1)
            {
                Logger::error(("[OS][IPC] shm_unlink failed: " + std::string(strerror(errno))).c_str());
            }
            else
            {
                Logger::info(("[OS][IPC] Shared Memory destroyed: " + shm_name).c_str());
            }
        }
    }

    SharedStatusTable *SharedMemory::getTable() const
    {
        return mapped_table;
    }

} // namespace ipc
