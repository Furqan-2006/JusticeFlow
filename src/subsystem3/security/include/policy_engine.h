#pragma once

#include <string>
#include "common/constants.h"
#include "utils/include/rule_utils.h"

namespace security
{

    /**
     * @file policy_engine.h
     * @brief Chain of Responsibility pattern for rank-based operation escalation
     *
     * Implements hierarchical approval chain: INSPECTOR → DSP → SP
     * Each handler decides whether to approve, escalate, or reject operations
     * based on rank requirements and requesting officer's rank.
     *
     * Called before any operation with rank requirements (warrants, arrests, bail, etc.).
     *
     * Thread Safety: All functions are stateless and thread-safe.
     *
     * Dependencies: utils/rule_utils.h, legal/compliance.h, common/constants.h
     */

    /**
     * @class RankHandler
     * @brief Base class for rank-based operation handlers
     *
     * Implements one level of the Chain of Responsibility pattern.
     * Each handler knows its minimum rank and delegates to the next handler if needed.
     */
    class RankHandler
    {
    protected:
        JusticeFlow::OfficerRank min_rank_;
        RankHandler *next_handler_;

    public:
        virtual ~RankHandler() = default;

        /**
         * @brief Sets the next handler in the chain
         */
        void setNext(RankHandler *next)
        {
            next_handler_ = next;
        }

        /**
         * @brief Handles the request or delegates to next handler
         *
         * @param officer_rank The rank of the requesting officer
         * @param severity Severity level of operation (1-10, from rule_utils)
         * @return OK if approved, RANK_INSUFFICIENT if needs escalation, INVALID_STATE if rejected
         */
        virtual JusticeFlow::ResultCode handle(JusticeFlow::OfficerRank officer_rank, int severity) = 0;
    };

    /**
     * @class InspectorHandler
     * @brief Handles operations for INSPECTOR rank and below
     */
    class InspectorHandler : public RankHandler
    {
    public:
        InspectorHandler();
        JusticeFlow::ResultCode handle(JusticeFlow::OfficerRank officer_rank, int severity) override;
    };

    /**
     * @class DSPHandler
     * @brief Handles operations for DSP rank (escalated from INSPECTOR)
     */
    class DSPHandler : public RankHandler
    {
    public:
        DSPHandler();
        JusticeFlow::ResultCode handle(JusticeFlow::OfficerRank officer_rank, int severity) override;
    };

    /**
     * @class SPHandler
     * @brief Handles operations for SP rank (highest escalation)
     */
    class SPHandler : public RankHandler
    {
    public:
        SPHandler();
        JusticeFlow::ResultCode handle(JusticeFlow::OfficerRank officer_rank, int severity) override;
    };

    /**
     * @class PolicyEngine
     * @brief Main entry point for rank-based operation evaluation
     *
     * Manages the Chain of Responsibility and routes requests through handlers.
     */
    class PolicyEngine
    {
    private:
        InspectorHandler inspector_handler_;
        DSPHandler dsp_handler_;
        SPHandler sp_handler_;

        PolicyEngine();
        ~PolicyEngine() = default;

        PolicyEngine(const PolicyEngine &) = delete;
        PolicyEngine &operator=(const PolicyEngine &) = delete;

    public:
        /**
         * @brief Gets the singleton instance
         */
        static PolicyEngine &getInstance();

        /**
         * Evaluates an operation against the rank-based approval chain.
         *
         * Routing logic:
         *   1. If officer_rank meets requirement → OK (proceed)
         *   2. If INSPECTOR+ required but officer is SI → escalate to DSP
         *   3. If DSP+ required but officer is INSPECTOR → escalate to DSP
         *   4. If SP+ required and officer < SP → escalate to SP
         *   5. If operation severity ≥ 7 → escalate to DSP+ automatically
         *
         * @param operation_type String describing operation (for logging)
         *                       Examples: "SEARCH_WARRANT", "ARREST_WARRANT", "BAIL_MODIFICATION"
         * @param officer_rank The rank of the requesting officer
         * @param context Additional context for audit logging
         * @param out_code Output parameter:
         *                 - OK: approved immediately (officer has sufficient rank)
         *                 - RANK_INSUFFICIENT: needs escalation to higher rank
         *                 - INVALID_STATE: rejected (violates policy)
         * @return true if approved (OK), false otherwise
         *
         * @example
         *   JusticeFlow::ResultCode result;
         *   bool approved = PolicyEngine::getInstance().evaluate(
         *       "SEARCH_WARRANT",
         *       officer.currentRank,
         *       "Search warrant for KIDNAPPING case",
         *       result
         *   );
         *   if (!approved && result == ResultCode::RANK_INSUFFICIENT) {
         *       // Route to DSP for approval
         *   }
         */
        bool evaluate(const std::string &operation_type,
                      JusticeFlow::OfficerRank officer_rank,
                      const std::string &context,
                      JusticeFlow::ResultCode &out_code);
    };

} // namespace security