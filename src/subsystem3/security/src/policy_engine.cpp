#include "security/include/policy_engine.h"
#include "utils/include/rule_utils.h"
#include "common/logger.h"

using namespace JusticeFlow;

namespace security
{

    // ===========================
    // InspectorHandler
    // ===========================

    InspectorHandler::InspectorHandler() : next_handler_(nullptr)
    {
        min_rank_ = OfficerRank::INSPECTOR;
    }

    ResultCode InspectorHandler::handle(OfficerRank officer_rank, int severity)
    {
        // INSPECTOR can handle operations with severity <= 6
        if (rule_utils::meetsMinimumRank(officer_rank, OfficerRank::INSPECTOR) && severity <= 6)
        {
            return ResultCode::OK;
        }

        // If severity > 6, escalate to next handler
        if (next_handler_ && severity > 6)
        {
            return next_handler_->handle(officer_rank, severity);
        }

        // If officer rank < INSPECTOR, escalate
        if (next_handler_)
        {
            return next_handler_->handle(officer_rank, severity);
        }

        return ResultCode::RANK_INSUFFICIENT;
    }

    // ===========================
    // DSPHandler
    // ===========================

    DSPHandler::DSPHandler() : next_handler_(nullptr)
    {
        min_rank_ = OfficerRank::DSP;
    }

    ResultCode DSPHandler::handle(OfficerRank officer_rank, int severity)
    {
        // DSP can handle operations with severity <= 8
        if (rule_utils::meetsMinimumRank(officer_rank, OfficerRank::DSP) && severity <= 8)
        {
            return ResultCode::OK;
        }

        // If severity > 8, escalate to next handler
        if (next_handler_ && severity > 8)
        {
            return next_handler_->handle(officer_rank, severity);
        }

        // If officer rank < DSP, escalate
        if (next_handler_)
        {
            return next_handler_->handle(officer_rank, severity);
        }

        return ResultCode::RANK_INSUFFICIENT;
    }

    // ===========================
    // SPHandler
    // ===========================

    SPHandler::SPHandler() : next_handler_(nullptr)
    {
        min_rank_ = OfficerRank::SP;
    }

    ResultCode SPHandler::handle(OfficerRank officer_rank, int severity)
    {
        // SP can handle any operation (severity <= 10)
        if (rule_utils::meetsMinimumRank(officer_rank, OfficerRank::SP))
        {
            return ResultCode::OK;
        }

        // If officer rank < SP, cannot proceed at any level
        return ResultCode::RANK_INSUFFICIENT;
    }

    // ===========================
    // PolicyEngine
    // ===========================

    PolicyEngine::PolicyEngine()
    {
        // Build the chain: INSPECTOR → DSP → SP
        inspector_handler_.setNext(&dsp_handler_);
        dsp_handler_.setNext(&sp_handler_);
    }

    PolicyEngine &PolicyEngine::getInstance()
    {
        static PolicyEngine instance;
        return instance;
    }

    bool PolicyEngine::evaluate(const std::string &operation_type,
                                OfficerRank officer_rank,
                                const std::string &context,
                                ResultCode &out_code)
    {
        // Determine severity from operation type
        int severity = 5; // Default severity for standard operations

        if (operation_type.find("SEARCH_WARRANT") != std::string::npos ||
            operation_type.find("SURVEILLANCE") != std::string::npos)
        {
            severity = 6;
        }
        else if (operation_type.find("WITNESS_PROTECTION") != std::string::npos ||
                 operation_type.find("MULTI_CASE") != std::string::npos)
        {
            severity = 7;
        }
        else if (operation_type.find("CROSS_JURISDICTION") != std::string::npos ||
                 operation_type.find("ASSET_SEIZURE") != std::string::npos)
        {
            severity = 8;
        }

        // Route through chain of responsibility
        out_code = inspector_handler_.handle(officer_rank, severity);

        if (out_code == ResultCode::OK)
        {
            std::string msg = "policy_engine: Operation approved - " + operation_type;
            Logger::info(msg.c_str());
            return true;
        }
        else
        {
            std::string msg = "policy_engine: Operation requires escalation - " + operation_type;
            Logger::debug(msg.c_str());
            return false;
        }
    }

} // namespace security