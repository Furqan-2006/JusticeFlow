#pragma once

#include "../models/Case.h"
#include "../s2_types.h"
#include "../../../common/constants.h"
#include "../../../common/common.h"

namespace subsystem2
{

    class CaseManager
    {
    public:
        CaseManager() = default;
        ~CaseManager() = default;

        JusticeFlow::ResultCode registerFIR(const FIRRegistrationRequest &request,
                                            const JusticeFlow::SessionContext &session,
                                            Case *&out_case);

        JusticeFlow::ResultCode fetchCase(int64_t case_id, Case *&out_case);
        static JusticeFlow::ResultCode getCasesByStation(int64_t station_id, std::vector<Case *> &out_cases);
        static JusticeFlow::ResultCode getCasesByStatus(JusticeFlow::CaseStatus status, std::vector<Case *> &out_cases);
    };

} // namespace subsystem2
