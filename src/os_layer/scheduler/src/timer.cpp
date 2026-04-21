#include "../include/timer.h"
#include "common/logger.h"

#define _POSIX_C_SOURCE 199309L

#include <sys/time.h>
#include <time.h>
#include <cstddef>

namespace Timer
{
    static struct timespec expected_next_tick = {0, 0};
    static int current_interval = 0;
    static constexpr long MAX_DRIFT_NS = 50000000;

    JusticeFlow::ResultCode arm(int interval_seconds)
    {
        struct itimerval it_val;
        it_val.it_value.tv_sec = interval_seconds;
        it_val.it_value.tv_usec = 0;
        it_val.it_interval.tv_sec = interval_seconds;
        it_val.it_interval.tv_usec = 0;

        if (setitimer(ITIMER_REAL, &it_val, nullptr) == -1)
        {
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        current_interval = interval_seconds;
        clock_gettime(CLOCK_MONOTONIC, &expected_next_tick);
        expected_next_tick.tv_sec += interval_seconds;

        return JusticeFlow::ResultCode::OK;
    }

    JusticeFlow::ResultCode disarm()
    {
        struct itimerval it_val;
        it_val.it_value.tv_sec = 0;
        it_val.it_value.tv_usec = 0;
        it_val.it_interval.tv_sec = 0;
        it_val.it_interval.tv_usec = 0;

        if (setitimer(ITIMER_REAL, &it_val, nullptr) == -1)
        {
            return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
        }

        return JusticeFlow::ResultCode::OK;
    }
}