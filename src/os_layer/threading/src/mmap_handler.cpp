#include "../include/mmap_handler.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>

void* MmapHandler::mapFile(const char* path, size_t& out_size, MapMode mode) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        std::cerr << "[Memory] Failed to open file for mmap: " << path << "\n";
        return MAP_FAILED;
    }

    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        close(fd);
        return MAP_FAILED;
    }
    out_size = sb.st_size;

    // MAP_POPULATE pre-faults the memory so we don't stall during execution
    void* addr = mmap(nullptr, out_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
    close(fd); // Safe to close file descriptor after mapping

    if (addr == MAP_FAILED) {
        std::cerr << "[Memory] mmap failed for: " << path << "\n";
        return MAP_FAILED;
    }

    // Apply kernel hints based on the access pattern
    if (mode == SEQUENTIAL) {
        madvise(addr, out_size, MADV_SEQUENTIAL);
    } else {
        madvise(addr, out_size, MADV_RANDOM);
    }

    return addr;
}

void MmapHandler::unmap(void* addr, size_t size) {
    if (addr && addr != MAP_FAILED) {
        munmap(addr, size);
    }
}

void MmapHandler::sync_shared(void* addr, size_t size) {
    if (addr && addr != MAP_FAILED) {
        msync(addr, size, MS_SYNC);
    }
}
