#include "../include/report_factory.h"
#include "logger.h"
#include <string>

namespace JusticeFlow
{

    void DailySummaryReport::generate(int officer_id)
    {
        std::string msg = "[S1: ReportFactory] Generating Daily Summary for Officer " + std::to_string(officer_id);
        Logger::info(msg.c_str());
    }

    void ChainOfCustodyReport::generate(int officer_id)
    {
        std::string msg = "[S1: ReportFactory] Generating Chain of Custody log for Officer " + std::to_string(officer_id);
        Logger::info(msg.c_str());
    }

    void CaseHistoryReport::generate(int officer_id)
    {
        std::string msg = "[S1: ReportFactory] Generating historical case performance for Officer " + std::to_string(officer_id);
        Logger::info(msg.c_str());
    }

    Report *ReportFactory::create_report(ReportType type)
    {
        switch (type)
        {
        case ReportType::DAILY_SUMMARY:
            return new DailySummaryReport();
        case ReportType::CHAIN_OF_CUSTODY:
            return new ChainOfCustodyReport();
        case ReportType::CASE_HISTORY:
            return new CaseHistoryReport();
        default:
            Logger::error("[S1: ReportFactory] Unknown report type requested.");
            return nullptr;
        }
    }

} // namespace JusticeFlow