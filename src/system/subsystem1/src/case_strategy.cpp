#include "../include/case_strategy.h"
#include "logger.h"

namespace JusticeFlow
{

    bool ConstableStrategy::can_close_case(CaseType type)
    {
        // Constables can close petty crimes
        if (type == CaseType::THEFT || type == CaseType::VANDALISM || type == CaseType::PUBLIC_DISTURBANCE)
        {
            return true;
        }
        Logger::error("[S1: Strategy] DENIED: Constable rank insufficient to close this case level.");
        return false;
    }

    bool ConstableStrategy::can_escalate_case()
    {
        return true;
    }

    bool InspectorStrategy::can_close_case(CaseType type)
    {
        // Inspectors can't close Murder or Terrorism
        if (type == CaseType::MURDER || type == CaseType::TERRORISM)
        {
            Logger::error("[S1: Strategy] DENIED: Inspector rank insufficient to close Capital/Federal cases.");
            return false;
        }
        return true;
    }

    bool InspectorStrategy::can_escalate_case()
    {
        return true;
    }

    bool SHOStrategy::can_close_case(CaseType type)
    {
        return true; // Full jurisdictional authority
    }

    bool SHOStrategy::can_escalate_case()
    {
        return true;
    }

} // namespace JusticeFlow