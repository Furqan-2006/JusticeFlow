#ifndef I_CASE_STATUS_STRATEGY_H
#define I_CASE_STATUS_STRATEGY_H

#include "../../../common/constants.h"
#include "../../../common/common.h" // For SessionContext

namespace subsystem2 {

// Forward declaration
class Case;

/**
 * @brief Interface for the Strategy Pattern.
 * Encapsulates the specific business rules for transitioning case statuses.
 */
class ICaseStatusStrategy {
public:
    virtual ~ICaseStatusStrategy() = default;

    // Evaluates if the transition is legally allowed, then updates the status.
    // Takes the current session context to verify the officer's rank/identity.
    virtual JusticeFlow::ResultCode processTransition(Case* c, const JusticeFlow::SessionContext& session) = 0;
};

} // namespace subsystem2

#endif // I_CASE_STATUS_STRATEGY_H
