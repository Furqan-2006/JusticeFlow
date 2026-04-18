#pragma once

#include "common/constants.h"

namespace Timer
{
    JusticeFlow::ResultCode arm(int interval_seconds);

    JusticeFlow::ResultCode disarm();
};