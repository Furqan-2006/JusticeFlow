#ifndef MMAP_HANDLER_H
#define MMAP_HANDLER_H

#include <stddef.h>

enum MapMode {
    SEQUENTIAL,
    RANDOM
};

class MmapHandler {
public:
    // Maps a file into memory and applies the requested access hints
    static void* mapFile(const char* path, size_t& out_size, MapMode mode);
    
    // Unmaps a previously mapped region
    static void unmap(void* addr, size_t size);
    
    // Flushes shared memory modifications atomically to the underlying file/segment
    static void sync_shared(void* addr, size_t size);
};

#endif // MMAP_HANDLER_H
