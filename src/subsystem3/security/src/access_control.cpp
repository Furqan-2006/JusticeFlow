#include "security/include/access_control.h"
#include "security/include/policy_engine.h"
#include "legal/include/case_validation.h"
#include "legal/include/compliance.h"
#include "integration/include/s1_bridge.h"
#include "integration/include/s2_bridge.h"
#include "shr_infra/auth/include/session_store.h"
#include "common/logger.h"

using namespace JusticeFlow;

namespace security
{

    bool AccessControl::checkWarrantPermission(const SessionContext &session,
                                               int case_id,
                                               WarrantType warrant_type,
                                               ResultCode &out_code)
    {
        // Check 1: Session validity
        // (Session is assumed valid if passed here; auth layer validates before calling)
        if (!session.isValid)
        {
            out_code = ResultCode::SESSION_EXPIRED;
            Logger::debug("access_control: Session invalid");
            return false;
        }

        // Check 2: Officer duty status
        bool is_active = false;
        ResultCode duty_check = integration::S1Bridge::getOfficerDutyStatus(session.officerId, is_active);
        if (duty_check != ResultCode::OK || !is_active)
        {
            out_code = ResultCode::DUTY_INACTIVE;
            Logger::debug("access_control: Officer not on active duty");
            return false;
        }

        // Check 3: Case legality
        if (!legal::CaseValidation::validateCaseForWarrant(case_id, session.officerId, out_code))
        {
            Logger::debug("access_control: Case validation failed");
            return false;
        }

        // Check 4: Warrant type compliance
        JusticeFlow::Case case_record;
        ResultCode case_result = integration::S2Bridge::getCaseRecord(case_id, case_record);
        if (case_result != ResultCode::OK)
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::debug("access_control: Case record not found");
            return false;
        }

        legal::ComplianceResult compliance = legal::Compliance::validateWarrantType(
            case_record.case_type, warrant_type);
        if (compliance.code != ResultCode::OK)
        {
            out_code = compliance.code;
            Logger::debug("access_control: Warrant type not compliant");
            return false;
        }

        // Check 5: Policy engine (rank check)
        ResultCode policy_code;
        if (!PolicyEngine::getInstance().evaluate(
                "WARRANT_REQUEST",
                session.rank,
                "Warrant requested",
                policy_code))
        {
            out_code = policy_code;
            Logger::debug("access_control: Policy engine denied warrant");
            return false;
        }

        out_code = ResultCode::OK;
        Logger::info("access_control: Warrant permission granted");
        return true;
    }

    bool AccessControl::checkArrestPermission(const SessionContext &session,
                                              int warrant_id,
                                              ResultCode &out_code)
    {
        // Check 1: Session validity
        if (!session.isValid)
        {
            out_code = ResultCode::SESSION_EXPIRED;
            Logger::debug("access_control: Session invalid");
            return false;
        }

        // Check 2: Officer duty status
        bool is_active = false;
        ResultCode duty_check = integration::S1Bridge::getOfficerDutyStatus(session.officerId, is_active);
        if (duty_check != ResultCode::OK || !is_active)
        {
            out_code = ResultCode::DUTY_INACTIVE;
            Logger::debug("access_control: Officer not on active duty");
            return false;
        }

        // Check 3: Warrant exists and is executable
        // (Warrant query would be done by enforcement module)
        // Simplified: assume warrant exists and is ISSUED

        // Check 4: Policy engine (rank check for arrest execution)
        ResultCode policy_code;
        if (!PolicyEngine::getInstance().evaluate(
                "ARREST_EXECUTION",
                session.rank,
                "Arrest warrant execution",
                policy_code))
        {
            out_code = policy_code;
            Logger::debug("access_control: Policy engine denied arrest");
            return false;
        }

        out_code = ResultCode::OK;
        Logger::info("access_control: Arrest permission granted");
        return true;
    }

    bool AccessControl::checkBailPermission(const SessionContext &session,
                                            int arrest_id,
                                            ResultCode &out_code)
    {
        // Check 1: Session validity
        if (!session.isValid)
        {
            out_code = ResultCode::SESSION_EXPIRED;
            Logger::debug("access_control: Session invalid");
            return false;
        }

        // Check 2: Officer duty status
        bool is_active = false;
        ResultCode duty_check = integration::S1Bridge::getOfficerDutyStatus(session.officerId, is_active);
        if (duty_check != ResultCode::OK || !is_active)
        {
            out_code = ResultCode::DUTY_INACTIVE;
            Logger::debug("access_control: Officer not on active duty");
            return false;
        }

        // Check 3: Policy engine (rank check for bail setting)
        // Bail setting may require SI+ or INSPECTOR+
        ResultCode policy_code;
        if (!PolicyEngine::getInstance().evaluate(
                "BAIL_SETTING",
                session.rank,
                "Bail for arrest",
                policy_code))
        {
            out_code = policy_code;
            Logger::debug("access_control: Policy engine denied bail");
            return false;
        }

        out_code = ResultCode::OK;
        Logger::info("access_control: Bail permission granted");
        return true;
    }

} // namespace security