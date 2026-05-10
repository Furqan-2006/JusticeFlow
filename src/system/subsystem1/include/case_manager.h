#pragma once

#include "common.h"
#include "case_strategy.h"

namespace JusticeFlow
{

    class CaseManager
    {
    private:
        CaseTransitionStrategy *active_strategy;
        Case *current_case; // Using JusticeFlow::Case from common.h

    public:
        CaseManager();
        ~CaseManager();

        void load_case(Case *c);
        void set_authorization_strategy(CaseTransitionStrategy *strategy);
        bool attempt_case_closure();
    };

} // namespace JusticeFlow