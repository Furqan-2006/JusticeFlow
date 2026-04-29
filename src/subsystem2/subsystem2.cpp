/**
 * @file subsystem2.cpp
 * @brief Implementation of the Subsystem 2 public Facade.
 *
 * Every public method here does exactly three things:
 *   1. Emit a structured log entry so the API Gateway can trace routing.
 *   2. Delegate to the appropriate internal manager.
 *   3. Return the result code unchanged.
 *
 * No business logic lives here — that belongs in the managers and entities.
 */

#include "subsystem2.h"
#include "../../common/logger.h"

namespace subsystem2
{

    // =============================================================================
    // Singleton
    // =============================================================================

    Subsystem2 &Subsystem2::getInstance()
    {
        // C++11 guarantees this is initialised exactly once, even under concurrency.
        static Subsystem2 instance;
        return instance;
    }

    // =============================================================================
    // UC-1 : Register FIR
    // =============================================================================

    JusticeFlow::ResultCode Subsystem2::registerFIR(
        const FIRRegistrationRequest &request,
        const JusticeFlow::SessionContext &session,
        Case *&out_case)
    {
        Logger::info("[S2][Facade] Routing UC-1: registerFIR -> CaseManager");

        JusticeFlow::ResultCode rc = case_mgr_.registerFIR(request, session, out_case);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[S2][Facade] UC-1 registerFIR returned non-OK result.");
        }
        return rc;
    }

    // =============================================================================
    // UC-2 : Log & Secure Evidence
    // =============================================================================

    JusticeFlow::ResultCode Subsystem2::logAndSecureEvidence(
        int64_t case_id,
        JusticeFlow::EvidenceType type,
        const std::string &description,
        const std::string &file_path,
        const JusticeFlow::SessionContext &session,
        Evidence *&out_evidence)
    {
        Logger::info("[S2][Facade] Routing UC-2: logAndSecureEvidence -> EvidenceManager");

        JusticeFlow::ResultCode rc = evidence_mgr_.logAndSecureEvidence(
            case_id, type, description, file_path, session, out_evidence);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[S2][Facade] UC-2 logAndSecureEvidence returned non-OK result.");
        }
        return rc;
    }

    // =============================================================================
    // UC-3 : Draft Charge Sheet
    // =============================================================================

    JusticeFlow::ResultCode Subsystem2::draftChargeSheet(
        int64_t case_id,
        const JusticeFlow::SessionContext &session,
        ChargeSheet *&out_sheet)
    {
        Logger::info("[S2][Facade] Routing UC-3: draftChargeSheet -> InvestigationManager");

        JusticeFlow::ResultCode rc = inv_mgr_.draftChargeSheet(case_id, session, out_sheet);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[S2][Facade] UC-3 draftChargeSheet returned non-OK result.");
        }
        return rc;
    }

    // =============================================================================
    // UC-4 : Submit Charge Sheet to Magistrate
    // =============================================================================

    JusticeFlow::ResultCode Subsystem2::submitChargeSheet(
        ChargeSheet *sheet,
        const JusticeFlow::SessionContext &session)
    {
        Logger::info("[S2][Facade] Routing UC-4: submitChargeSheet -> InvestigationManager");

        // Null guard at the facade boundary — managers assume non-null input.
        if (sheet == nullptr)
        {
            Logger::error("[S2][Facade] UC-4 called with null ChargeSheet pointer. Aborting.");
            return JusticeFlow::ResultCode::INVALID_INPUT;
        }

        JusticeFlow::ResultCode rc = inv_mgr_.submitChargeSheet(sheet, session);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[S2][Facade] UC-4 submitChargeSheet returned non-OK result.");
        }
        return rc;
    }

    // =============================================================================
    // UC-X : Fetch Case by ID
    // =============================================================================

    JusticeFlow::ResultCode Subsystem2::fetchCase(int64_t case_id, Case *&out_case)
    {
        Logger::info("[S2][Facade] Routing UC-X: fetchCase -> CaseManager");

        JusticeFlow::ResultCode rc = case_mgr_.fetchCase(case_id, out_case);

        if (rc != JusticeFlow::ResultCode::OK)
        {
            Logger::error("[S2][Facade] UC-X fetchCase returned non-OK result.");
        }
        return rc;
    }

} // namespace subsystem2