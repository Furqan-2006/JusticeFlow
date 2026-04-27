#ifndef CASE_MANAGER_H
#define CASE_MANAGER_H

#include "case_strategy.h"

// A raw struct representing a database record
struct CaseRecord {
    int case_id;
    CaseType type;
    CaseStatus status;
};

class CaseManager {
private:
    CaseTransitionStrategy* active_strategy;
    CaseRecord* current_case;

public:
    CaseManager() : active_strategy(nullptr), current_case(nullptr) {}

    // Destructor must clean up raw pointers
    ~CaseManager() {
        if (active_strategy != nullptr) {
            delete active_strategy;
        }
    }

    // Load a case into the manager's context
    void load_case(CaseRecord* c) {
        current_case = c;
    }

    // Dynamically inject the authorization strategy based on logged-in officer
    void set_authorization_strategy(CaseTransitionStrategy* strategy) {
        if (active_strategy != nullptr) {
            delete active_strategy; // Prevent memory leaks from re-assignment
        }
        active_strategy = strategy;
    }

    // Execute the transition request using the loaded strategy
    bool attempt_case_closure() {
        if (current_case == nullptr || active_strategy == nullptr) {
            std::cerr << "[S1: CaseManager] Error: Context not fully initialized.\n";
            return false;
        }

        if (active_strategy->can_close_case(current_case->type)) {
            current_case->status = CLOSED;
            std::cout << "[S1: CaseManager] GRANTED: Case " << current_case->case_id << " successfully closed.\n";
            return true;
        }

        return false;
    }
};

#endif // CASE_MANAGER_H
