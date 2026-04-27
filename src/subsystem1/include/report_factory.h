#ifndef REPORT_FACTORY_H
#define REPORT_FACTORY_H

#include <iostream>


enum ReportType {
    DAILY_SUMMARY,
    CHAIN_OF_CUSTODY,
    CASE_HISTORY
};






class Report {
public:
    virtual ~Report() {}
    virtual void generate(int officer_id) = 0;
};

// ---------------------------------------------------------
// Concrete Products
// ---------------------------------------------------------
class DailySummaryReport : public Report {
public:
    void generate(int officer_id) override {
        // In reality, this would query the DB for the officer's shift actions
        std::cout << "[S1: ReportFactory] Generating Daily Summary for Officer " << officer_id << "...\n";
    }
};

class ChainOfCustodyReport : public Report {
public:
    void generate(int officer_id) override {
        std::cout << "[S1: ReportFactory] Generating Chain of Custody log for Officer " << officer_id << "...\n";
    }
};

class CaseHistoryReport : public Report {
public:
    void generate(int officer_id) override {
        std::cout << "[S1: ReportFactory] Generating historical case performance for Officer " << officer_id << "...\n";
    }
};





class ReportFactory {
public:
    // The Factory Method: returns a base pointer to a newly allocated concrete object
    static Report* create_report(ReportType type) {
        switch (type) {
            case DAILY_SUMMARY:
                return new DailySummaryReport();
            case CHAIN_OF_CUSTODY:
                return new ChainOfCustodyReport();
            case CASE_HISTORY:
                return new CaseHistoryReport();
            default:
                std::cerr << "[S1: ReportFactory] Unknown report type requested.\n";
                return nullptr;
        }
    }
};

#endif // REPORT_FACTORY_H
