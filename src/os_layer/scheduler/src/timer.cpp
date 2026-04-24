// CRITICAL FIX #10.1: Feature test macro BEFORE all other includes
#define _POSIX_C_SOURCE 199309L

#include "../include/timer.h"
#include "common/logger.h"

#include <sys/time.h>
#include <cstring>

namespace Timer
{
    // CRITICAL FIX #10.2: Removed dead code (expected_next_tick, current_interval, MAX_DRIFT_NS)
    // Drift detection not yet implemented - should be added in future refactor

    JusticeFlow::ResultCode arm(int interval_seconds)
    {
        if (interval_seconds <= 0)
        {
            Logger::error("[Timer] Invalid interval: must be > 0");
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }

        struct itimerval it_val;
        std::memset(&it_val, 0, sizeof(it_val));

        it_val.it_value.tv_sec = interval_seconds;
        it_val.it_value.tv_usec = 0;
        it_val.it_interval.tv_sec = interval_seconds;
        it_val.it_interval.tv_usec = 0;

        if (setitimer(ITIMER_REAL, &it_val, nullptr) == -1)
        {
            Logger::error("[Timer] setitimer() failed during arm()");
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        char log_buf[128];
        std::snprintf(log_buf, sizeof(log_buf), "[Timer] Interval timer armed for %d seconds", interval_seconds);
        Logger::info(log_buf);

        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode disarm()
    {
        struct itimerval it_val;
        std::memset(&it_val, 0, sizeof(it_val));

        // Zero out both it_value and it_interval to disarm
        it_val.it_value.tv_sec = 0;
        it_val.it_value.tv_usec = 0;
        it_val.it_interval.tv_sec = 0;
        it_val.it_interval.tv_usec = 0;

        if (setitimer(ITIMER_REAL, &it_val, nullptr) == -1)
        {
            Logger::error("[Timer] setitimer() failed during disarm()");
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        Logger::info("[Timer] Interval timer disarmed");
        return JusticeFlow::ResultCode::OK;
    }
}