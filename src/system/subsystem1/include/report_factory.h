#pragma once

namespace JusticeFlow
{

    enum class ReportType
    {
        DAILY_SUMMARY,
        CHAIN_OF_CUSTODY,
        CASE_HISTORY
    };

    class Report
    {
    public:
        virtual ~Report() = default;
        virtual void generate(int officer_id) = 0;
    };

    // Concrete Products
    class DailySummaryReport : public Report
    {
    public:
        void generate(int officer_id) override;
    };

    class ChainOfCustodyReport : public Report
    {
    public:
        void generate(int officer_id) override;
    };

    class CaseHistoryReport : public Report
    {
    public:
        void generate(int officer_id) override;
    };

    // The Factory
    class ReportFactory
    {
    public:
        static Report *create_report(ReportType type);
    };

} // namespace JusticeFlow