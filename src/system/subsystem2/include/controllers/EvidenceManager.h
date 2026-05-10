#pragma once

#include "../models/Evidence.h"
#include "../s2_types.h"
#include "../../../common/constants.h"
#include "../../../common/common.h"

//  include the OS memory handler which was built in os_layer
#include "../../../os_layer/memory/include/mmap_handler.h"

namespace subsystem2
{

    /**
     * @brief Controller for managing Evidence lifecycle.
     * Integrates directly with OS Layer (mmap) for secure file handling.
     */
    class EvidenceManager
    {
    private:
        // Holds the memory mapping for the currently active evidence file
        memory::MmapHandler mem_handler;

    public:
        EvidenceManager() = default;
        ~EvidenceManager() = default;

        /**
         * Use Case 2: Log & Secure Evidence
         * @param case_id The case this evidence belongs to.
         * @param type Physical or Digital.
         * @param description Text details.
         * @param file_path The path to the CCTV or document file on Linux.
         * @param session The logged-in IO.
         * @param out_evidence Pointer to return the created object.
         */
        JusticeFlow::ResultCode logAndSecureEvidence(int64_t case_id,
                                                     JusticeFlow::EvidenceType type,
                                                     const std::string &description,
                                                     const std::string &file_path,
                                                     const JusticeFlow::SessionContext &session,
                                                     Evidence *&out_evidence);

        /**
         * OS Requirement: Maps the evidence file to RAM using demand paging.
         */
        JusticeFlow::ResultCode mapFileToMemory(const std::string &file_path);
    };

} // namespace subsystem2
