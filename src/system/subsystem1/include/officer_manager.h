#pragma once

#include "report_factory.h"

namespace JusticeFlow
{

    class OfficerMgr
    {
    public:
        OfficerMgr() = default;
        ~OfficerMgr() = default;

        void request_officer_report(int officer_id, ReportType type);
    };

} // namespace JusticeFlow