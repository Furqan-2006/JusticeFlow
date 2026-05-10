#pragma once

namespace JusticeFlow
{

    class Observer
    {
    public:
        virtual ~Observer() = default;
        virtual void update(int case_id, int evidence_id) = 0;
    };

} // namespace JusticeFlow