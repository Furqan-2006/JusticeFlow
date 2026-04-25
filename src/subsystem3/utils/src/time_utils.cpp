#include "utils/include/time_utils.h"
#include "../../common/logger.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace time_utils
{

    bool isExpired(time_t valid_until)
    {
        time_t now = std::time(nullptr);
        return now > valid_until;
    }

    int daysBetween(time_t from, time_t to)
    {
        const int SECONDS_PER_DAY = 86400;
        int diff = static_cast<int>(to - from);
        return diff / SECONDS_PER_DAY;
    }

    time_t midnight(time_t date, int days_offset)
    {
        struct tm tm_info;

#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&tm_info, &date);
#else
        gmtime_r(&date, &tm_info);
#endif

        // Set to midnight (00:00:00) UTC
        tm_info.tm_hour = 0;
        tm_info.tm_min = 0;
        tm_info.tm_sec = 0;

        // Add days offset
        tm_info.tm_mday += days_offset;

#if defined(_WIN32) || defined(_WIN64)
        return _mkgmtime(&tm_info);
#else
        // POSIX: use timegm (non-POSIX but widely available)
        return timegm(&tm_info);
#endif
    }

    std::string toReadableString(time_t t)
    {
        struct tm tm_info;

#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&tm_info, &t);
#else
        gmtime_r(&t, &tm_info);
#endif

        char buffer[30];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &tm_info);
        return std::string(buffer);
    }

} // namespace time_utils