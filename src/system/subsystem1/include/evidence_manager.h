#pragma once

#include "observer.h"

#define MAX_OBSERVERS 10

namespace JusticeFlow
{

    class EvidenceMgr
    {
    private:
        Observer *observers[MAX_OBSERVERS];
        int observer_count;

    public:
        EvidenceMgr();

        void attach(Observer *obs);
        void detach(Observer *obs);
        void notify_all(int case_id, int evidence_id);
        void add_evidence(int case_id, int evidence_id, const char *description);
    };

} // namespace JusticeFlow