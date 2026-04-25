#include "utils/include/rule_utils.h"
#include "common/constants.h"
#include "../../common/logger.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>

using namespace JusticeFlow;

namespace rule_utils
{

    bool meetsMinimumRank(OfficerRank actual, OfficerRank minimum)
    {
        return static_cast<int>(actual) >= static_cast<int>(minimum);
    }

    std::string rankToString(OfficerRank rank)
    {
        switch (rank)
        {
        case OfficerRank::CONSTABLE:
            return "CONSTABLE";
        case OfficerRank::HEAD_CONSTABLE:
            return "HEAD_CONSTABLE";
        case OfficerRank::ASI:
            return "ASI";
        case OfficerRank::SI:
            return "SI";
        case OfficerRank::INSPECTOR:
            return "INSPECTOR";
        case OfficerRank::DSP:
            return "DSP";
        case OfficerRank::SP:
            return "SP";
        case OfficerRank::SSP:
            return "SSP";
        case OfficerRank::DIG:
            return "DIG";
        case OfficerRank::ADDL_IG:
            return "ADDL_IG";
        case OfficerRank::IGP:
            return "IGP";
        default:
            return "UNKNOWN";
        }
    }

    OfficerRank rankFromString(const std::string &str)
    {
        std::string upper_str = str;
        std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(),
                       [](unsigned char c)
                       { return std::toupper(c); });

        if (upper_str == "CONSTABLE")
            return OfficerRank::CONSTABLE;
        if (upper_str == "HEAD_CONSTABLE")
            return OfficerRank::HEAD_CONSTABLE;
        if (upper_str == "ASI")
            return OfficerRank::ASI;
        if (upper_str == "SI")
            return OfficerRank::SI;
        if (upper_str == "INSPECTOR")
            return OfficerRank::INSPECTOR;
        if (upper_str == "DSP")
            return OfficerRank::DSP;
        if (upper_str == "SP")
            return OfficerRank::SP;
        if (upper_str == "SSP")
            return OfficerRank::SSP;
        if (upper_str == "DIG")
            return OfficerRank::DIG;
        if (upper_str == "ADDL_IG")
            return OfficerRank::ADDL_IG;
        if (upper_str == "IGP")
            return OfficerRank::IGP;

        Logger::error("rule_utils: Invalid rank string");
        throw std::invalid_argument("Invalid rank string");
    }

    int severityWeight(const std::string &severity)
    {
        std::string upper_severity = severity;
        std::transform(upper_severity.begin(), upper_severity.end(), upper_severity.begin(),
                       [](unsigned char c)
                       { return std::toupper(c); });

        if (upper_severity == "CRITICAL")
            return 4;
        if (upper_severity == "HIGH")
            return 3;
        if (upper_severity == "MEDIUM")
            return 2;
        if (upper_severity == "LOW")
            return 1;

        Logger::debug("rule_utils: Unknown severity level");
        return 0; // Default: no escalation
    }

    std::string severityFromWeight(int weight)
    {
        switch (weight)
        {
        case 4:
            return "CRITICAL";
        case 3:
            return "HIGH";
        case 2:
            return "MEDIUM";
        case 1:
            return "LOW";
        default:
            return "UNKNOWN";
        }
    }

} // namespace rule_utils