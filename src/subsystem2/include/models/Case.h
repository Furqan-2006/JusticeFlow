#ifndef CASE_H
#define CASE_H

#include <string>
#include <vector>
#include <memory>
#include "../s2_types.h"
#include "Evidence.h"
#include "../patterns/ICaseStatusStrategy.h"

namespace subsystem2 {

/**
 * @brief Entity Class for Case.
 * Maps to the Draw.io UML Box for Case.
 */
class Case {
private:
    CaseDTO data;
    
    // Strategy Pattern: The current logic handler for status changes
    std::unique_ptr<ICaseStatusStrategy> status_strategy;

    // Aggregation: The Case holds a list of its evidence
    std::vector<Evidence*> evidence_list;

public:
    // Constructor initializes with DB data
    explicit Case(const CaseDTO& init_data);
    ~Case() = default;

    // --- Data Accessors ---
    int64_t getCaseId() const;
    std::string getFirNumber() const;
    JusticeFlow::CaseStatus getStatus() const;
    int64_t getLeadOfficerId() const;

    // --- Domain Logic ---
    void addEvidence(Evidence* ev);
    void setLeadOfficer(int64_t officer_id);
    
    // Updates raw status enum (called by the Strategy)
    void setStatusEnum(JusticeFlow::CaseStatus new_status);

    // --- Strategy Pattern Integration ---
    // Changes the active strategy dynamically
    void setStatusStrategy(std::unique_ptr<ICaseStatusStrategy> new_strategy);
    
    // Executes the strategy logic to attempt a state transition
    JusticeFlow::ResultCode transitionStatus(const JusticeFlow::SessionContext& session);
    // Returns DTO for database saving
    CaseDTO getDTO() const;
};

} // namespace subsystem2

#endif // CASE_H
