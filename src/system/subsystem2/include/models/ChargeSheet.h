#pragma once

#include <string>
#include <vector>
#include "../s2_types.h"

namespace subsystem2
{

    /**
     * @brief Entity Class for ChargeSheet (Challan / Section 173 Report).
     * Maps to the Draw.io UML Box for ChargeSheet.
     */
    class ChargeSheet
    {
    private:
        ChargeSheetDTO data;

    public:
        // Constructor initializes with DB data
        explicit ChargeSheet(const ChargeSheetDTO &init_data);
        ~ChargeSheet() = default;

        // --- Data Accessors ---
        int64_t getSheetId() const;
        std::string getSheetNumber() const;
        JusticeFlow::ChargeSheetStatus getStatus() const;
        bool isLocked() const;

        // --- Domain Logic ---

        // Adds a legal statute (e.g., "PPC 302") to the array
        JusticeFlow::ResultCode addLawInvoked(const std::string &law);

        // Transitions status to SUBMITTED_TO_COURT and sets is_locked = true
        JusticeFlow::ResultCode submitToMagistrate(int64_t officer_id);

        // Returns DTO for database saving
        ChargeSheetDTO getDTO() const;
    };

} // namespace subsystem2
