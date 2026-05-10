#include "../include/officer_manager.h"
#include "common/logger.h"
#include <string>

namespace JusticeFlow
{

    void OfficerMgr::request_officer_report(int officer_id, ReportType type)
    {
        std::string msg = "[S1: OfficerMgr] Processing report request for Officer UID: " + std::to_string(officer_id);
        Logger::info(msg.c_str());

        Report *requested_report = ReportFactory::create_report(type);

        if (requested_report != nullptr)
        {
            requested_report->generate(officer_id);
            delete requested_report; // Clean up memory
        }
        else
        {
            Logger::error("[S1: OfficerMgr] Failed to generate report.");
        }
    }

} // namespace JusticeFlow