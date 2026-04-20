#ifndef FIFO_H
#define FIFO_H

#include <string>
#include <sys/types.h>
#include "../../../common/constants.h"

namespace ipc {

class Fifo {
private:
    std::string fifo_path;
    int fd;             // File descriptor for the pipe
    bool is_reader;     // Is this end reading or writing?

public:
    // Constructor defines the path (e.g., "/tmp/jf_hotspot.fifo")
    explicit Fifo(const std::string& path);
    
    // Destructor ensures the FIFO is closed and unlinked
    ~Fifo();

    // Create the FIFO on the filesystem (mkfifo)
    JusticeFlow::ResultCode create();

    // Open the FIFO for reading (Blocks until a writer connects)
    JusticeFlow::ResultCode openForRead();

    // Open the FIFO for writing (Blocks until a reader connects)
    JusticeFlow::ResultCode openForWrite();

    // Read a formatted IPC struct (from common/ipc_types.h)
    JusticeFlow::ResultCode readMessage(void* buffer, size_t size);

    // Write a formatted IPC struct
    JusticeFlow::ResultCode writeMessage(const void* buffer, size_t size);

    // Close the file descriptor
    void closeFifo();

    // Delete the FIFO file from the OS
    void destroy();
};

} // namespace ipc

#endif // FIFO_H
