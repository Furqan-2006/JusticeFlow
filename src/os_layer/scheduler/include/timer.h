#pragma once

// CRITICAL FIX #10.1: Feature test macro BEFORE system headers
#define _POSIX_C_SOURCE 199309L

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