#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <string>
#include "../../../common/constants.h"
#include "shm_layout.h"

namespace ipc {

class SharedMemory {
private:
    std::string shm_name;
    int shm_fd;
    SharedStatusTable* mapped_table;
    bool is_creator;

public:
    SharedMemory(const std::string& name);
    ~SharedMemory();

    JusticeFlow::ResultCode create();
    JusticeFlow::ResultCode attach();
    void destroy();

    SharedStatusTable* getTable() const; // Returns typed pointer!
};

} // namespace ipc
#endif
