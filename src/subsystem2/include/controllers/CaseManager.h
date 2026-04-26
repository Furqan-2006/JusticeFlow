#ifndef CASE_MANAGER_H
#define CASE_MANAGER_H

#include "../models/Case.h"
#include "../s2_types.h"
#include "../../../common/constants.h"
#include "../../../common/common.h"

namespace subsystem2 {

class CaseManager {
public:
    CaseManager() = default;
    ~CaseManager() = default;

    JusticeFlow::ResultCode registerFIR(const FIRRegistrationRequest& request, 
                                        const JusticeFlow::SessionContext& session, 
                                        Case*& out_case);

    JusticeFlow::ResultCode fetchCase(int64_t case_id, Case*& out_case);
};

} // namespace subsystem2

#endif // CASE_MANAGER_H