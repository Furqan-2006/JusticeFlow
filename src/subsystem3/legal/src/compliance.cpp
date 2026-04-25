#include "legal/include/compliance.h"
#include "utils/include/rule_utils.h"
#include "../../common/logger.h"
#include <sstream>
#include <algorithm>
#include <cctype>

using namespace JusticeFlow;

namespace legal
{

    ComplianceResult Compliance::validateWarrantType(CaseType case_type, WarrantType warrant_type)
    {
        switch (warrant_type)
        {
        case WarrantType::ARREST:
            // ARREST: All crime types allowed
            Logger::info("compliance: ARREST warrant allowed for all crime types");
            return ComplianceResult(ResultCode::OK, "");

        case WarrantType::SEARCH:
            // SEARCH: BURGLARY, ROBBERY, ASSAULT, KIDNAPPING, HUMAN_TRAFFICKING, TERRORISM, DRUG_TRAFFICKING, GANG_ACTIVITY
            if (case_type == CaseType::BURGLARY || case_type == CaseType::ROBBERY ||
                case_type == CaseType::ARMED_ROBBERY || case_type == CaseType::ASSAULT ||
                case_type == CaseType::AGGRAVATED_ASSAULT || case_type == CaseType::KIDNAPPING ||
                case_type == CaseType::HUMAN_TRAFFICKING || case_type == CaseType::TERRORISM ||
                case_type == CaseType::DRUG_TRAFFICKING || case_type == CaseType::GANG_ACTIVITY)
            {
                Logger::info("compliance: SEARCH warrant allowed for this crime type");
                return ComplianceResult(ResultCode::OK, "");
            }
            else
            {
                std::string reason = "Search warrant not allowed for this crime type";
                Logger::debug("compliance: Search warrant denied");
                return ComplianceResult(ResultCode::INVALID_INPUT, reason);
            }

        case WarrantType::SEIZURE:
            // SEIZURE: ROBBERY, DRUG_TRAFFICKING, TERRORISM, GANG_ACTIVITY
            if (case_type == CaseType::ROBBERY || case_type == CaseType::ARMED_ROBBERY ||
                case_type == CaseType::DRUG_TRAFFICKING || case_type == CaseType::TERRORISM ||
                case_type == CaseType::GANG_ACTIVITY)
            {
                Logger::info("compliance: SEIZURE warrant allowed for this crime type");
                return ComplianceResult(ResultCode::OK, "");
            }
            else
            {
                std::string reason = "Seizure warrant only allowed for contraband-related crimes";
                Logger::debug("compliance: Seizure warrant denied");
                return ComplianceResult(ResultCode::INVALID_INPUT, reason);
            }

        default:
            Logger::error("compliance: Unknown warrant type");
            return ComplianceResult(ResultCode::INVALID_INPUT, "Unknown warrant type");
        }
    }

    ComplianceResult Compliance::validateBailAmount(BailType bail_type, uint64_t amount_paise)
    {
        uint64_t min_paise = 0;
        uint64_t max_paise = 0;
        std::string type_name;

        switch (bail_type)
        {
        case BailType::REGULAR:
            min_paise = 500000;   // ₹5,000
            max_paise = 50000000; // ₹500,000
            type_name = "REGULAR";
            break;
        case BailType::ANTICIPATORY:
            min_paise = 1000000;   // ₹10,000
            max_paise = 100000000; // ₹1,000,000
            type_name = "ANTICIPATORY";
            break;
        case BailType::INTERIM:
            min_paise = 250000;   // ₹2,500
            max_paise = 10000000; // ₹100,000
            type_name = "INTERIM";
            break;
        case BailType::SURETY:
            min_paise = 2500000;   // ₹25,000
            max_paise = 500000000; // ₹5,000,000
            type_name = "SURETY";
            break;
        default:
            Logger::error("compliance: Unknown bail type");
            return ComplianceResult(ResultCode::INVALID_INPUT, "Unknown bail type");
        }

        // Check bounds
        if (amount_paise < min_paise)
        {
            std::stringstream reason_ss;
            reason_ss << "Amount below minimum for " << type_name;
            Logger::debug("compliance: Bail amount below minimum");
            return ComplianceResult(ResultCode::INVALID_INPUT, reason_ss.str());
        }

        if (amount_paise > max_paise)
        {
            std::stringstream reason_ss;
            reason_ss << "Amount exceeds maximum for " << type_name;
            Logger::debug("compliance: Bail amount exceeds maximum");
            return ComplianceResult(ResultCode::INVALID_INPUT, reason_ss.str());
        }

        Logger::info("compliance: Bail amount validated");
        return ComplianceResult(ResultCode::OK, "");
    }

    ComplianceResult Compliance::checkSOPCompliance(const std::string &operation_type,
                                                    const std::string &context)
    {
        std::string op_upper = operation_type;
        std::transform(op_upper.begin(), op_upper.end(), op_upper.begin(),
                       [](unsigned char c)
                       { return std::toupper(c); });

        if (op_upper == "NON_STANDARD_WARRANT")
        {
            std::string reason = "Requires DSP+ rank approval";
            Logger::debug("compliance: Non-standard warrant requires escalation");
            return ComplianceResult(ResultCode::INVALID_STATE, reason);
        }
        else if (op_upper == "UNUSUAL_EVIDENCE_HANDLING")
        {
            std::string reason = "Requires SI+ rank and SOP review";
            Logger::debug("compliance: Unusual evidence handling requires escalation");
            return ComplianceResult(ResultCode::INVALID_STATE, reason);
        }
        else if (op_upper == "MULTI_CASE_LINKAGE")
        {
            std::string reason = "Requires INSPECTOR+ approval";
            Logger::debug("compliance: Multi-case linkage requires approval");
            return ComplianceResult(ResultCode::INVALID_STATE, reason);
        }
        else if (op_upper == "CROSS_JURISDICTION")
        {
            std::string reason = "Requires DSP+ approval and inter-station notification";
            Logger::debug("compliance: Cross-jurisdiction operation requires approval");
            return ComplianceResult(ResultCode::INVALID_STATE, reason);
        }
        else if (op_upper == "STANDARD_OPERATION")
        {
            Logger::info("compliance: Standard operation approved");
            return ComplianceResult(ResultCode::OK, "");
        }
        else
        {
            std::string reason = "Unknown operation type";
            Logger::error("compliance: Unknown operation type");
            return ComplianceResult(ResultCode::INVALID_INPUT, reason);
        }
    }

} // namespace legal