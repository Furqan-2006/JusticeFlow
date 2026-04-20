#ifndef MMAP_HANDLER_H
#define MMAP_HANDLER_H

#include <cstddef>
#include <string>
#include "../../../common/constants.h"

namespace memory {

class MmapHandler {
private:
    void* mapped_address;
    size_t mapping_size;
    int file_descriptor;
    bool is_mapped;

public:
    // Constructor initializes an empty handler
    MmapHandler();
    
    // Destructor automatically unmaps memory (RAII)
    ~MmapHandler();

    // Map a file or shared memory object into virtual RAM
    // fd: The open file descriptor (from shm_open or open)
    // size: The size of the mapping
    // is_shared: true for MAP_SHARED (IPC), false for MAP_PRIVATE (Evidence files)
    JusticeFlow::ResultCode map(int fd, size_t size, bool is_shared);

    // Unmap the memory explicitly
    JusticeFlow::ResultCode unmap();

    // Flush changes to disk/backing store (msync)
    JusticeFlow::ResultCode sync();

    // Get the raw pointer to the mapped memory
    void* getPointer() const;

    // Check if memory is currently mapped
    bool isValid() const;
};

} // namespace memory

#endif // MMAP_HANDLER_H
