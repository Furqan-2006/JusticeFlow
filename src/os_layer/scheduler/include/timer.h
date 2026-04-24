#pragma once

#include "common/constants.h"

namespace Timer
{
    /**
     * Arm the interval timer to fire every interval_seconds
     * Only safe to call from main event loop, not from signal handlers
     */
    JusticeFlow::ResultCode arm(int interval_seconds);

    /**
     * Disarm the interval timer
     * Only safe to call from main event loop, not from signal handlers
     */
    JusticeFlow::ResultCode disarm();
};