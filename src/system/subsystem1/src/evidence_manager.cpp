#include "evidence_mgr.h"
#include "common/logger.h"
#include <string>

namespace JusticeFlow
{

    EvidenceMgr::EvidenceMgr() : observer_count(0)
    {
        for (int i = 0; i < MAX_OBSERVERS; i++)
        {
            observers[i] = nullptr;
        }
    }

    void EvidenceMgr::attach(Observer *obs)
    {
        if (observer_count < MAX_OBSERVERS)
        {
            observers[observer_count++] = obs;
        }
        else
        {
            Logger::error("[S2: EvidenceMgr] Error: Maximum observer limit reached.");
        }
    }

    void EvidenceMgr::detach(Observer *obs)
    {
        for (int i = 0; i < observer_count; i++)
        {
            if (observers[i] == obs)
            {
                for (int j = i; j < observer_count - 1; j++)
                {
                    observers[j] = observers[j + 1];
                }
                observers[observer_count - 1] = nullptr;
                observer_count--;
                break;
            }
        }
    }

    void EvidenceMgr::notify_all(int case_id, int evidence_id)
    {
        for (int i = 0; i < observer_count; i++)
        {
            if (observers[i] != nullptr)
            {
                observers[i]->update(case_id, evidence_id);
            }
        }
    }

    void EvidenceMgr::add_evidence(int case_id, int evidence_id, const char *description)
    {
        std::string msg = "[S2: EvidenceMgr] Logging Evidence " + std::to_string(evidence_id) +
                          " for Case " + std::to_string(case_id) + ": " + description;
        Logger::info(msg.c_str());

        // Notify the Audit Logger and AI Triggers
        notify_all(case_id, evidence_id);
    }

} // namespace JusticeFlow