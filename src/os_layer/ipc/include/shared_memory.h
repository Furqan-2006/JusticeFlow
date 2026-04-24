#pragma once

#include <string>
#include "../../../common/constants.h"
#include "shm_layout.h" // pulls common/ipc_types.h

namespace ipc
{

    class SharedMemory
    {
    private:
        std::string shm_name;
        int shm_fd;
        SharedStatusTable *mapped_table;
        bool is_creator;

    public:
        explicit SharedMemory(const std::string &name);
        ~SharedMemory();

        JusticeFlow::ResultCode create();
        JusticeFlow::ResultCode attach();
        void destroy();

        SharedStatusTable *getTable() const;
    };

} // namespace ipc