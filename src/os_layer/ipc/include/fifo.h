#pragma once

#include <string>
#include <cstddef>
#include "../../../common/constants.h"
#include "../../../common/ipc_types.h"

namespace ipc
{

    class Fifo
    {
    private:
        // Expected size of the message structure - cached for validation
        static constexpr size_t EXPECTED_MESSAGE_SIZE = sizeof(AgentStatusMessage);

    public:
        Fifo() = default;
        ~Fifo() = default;

        // Deleted copy constructor and assignment operator to prevent misuse
        Fifo(const Fifo &) = delete;
        Fifo &operator=(const Fifo &) = delete;

        /**
         * Creates a FIFO at the given path with restricted permissions.
         * Safe to call from non-signal context.
         *
         * @param path The filesystem path for the FIFO
         * @return ResultCode::OK on success, FILE_SYSTEM_ERROR on failure
         */
        JusticeFlow::ResultCode create(const std::string &path);

        /**
         * Reads an AgentStatusMessage from the FIFO with full validation.
         * Uses O_NONBLOCK to prevent scheduler from hanging.
         *
         * CRITICAL: This method validates that the read returns the COMPLETE
         * message. Partial reads are treated as errors to prevent garbage data
         * in shared memory.
         *
         * @param path The FIFO path to read from
         * @param out_msg Output parameter for the status message
         * @return ResultCode::OK if complete message read successfully
         *         ResultCode::NOT_FOUND if FIFO is empty or no data available
         *         ResultCode::FILE_SYSTEM_ERROR on I/O error
         */
        JusticeFlow::ResultCode readStatus(const std::string &path, AgentStatusMessage &out_msg);

        /**
         * Destroys (unlinks) the FIFO at the given path.
         * Safe to call from non-signal context.
         *
         * @param path The FIFO path to destroy
         */
        void destroy(const std::string &path);
    };

} // namespace ipc
