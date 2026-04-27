#ifndef OFFICER_MGR_H
#define OFFICER_MGR_H

#include "report_factory.h"
#include <iostream>

class OfficerMgr {
public:

    OfficerMgr() {}
    
    
    ~OfficerMgr() {}

    // Expose a clean API to the rest of the system to generate reports
    void request_officer_report(int officer_id, ReportType type) {
        std::cout << "[S1: OfficerMgr] Processing report request for Officer UID: " << officer_id << "\n";
        
        // 1. Ask the Factory to instantiate the correct report object
        Report* requested_report = ReportFactory::create_report(type);
        
        
        if (requested_report != nullptr)
        {
            // 2. Execute the report logic
            requested_report->generate(officer_id);
            
            // 3. Clean up the heap allocation to prevent memory leaks
            delete requested_report;
        } 
        
        else 
        
        {
            std::cerr << "[S1: OfficerMgr] Failed to generate report.\n";
        }
    }
};






#endif 
