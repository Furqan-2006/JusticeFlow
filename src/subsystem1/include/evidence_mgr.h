#ifndef EVIDENCE_MGR_H
#define EVIDENCE_MGR_H

#include "observer.h"
#include <iostream>

#define MAX_OBSERVERS 10

class EvidenceMgr {
private:
    Observer* observers[MAX_OBSERVERS];
    int observer_count;

public:
    EvidenceMgr() : observer_count(0) {
        for (int i = 0; i < MAX_OBSERVERS; i++) {
            observers[i] = nullptr;
        }
    }

    // Register a new listener
    void attach(Observer* obs) {
        if (observer_count < MAX_OBSERVERS) {
            observers[observer_count++] = obs;
        } else {
            std::cerr << "[S1: EvidenceMgr] Error: Maximum observer limit reached.\n";
        }
    }



    // listener hatao
    void detach(Observer* obs) {
        for (int i = 0; i < observer_count; i++) {
            if (observers[i] == obs) {
                // Shift the remaining observers left to fill the gap
                for (int j = i; j < observer_count - 1; j++) {
                    observers[j] = observers[j + 1];
                }
                observers[observer_count - 1] = nullptr;
                observer_count--;
                break;
            }
        }
    }

    // Trigger the update() method on all listeners
    void notify_all(int case_id, int evidence_id) {
        for (int i = 0; i < observer_count; i++) {
            if (observers[i] != nullptr) {
                observers[i]->update(case_id, evidence_id);
            }
        }
    }

    
    
    
    
    void add_evidence(int case_id, int evidence_id, const char* description) {
        std::cout << "[S1: EvidenceMgr] Logging Evidence " << evidence_id 
                  << " for Case " << case_id << ": " << description << "\n";
        
        // (In a complete flow, we would write this to Abdullah's database via IPC here)
        
        // Instantly notify the Audit Logger and AI Triggers
        notify_all(case_id, evidence_id);
    }
};





#endif 
