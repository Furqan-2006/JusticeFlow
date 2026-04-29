#include "../include/case_manager.h"
#include "logger.h"
#include <string>

namespace JusticeFlow
{

    CaseManager::CaseManager() : active_strategy(nullptr), current_case(nullptr) {}

    CaseManager::~CaseManager()
    {
        if (active_strategy != nullptr)
        {
            delete active_strategy;
        }
    }

    void CaseManager::load_case(Case *c)
    {
        current_case = c;
    }

    void CaseManager::set_authorization_strategy(CaseTransitionStrategy *strategy)
    {
        if (active_strategy != nullptr)
        {
            delete active_strategy;
        }
        active_strategy = strategy;
    }

    bool CaseManager::attempt_case_closure()
    {
        if (current_case == nullptr || active_strategy == nullptr)
        {
            Logger::error("[S1: CaseManager] Error: Context not fully initialized.");
            return false;
        }

        if (active_strategy->can_close_case(current_case->case_type))
        {
            current_case->case_status = CaseStatus::CLOSED;

            std::string msg = "[S1: CaseManager] GRANTED: Case " + std::to_string(current_case->case_id) + " successfully closed.";
            Logger::info(msg.c_str());
            return true;
        }

        return false;
    }

} // namespace JusticeFlow