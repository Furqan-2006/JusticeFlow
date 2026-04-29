#pragma once

#include "common.h"
#include "constants.h"

namespace JusticeFlow
{

    // ---------------------------------------------------------
    // The Abstract Strategy
    // ---------------------------------------------------------
    class CaseTransitionStrategy
    {
    public:
        virtual ~CaseTransitionStrategy() = default;

        // Contract for state transitions
        virtual bool can_close_case(CaseType type) = 0;
        virtual bool can_escalate_case() = 0;
    };

    // Concrete Strategy: Constable (Can only close minor civilian cases)
    class ConstableStrategy : public CaseTransitionStrategy
    {
    public:
        bool can_close_case(CaseType type) override;
        bool can_escalate_case() override;
    };

    // Concrete Strategy: Inspector (Can close up to major civilian / minor criminal)
    class InspectorStrategy : public CaseTransitionStrategy
    {
    public:
        bool can_close_case(CaseType type) override;
        bool can_escalate_case() override;
    };

    // Concrete Strategy: SHO (Station House Officer - Full Authority)
    class SHOStrategy : public CaseTransitionStrategy
    {
    public:
        bool can_close_case(CaseType type) override;
        bool can_escalate_case() override;
    };

} // namespace JusticeFlow