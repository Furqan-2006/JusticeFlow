#ifndef CASE_STRATEGY_H
#define CASE_STRATEGY_H

#include <iostream>

// Represents the severity/type of a case
enum CaseType {
    PETTY_THEFT,
    ASSAULT,
    HOMICIDE,
    FEDERAL_OFFENSE
};

// Represents the current status of a case
enum CaseStatus {
    OPEN,
    UNDER_INVESTIGATION,
    PENDING_TRIAL,
    CLOSED
};

// ---------------------------------------------------------
// The Abstract Strategy
// ---------------------------------------------------------
class CaseTransitionStrategy {
public:
    virtual ~CaseTransitionStrategy() {}
    
    // Pure virtual functions defining the contract for state transitions
    virtual bool can_close_case(CaseType type) = 0;
    virtual bool can_escalate_case() = 0;
};

// ---------------------------------------------------------
// Concrete Strategy: Constable
// Constables can only close petty crimes.
// ---------------------------------------------------------
class ConstableStrategy : public CaseTransitionStrategy {
public:
    bool can_close_case(CaseType type) override {
        if (type == PETTY_THEFT) {
            return true;
        }
        std::cerr << "[S1: Strategy] DENIED: Constable rank insufficient to close high-level case.\n";
        return false;
    }
    
    bool can_escalate_case() override {
        return true; // Constables can always escalate a case to a higher rank
    }
};

// ---------------------------------------------------------
// Concrete Strategy: Inspector
// Inspectors can close up to assault cases.
// ---------------------------------------------------------
class InspectorStrategy : public CaseTransitionStrategy {
public:
    bool can_close_case(CaseType type) override {
        if (type == PETTY_THEFT || type == ASSAULT) {
            return true;
        }
        std::cerr << "[S1: Strategy] DENIED: Inspector rank insufficient to close homicide/federal cases.\n";
        return false;
    }
    
    bool can_escalate_case() override {
        return true;
    }
};

// ---------------------------------------------------------
// Concrete Strategy: SHO (Station House Officer)
// SHOs have full jurisdictional authority.
// ---------------------------------------------------------
class SHOStrategy : public CaseTransitionStrategy {
public:
    bool can_close_case(CaseType type) override {
        return true; // SHO can close any case within their jurisdiction
    }
    
    bool can_escalate_case() override {
        return true; 
    }
};

#endif // CASE_STRATEGY_H
