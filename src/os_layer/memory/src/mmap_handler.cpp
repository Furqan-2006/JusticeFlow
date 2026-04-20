#include "../include/mmap_handler.h"
#include "../../../common/logger.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace os_layer {
namespace memory {

MmapHandler::MmapHandler() 
    : mapped_address(MAP_FAILED), mapping_size(0), file_descriptor(-1), is_mapped(false) {}

MmapHandler::~MmapHandler() {
    // RAII: Guarantee memory is unmapped when object goes out of scope
    if (is_mapped) {
        unmap();
    }
}

JusticeFlow::ResultCode MmapHandler::map(int fd, size_t size, bool is_shared) {
    if (is_mapped) {
        Logger::error("[OS][Memory] Attempted to map an already mapped handler.");
        return JusticeFlow::ResultCode::INVALID_STATE;
    }

    file_descriptor = fd;
    mapping_size = size;

    // Determine mapping flags based on use case (IPC vs Evidence Files)
    int flags = is_shared ? MAP_SHARED : MAP_PRIVATE;
    
    // PROT_READ | PROT_WRITE allows both reading and writing to the memory
    mapped_address = mmap(nullptr, mapping_size, PROT_READ | PROT_WRITE, flags, file_descriptor, 0);

    if (mapped_address == MAP_FAILED) {
        Logger::error(("[OS][Memory] mmap failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    is_mapped = true;
    Logger::info("[OS][Memory] Memory mapping successful.");
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode MmapHandler::unmap() {
    if (!is_mapped || mapped_address == MAP_FAILED) {
        return JusticeFlow::ResultCode::INVALID_STATE;
    }

    if (munmap(mapped_address, mapping_size) == -1) {
        Logger::error(("[OS][Memory] munmap failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    mapped_address = MAP_FAILED;
    mapping_size = 0;
    is_mapped = false;
    Logger::info("[OS][Memory] Memory successfully unmapped.");
    
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode MmapHandler::sync() {
    if (!is_mapped || mapped_address == MAP_FAILED) {
        return JusticeFlow::ResultCode::INVALID_STATE;
    }

    // MS_SYNC forces a synchronous write to the backing file/store
    if (msync(mapped_address, mapping_size, MS_SYNC) == -1) {
        Logger::error(("[OS][Memory] msync failed: " + std::string(strerror(errno))).c_str());
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    return JusticeFlow::ResultCode::OK;
}

void* MmapHandler::getPointer() const {
    return is_mapped ? mapped_address : nullptr;
}

bool MmapHandler::isValid() const {
    return is_mapped;
}

} // namespace memory
} // namespace os_layer
