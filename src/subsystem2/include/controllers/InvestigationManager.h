#ifndef INVESTIGATION_MANAGER_H
#define INVESTIGATION_MANAGER_H

#include "../models/ChargeSheet.h"
#include "../s2_types.h"
#include "../../../common/constants.h"
#include "../../../common/common.h"

namespace subsystem2 {

/**
 * @brief Controller for managing active investigation conclusions.
 * Handles drafting and submitting Charge Sheets to the court.
 */
class InvestigationManager {
public:
    InvestigationManager() = default;
    ~InvestigationManager() = default;

    /**
     * Use Case 3: Draft a Charge Sheet
     * @param case_id The active case ID.
     * @param session The IO drafting the document.
     * @param out_sheet Output pointer to the created entity.
     */
    JusticeFlow::ResultCode draftChargeSheet(int64_t case_id, 
                                             const JusticeFlow::SessionContext& session, 
                                             ChargeSheet*& out_sheet);

    /**
     * Use Case 4: Submit to Court
     * @param sheet The C++ entity to submit.
     * @param session The IO submitting it.
     */
    JusticeFlow::ResultCode submitChargeSheet(ChargeSheet* sheet, 
                                              const JusticeFlow::SessionContext& session);
};

} // namespace subsystem2

#endif // INVESTIGATION_MANAGER_H
