#pragma once

/**
 * @file subsystem2.h
 * @brief Public API for Subsystem 2: Investigation & Case Processing.
 *
 * This is the ONLY header the API Gateway (and other subsystems) should include
 * when interacting with case processing workflows.
 *
 * Design Pattern: FACADE
 *   Wraps the three internal controllers (CaseManager, EvidenceManager,
 *   InvestigationManager) behind a single, clean entry point.
 *
 * Use Cases Exposed:
 *   UC-1  Register FIR
 *   UC-2  Log & Secure Evidence  (with OS mmap integration)
 *   UC-3  Draft Charge Sheet
 *   UC-4  Submit Charge Sheet to Magistrate
 *   UC-X  Fetch Case by ID       (utility / read-only)
 */

#include "include/controllers/CaseManager.h"
#include "include/controllers/EvidenceManager.h"
#include "include/controllers/InvestigationManager.h"
#include "include/models/Case.h"
#include "include/models/Evidence.h"
#include "include/models/ChargeSheet.h"
#include "include/s2_types.h"
#include "../../common/constants.h"
#include "../../common/common.h"

namespace subsystem2
{

    /**
     * @class Subsystem2
     * @brief Singleton Facade for the Investigation & Case Processing subsystem.
     *
     * The API Gateway obtains the instance via getInstance() and calls one of the
     * four public use-case methods. Internally the call is routed to the correct
     * manager. No other internal headers need to be exposed to the outside world.
     *
     * Thread Safety:
     *   getInstance() uses function-local static initialisation (C++11 §6.7),
     *   which is guaranteed to be thread-safe by the standard.
     *   Individual method calls are NOT synchronised here; the managers and the
     *   IPC/DB layer are responsible for their own concurrency contracts.
     */
    class Subsystem2
    {
    private:
        // -------------------------------------------------------------------------
        // Internal Manager Instances
        // Each manager owns its own state (e.g. EvidenceManager holds MmapHandler)
        // -------------------------------------------------------------------------
        CaseManager case_mgr_;
        EvidenceManager evidence_mgr_;
        InvestigationManager inv_mgr_;

        /** Private constructor — use getInstance(). */
        Subsystem2() = default;

    public:
        ~Subsystem2() = default;

        // Non-copyable, non-movable (Singleton contract)
        Subsystem2(const Subsystem2 &) = delete;
        Subsystem2 &operator=(const Subsystem2 &) = delete;
        Subsystem2(Subsystem2 &&) = delete;
        Subsystem2 &operator=(Subsystem2 &&) = delete;

        // =========================================================================
        // Singleton accessor
        // =========================================================================
        /**
         * @brief Returns the single global instance of Subsystem2.
         * Safe to call from multiple threads after C++11.
         */
        static Subsystem2 &getInstance();

        // =========================================================================
        // UC-1 : Register FIR
        // =========================================================================
        /**
         * @brief Validates the session, checks officer rank, inserts a new case row
         *        and returns a heap-allocated Case entity.
         *
         * @param request   All data the investigating officer provided on the UI.
         * @param session   Authenticated session context (rank, officerId, etc.).
         * @param out_case  [out] Caller takes ownership of the returned pointer.
         *
         * @return OK              — FIR registered, out_case is valid.
         * @return AUTH_FAILED     — Session is invalid.
         * @return RANK_INSUFFICIENT — Officer rank too low (must be ASI / SI / Inspector).
         * @return DB_ERROR        — Database insert failed.
         */
        JusticeFlow::ResultCode registerFIR(const FIRRegistrationRequest &request,
                                            const JusticeFlow::SessionContext &session,
                                            Case *&out_case);

        // =========================================================================
        // UC-2 : Log & Secure Evidence
        // =========================================================================
        /**
         * @brief Maps the evidence file into RAM (mmap / demand-paging), then
         *        persists the evidence record to the database and fires the
         *        Observer notification chain.
         *
         * @param case_id      The case this evidence belongs to.
         * @param type         DIGITAL or PHYSICAL.
         * @param description  Free-text details provided by the officer.
         * @param file_path    Absolute Linux path to the CCTV clip / document.
         *                     Pass an empty string if there is no associated file.
         * @param session      Authenticated session context.
         * @param out_evidence [out] Caller takes ownership of the returned pointer.
         *
         * @return OK               — Evidence logged and file secured in memory.
         * @return AUTH_FAILED      — Session is invalid.
         * @return FILE_SYSTEM_ERROR — mmap failed (bad path, permissions, etc.).
         * @return DB_ERROR         — Database insert failed.
         */
        JusticeFlow::ResultCode logAndSecureEvidence(int64_t case_id,
                                                     JusticeFlow::EvidenceType type,
                                                     const std::string &description,
                                                     const std::string &file_path,
                                                     const JusticeFlow::SessionContext &session,
                                                     Evidence *&out_evidence);

        // =========================================================================
        // UC-3 : Draft Charge Sheet
        // =========================================================================
        /**
         * @brief Creates a new DRAFT charge sheet record for the given case.
         *        The sheet is unlocked and open for law additions until submitted.
         *
         * @param case_id   The active case being concluded.
         * @param session   Authenticated session context.
         * @param out_sheet [out] Caller takes ownership of the returned pointer.
         *
         * @return OK          — Charge sheet drafted, out_sheet is valid.
         * @return AUTH_FAILED — Session is invalid.
         * @return DB_ERROR    — Database insert failed.
         */
        JusticeFlow::ResultCode draftChargeSheet(int64_t case_id,
                                                 const JusticeFlow::SessionContext &session,
                                                 ChargeSheet *&out_sheet);

        // =========================================================================
        // UC-4 : Submit Charge Sheet to Magistrate
        // =========================================================================
        /**
         * @brief Runs domain validation (laws must be present, sheet must not be
         *        already locked) then persists the SUBMITTED_TO_COURT status and
         *        permanently locks the record in the database.
         *
         * @param sheet   The ChargeSheet entity to submit (must not be nullptr).
         * @param session Authenticated session context.
         *
         * @return OK             — Sheet submitted and locked.
         * @return AUTH_FAILED    — Session is invalid.
         * @return INVALID_INPUT  — sheet pointer is nullptr.
         * @return INVALID_STATE  — No laws have been invoked yet.
         * @return RECORD_LOCKED  — Sheet was already submitted and locked.
         * @return DB_ERROR       — Database update failed.
         */
        JusticeFlow::ResultCode submitChargeSheet(ChargeSheet *sheet,
                                                  const JusticeFlow::SessionContext &session);

        // =========================================================================
        // UC-X : Fetch Case by ID  (read-only utility)
        // =========================================================================
        /**
         * @brief Retrieves an existing case record from the database by primary key.
         *
         * @param case_id  The numeric primary key of the case.
         * @param out_case [out] Caller takes ownership of the returned pointer.
         *
         * @return OK        — Case found, out_case is valid.
         * @return NOT_FOUND — No case with that ID exists in the database.
         * @return DB_ERROR  — Query execution failed.
         */
        JusticeFlow::ResultCode fetchCase(int64_t case_id,
                                          Case *&out_case);
    };

} // namespace subsystem2
