#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <string>
#include <cstddef>
#include "../../../common/constants.h"

namespace ipc {

class SharedMemory {
private:
    std::string shm_name;
    int shm_fd;         // File descriptor for the shared memory object
    size_t shm_size;
    void* mapped_addr;  // Pointer to the actual RAM block
    bool is_creator;    // Did this process create the segment?

public:
    // Constructor takes the name (e.g., "/jf_status_shm") and size
    SharedMemory(const std::string& name, size_t size);
    
    // Destructor unmaps and optionally unlinks the segment
    ~SharedMemory();

    // Creates the shared memory segment (used by the Scheduler Daemon)
    JusticeFlow::ResultCode create();

    // Opens an existing segment (used by the Application/Dashboard)
    JusticeFlow::ResultCode attach();

    // Get the raw pointer to cast to SharedStatusTable
    void* getPointer() const;

    // Remove the shared memory segment from the OS
    void destroy();
};

} // namespace ipc

#endif // SHARED_MEMORY_H
