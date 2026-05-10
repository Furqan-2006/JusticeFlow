#pragma once

/**
 * @file system.h
 * @brief Top-level System Gateway for the JusticeFlow platform.
 *
 * This is the ONLY header the API gateway / routing layer should include.
 *
 * ============================================================================
 * Design Patterns
 * ============================================================================
 *
 *  1. FACADE (split — Fix #1: not a God Object)
 *       SystemManager owns eight narrow sub-facades, each responsible for
 *       one domain.  The gateway accesses them via typed references:
 *
 *         sys.auth()          — login, token validation, rank checks
 *         sys.cases()         — S1 case CRUD, parties, status transitions
 *         sys.investigation() — S2 FIR, evidence (mmap), charge sheets
 *         sys.personnel()     — S1 officer profiles, rank, deployments
 *         sys.duty()          — S1 duty scheduling and patrol routes
 *         sys.enforcement()   — S3 warrants, arrests, bail
 *         sys.audit()         — S3 audit trail and compliance queries
 *         sys.forensic()      — S3 forensic lab workflow
 *
 *  2. ADAPTER (Fix #2: canonical SystemResult<T> bridges subsystem differences)
 *       Four pure-virtual adapter interfaces bridge structural mismatches.
 *       Default concrete adapters live in system.cpp.
 *
 *  3. DEPENDENCY INJECTION (Fix #7: injection guarded against post-init misuse)
 *       injectXxx() throws std::logic_error if called after init().
 *
 *  4. MANAGER (Fix #4: staged, explicit init order)
 *       init() runs four named stages in dependency order.
 *       std::atomic<bool> guards every facade call (Fix #5: thread safety).
 *
 * ============================================================================
 * Unified Return Type — SystemResult<T>  (Fix #6: no more split-brain API)
 * ============================================================================
 *
 *  ALL public methods return SystemResult<T>.
 *  No more mixed bool / out-param / ResultCode semantics.
 *
 * ============================================================================
 * Memory Ownership — S2 Entities  (Fix #3: no raw pointer leakage)
 * ============================================================================
 *
 *  S2 adapters return std::unique_ptr<> instead of raw pointers.
 *  SystemResult<std::unique_ptr<T>> transfers ownership unambiguously.
 *
 * ============================================================================
 * Thread-Safety Contract  (Fix #5)
 * ============================================================================
 *
 *  - init() and shutdown() MUST be called from the main thread, before
 *    any worker thread is spawned / after all worker threads have joined.
 *  - Sub-facade method calls ARE safe to issue concurrently from multiple
 *    worker threads, provided the underlying adapters are stateless.
 *    The default concrete adapters satisfy this — they delegate to
 *    stateless static managers or singletons whose internal locking is
 *    the respective subsystem's responsibility.
 *  - Adapter injection (injectXxx) is NOT thread-safe; must complete on
 *    the main thread before init().
 *  - PGconn* handles must NOT be shared across threads. Each worker
 *    supplies its own connection from its own pool slot.
 *
 * @author  JusticeFlow Platform Team
 */

#include <memory>
#include <vector>
#include <string>
#include <ctime>
#include <cstdint>
#include <atomic>
#include <stdexcept>

#include <postgresql/libpq-fe.h>

#include "common/constants.h"
#include "common/common.h"

// ── Subsystem public facades (adapter targets) ────────────────────────────
#include "subsystem1/subsystem1.h"
#include "subsystem2/subsystem2.h"
#include "subsystem3/subsystem3.h"
#include "shr_infra/auth/include/auth_module.h"

// ── S2 domain models (returned via unique_ptr) ────────────────────────────
#include "subsystem2/include/models/Case.h"
#include "subsystem2/include/models/Evidence.h"
#include "subsystem2/include/models/ChargeSheet.h"
#include "subsystem2/include/s2_types.h"

namespace system_layer
{

    // =============================================================================
    // Fix #6 — SystemResult<T>: unified return type
    // =============================================================================

    /**
     * @brief Single return type for all SystemManager / sub-facade methods.
     *
     * Replaces the patchwork of:
     *   - bool return + ResultCode& out-param   (S1, S3 write ops)
     *   - ResultCode return + T& out-param      (S1, S3 read ops)
     *   - ResultCode return + T*& raw-pointer   (S2)
     *
     * Usage:
     * @code
     *   auto r = sys.auth().login("12345-6789012-3", "s3cr3t");
     *   if (!r.ok()) { handle(r.code); return; }
     *   std::string token = std::move(r.value);
     * @endcode
     */
    template <typename T>
    struct SystemResult
    {
        JusticeFlow::ResultCode code = JusticeFlow::ResultCode::OK;
        T value = {};

        bool ok() const noexcept { return code == JusticeFlow::ResultCode::OK; }

        static SystemResult<T> success(T val)
        {
            return {JusticeFlow::ResultCode::OK, std::move(val)};
        }

        static SystemResult<T> failure(JusticeFlow::ResultCode rc)
        {
            return {rc, {}};
        }
    };

    /** Specialisation for operations that produce no value. */
    template <>
    struct SystemResult<void>
    {
        JusticeFlow::ResultCode code = JusticeFlow::ResultCode::OK;

        bool ok() const noexcept { return code == JusticeFlow::ResultCode::OK; }

        static SystemResult<void> success() { return {JusticeFlow::ResultCode::OK}; }
        static SystemResult<void> failure(JusticeFlow::ResultCode rc) { return {rc}; }
    };

    // =============================================================================
    // Fix #4 — SystemInitConfig: explicit dependency graph, not tribal knowledge
    // =============================================================================

    /**
     * @brief All parameters required by SystemManager::init().
     *
     * Naming each parameter in a struct makes staged dependencies visible
     * at the call site and trivial to extend without breaking callers.
     */
    struct SystemInitConfig
    {
        /** libpq connection string for the dedicated read-only audit DB (S3 only). */
        const char *audit_db_conninfo = nullptr;

        // Future slots: per-subsystem pool sizes, timeouts, feature flags.
    };

    // =============================================================================
    // Adapter Interfaces
    // =============================================================================

    // -----------------------------------------------------------------------------
    // IAuthAdapter
    // -----------------------------------------------------------------------------

    /**
     * @interface IAuthAdapter
     * @brief Bridges SystemManager to auth::AuthManager (singleton).
     */
    class IAuthAdapter
    {
    public:
        virtual ~IAuthAdapter() = default;

        virtual SystemResult<std::string> login(const char *cnic,
                                                const char *password) = 0;
        virtual SystemResult<JusticeFlow::SessionContext> validateToken(const char *token) = 0;
        virtual SystemResult<void> validateRank(const JusticeFlow::SessionContext &s,
                                                JusticeFlow::OfficerRank required) = 0;
        /** Returns cached duty status; never throws. */
        virtual bool isDutyActive(int officer_id) = 0;
        virtual SystemResult<void> refreshSession(const char *token) = 0;
        virtual SystemResult<void> logout(const char *token) = 0;
    };

    // -----------------------------------------------------------------------------
    // ISubsystem1Adapter — wraps all-static Subsystem1 (Fix #2: uniform return type)
    // -----------------------------------------------------------------------------

    class ISubsystem1Adapter
    {
    public:
        virtual ~ISubsystem1Adapter() = default;

        // ── Duty Scheduling ───────────────────────────────────────────────────────

        /** @return out_duty_id on success. */
        virtual SystemResult<int> scheduleDuty(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, int station_id, int patrol_route_id,
            JusticeFlow::ShiftType, const char *duty_date,
            time_t scheduled_start, time_t scheduled_end) = 0;

        virtual SystemResult<void> markDutyStart(
            PGconn *conn, const JusticeFlow::SessionContext &session, int duty_id) = 0;

        virtual SystemResult<void> markDutyEnd(
            PGconn *conn, const JusticeFlow::SessionContext &session, int duty_id) = 0;

        virtual SystemResult<void> updateDutyStatus(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int duty_id, JusticeFlow::DutyStatus, const char *absence_reason) = 0;

        virtual SystemResult<void> cancelDuty(
            PGconn *conn, const JusticeFlow::SessionContext &session, int duty_id) = 0;

        virtual SystemResult<std::vector<JusticeFlow::DutyRoster>> getDutyRoster(
            PGconn *conn, int station_id, const char *duty_date) = 0;

        virtual SystemResult<std::vector<JusticeFlow::DutyRoster>> getActiveDuties(
            PGconn *conn, int station_id) = 0;

        virtual SystemResult<std::vector<JusticeFlow::DutyRoster>> getOfficerDutyHistory(
            PGconn *conn, int officer_id, time_t from, time_t to) = 0;

        // ── Patrol Routes ─────────────────────────────────────────────────────────

        /** @return out_route_id on success. */
        virtual SystemResult<int> createPatrolRoute(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int station_id, const char *beat_code,
            const char *route_name, const char *area_description) = 0;

        virtual SystemResult<void> deactivatePatrolRoute(
            PGconn *conn, const JusticeFlow::SessionContext &session, int route_id) = 0;

        virtual SystemResult<std::vector<JusticeFlow::PatrolRoute>> getPatrolRoutesByStation(
            PGconn *conn, int station_id) = 0;

        // ── Personnel ─────────────────────────────────────────────────────────────

        virtual SystemResult<JusticeFlow::Officer> getOfficerById(
            PGconn *conn, int officer_id) = 0;

        virtual SystemResult<JusticeFlow::Officer> getOfficerByCnic(
            PGconn *conn, const char *cnic) = 0;

        virtual SystemResult<std::vector<JusticeFlow::Officer>> getOfficersByStation(
            PGconn *conn, int station_id) = 0;

        virtual SystemResult<std::vector<JusticeFlow::Officer>> getOfficersByStatus(
            PGconn *conn, int station_id, JusticeFlow::OfficerStatus) = 0;

        virtual SystemResult<void> updateOfficerStatus(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, JusticeFlow::OfficerStatus) = 0;

        /** @return out_history_id on success. */
        virtual SystemResult<int> promoteOfficer(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, JusticeFlow::OfficerRank,
            const char *new_belt_number, const char *promotion_type,
            const char *effective_date, const char *order_date) = 0;

        virtual SystemResult<std::vector<JusticeFlow::OfficerRankHistory>> getOfficerRankHistory(
            PGconn *conn, int officer_id) = 0;

        /** @return out_deployment_id on success. */
        virtual SystemResult<int> deployOfficer(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, int to_station_id,
            const char *deployment_reason, const char *order_number,
            const char *deployed_from, const char *deployed_until) = 0;

        virtual SystemResult<void> endDeployment(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int deployment_id) = 0;

        virtual SystemResult<std::vector<JusticeFlow::OfficerDeployment>> getOfficerDeployments(
            PGconn *conn, int officer_id, bool active_only) = 0;

        virtual SystemResult<std::string> generateOfficerReport(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, JusticeFlow::ReportType) = 0;
    };

    // -----------------------------------------------------------------------------
    // ISubsystem2Adapter — Fix #3: raw pointers replaced with unique_ptr
    // -----------------------------------------------------------------------------

    /**
     * @interface ISubsystem2Adapter
     * @brief Bridges SystemManager to the Subsystem2 singleton.
     *
     * All entity-creating operations return std::unique_ptr<T> inside
     * SystemResult, transferring ownership unambiguously to the caller.
     * submitChargeSheet is the single exception: it receives a non-owning
     * raw pointer because the caller already owns the ChargeSheet
     * unique_ptr and merely passes it by address for the duration of the call.
     */
    class ISubsystem2Adapter
    {
    public:
        virtual ~ISubsystem2Adapter() = default;

        /** UC-1  Caller takes ownership of returned Case entity. */
        virtual SystemResult<std::unique_ptr<subsystem2::Case>> registerFIR(
            const subsystem2::FIRRegistrationRequest &request,
            const JusticeFlow::SessionContext &session) = 0;

        /** UC-2  Caller takes ownership of returned Evidence entity. */
        virtual SystemResult<std::unique_ptr<subsystem2::Evidence>> logAndSecureEvidence(
            int64_t case_id,
            JusticeFlow::EvidenceType type,
            const std::string &description,
            const std::string &file_path,
            const JusticeFlow::SessionContext &session) = 0;

        /** UC-3  Caller takes ownership of returned ChargeSheet entity. */
        virtual SystemResult<std::unique_ptr<subsystem2::ChargeSheet>> draftChargeSheet(
            int64_t case_id,
            const JusticeFlow::SessionContext &session) = 0;

        /**
         * UC-4  Non-owning: sheet must remain alive for the duration of the call.
         *       sheet must not be nullptr (validated by InvestigationFacade before
         *       reaching the adapter).
         */
        virtual SystemResult<void> submitChargeSheet(
            subsystem2::ChargeSheet *sheet,
            const JusticeFlow::SessionContext &session) = 0;

        /** UC-X  Caller takes ownership of returned Case entity. */
        virtual SystemResult<std::unique_ptr<subsystem2::Case>> fetchCase(
            int64_t case_id) = 0;
    };

    // -----------------------------------------------------------------------------
    // ISubsystem3Adapter
    // -----------------------------------------------------------------------------

    class ISubsystem3Adapter
    {
    public:
        virtual ~ISubsystem3Adapter() = default;

        // ── Audit ─────────────────────────────────────────────────────────────────

        virtual SystemResult<std::vector<audit::AuditRecord>> getAuditChangeHistory(
            int case_id) = 0;

        virtual SystemResult<std::vector<audit::AuditRecord>> getAuditOfficerActions(
            int officer_id, time_t from, time_t to) = 0;

        virtual SystemResult<std::vector<audit::AuditRecord>> getAuditTableChanges(
            const char *table_name, int record_id) = 0;

        virtual SystemResult<std::vector<audit::AuditRecord>> auditQueryByTimeWindow(
            time_t from, time_t to) = 0;

        virtual SystemResult<std::vector<audit::AuditRecord>> detectSuspiciousActivity(
            int station_id) = 0;

        // ── Warrants ──────────────────────────────────────────────────────────────

        /** @return out_warrant_id on success. */
        virtual SystemResult<int> requestWarrant(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int case_id, const char *accused_cnic,
            JusticeFlow::WarrantType,
            const char *magistrate_name, const char *issuing_court,
            const char *valid_until, const char *target_address) = 0;

        virtual SystemResult<void> executeWarrant(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int warrant_id) = 0;

        virtual SystemResult<void> cancelWarrant(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int warrant_id, const char *cancellation_reason) = 0;

        virtual SystemResult<std::vector<enforcement::WarrantRecord>> getWarrantsByCase(
            PGconn *conn, int case_id) = 0;

        virtual SystemResult<std::vector<enforcement::WarrantRecord>> getActiveWarrants(
            PGconn *conn, int station_id) = 0;

        // ── Arrests ───────────────────────────────────────────────────────────────

        /** @return out_arrest_id on success. */
        virtual SystemResult<int> recordArrest(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int case_id, const char *accused_cnic,
            const char *arrest_location, int warrant_id) = 0;

        virtual SystemResult<void> updateCustodyStatus(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int arrest_id, JusticeFlow::CustodyStatus, const char *reason) = 0;

        virtual SystemResult<void> markArrestAsDisputed(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int arrest_id, const char *dispute_reason) = 0;

        virtual SystemResult<std::vector<enforcement::ArrestRecord>> getArrestsByCase(
            PGconn *conn, int case_id) = 0;

        // ── Bail ──────────────────────────────────────────────────────────────────

        /** @return out_bail_id on success. */
        virtual SystemResult<int> recordBail(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int arrest_id, JusticeFlow::BailType,
            uint64_t bail_amount_paise,
            const char *court_name, const char *magistrate_name,
            const char *valid_until,
            const char *surety_name, const char *surety_cnic,
            const char *surety_contact) = 0;

        virtual SystemResult<void> revokeBail(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int bail_id, const char *revocation_reason) = 0;

        virtual SystemResult<enforcement::BailRecord> getBailByArrest(
            PGconn *conn, int arrest_id) = 0;

        // ── Forensic & Lab ────────────────────────────────────────────────────────

        /** @return out_request_id on success. */
        virtual SystemResult<int> createForensicRequest(
            const char *token, int case_id,
            const char *examination_purpose, const char *purpose_description,
            const char *lab_name, const char *examiner_name) = 0;

        virtual SystemResult<void> linkEvidence(
            const char *token, int request_id,
            int evidence_id, const char *notes) = 0;

        virtual SystemResult<void> recordLabReceipt(
            const char *token, int request_id,
            const char *received_date) = 0;

        virtual SystemResult<void> recordExaminationStart(
            const char *token, int request_id) = 0;

        virtual SystemResult<void> recordFindings(
            const char *token, int request_id,
            const char *findings, const char *report_file_path,
            const char *delivery_date) = 0;

        virtual SystemResult<void> recordAmendment(
            const char *token, int request_id,
            const char *amended_findings) = 0;

        virtual SystemResult<void> contestReport(
            const char *token, int request_id,
            const char *contest_reason) = 0;

        virtual SystemResult<std::vector<forensic::ForensicRecord>> getForensicRequestsByCase(
            const char *token, int case_id) = 0;

        virtual SystemResult<std::vector<forensic::ForensicRecord>> getPendingForensicRequests(
            const char *token, int station_id) = 0;

        virtual SystemResult<std::vector<forensic::EvidenceRef>> getEvidenceByForensicRequest(
            const char *token, int request_id) = 0;
    };

    // =============================================================================
    // Fix #1 — Sub-Facades: narrow, domain-scoped classes instead of one mega-class
    // =============================================================================
    // Each sub-facade holds a NON-OWNING pointer to its adapter.
    // SystemManager owns the adapters via unique_ptr and wires the facades
    // during init().  Facades are invalid (and must not be called) before init().
    // =============================================================================

    // -----------------------------------------------------------------------------
    // AuthFacade
    // -----------------------------------------------------------------------------

    class AuthFacade
    {
    public:
        explicit AuthFacade(IAuthAdapter *adapter = nullptr) noexcept
            : adapter_(adapter) {}

        SystemResult<std::string> login(const char *cnic, const char *password);
        SystemResult<JusticeFlow::SessionContext> validateToken(const char *token);
        SystemResult<void> validateRank(const JusticeFlow::SessionContext &s,
                                        JusticeFlow::OfficerRank required);
        bool isDutyActive(int officer_id);
        SystemResult<void> refreshSession(const char *token);
        SystemResult<void> logout(const char *token);

        void setAdapter(IAuthAdapter *a) noexcept { adapter_ = a; }

    private:
        IAuthAdapter *adapter_;
    };

    // -----------------------------------------------------------------------------
    // CaseFacade — S1 case CRUD, status transitions, all party types, vehicles
    // -----------------------------------------------------------------------------

    class CaseFacade
    {
    public:
        explicit CaseFacade(ISubsystem2Adapter *s2 = nullptr) noexcept : s2_(s2) {}

        void setAdapter(ISubsystem2Adapter *a) noexcept { s2_ = a; }

        // --- Typical Case/Party CRUD and status transitions --
        SystemResult<int> registerCase(PGconn *, const JusticeFlow::SessionContext &,
                                       JusticeFlow::CaseType, time_t,
                                       const char *address, const char *desc,
                                       double lat, double lon,
                                       int station_id, const char *cnic);

        SystemResult<JusticeFlow::Case> getCaseById(PGconn *, int case_id);
        SystemResult<std::vector<JusticeFlow::Case>> getCasesByStation(PGconn *, int station_id);
        SystemResult<std::vector<JusticeFlow::Case>> getCasesByStatus(PGconn *, int station_id,
                                                                      JusticeFlow::CaseStatus);

        SystemResult<void> updateCaseStatus(PGconn *, const JusticeFlow::SessionContext &,
                                            int case_id, JusticeFlow::CaseStatus, const char *reason);
        SystemResult<void> closeCase(PGconn *, const JusticeFlow::SessionContext &,
                                     int case_id, const char *reason);
        SystemResult<void> reopenCase(PGconn *, const JusticeFlow::SessionContext &,
                                      int case_id, const char *reason);
        SystemResult<void> transferCase(PGconn *, const JusticeFlow::SessionContext &,
                                        int case_id, int to_station_id, const char *reason);
        SystemResult<std::vector<JusticeFlow::CaseStatusLog>> getCaseStatusLog(PGconn *, int case_id);

        SystemResult<void> assignOfficerToCase(PGconn *, const JusticeFlow::SessionContext &,
                                               int case_id, int officer_id, JusticeFlow::CaseOfficerRole);
        SystemResult<void> relieveOfficerFromCase(PGconn *, const JusticeFlow::SessionContext &,
                                                  int case_id, int officer_id);
        SystemResult<std::vector<JusticeFlow::CaseOfficer>> getAssignedOfficers(PGconn *, int case_id);

        SystemResult<int> addComplainant(PGconn *, const JusticeFlow::SessionContext &,
                                         int case_id, const char *cnic,
                                         JusticeFlow::RelationshipToVictim, bool notify);
        SystemResult<void> updateComplainantStatus(PGconn *, const JusticeFlow::SessionContext &,
                                                   int id, JusticeFlow::ComplainantStatus, const char *reason);
        SystemResult<std::vector<JusticeFlow::Complainant>> getComplainantsByCase(PGconn *, int case_id);

        SystemResult<int> addVictim(PGconn *, const JusticeFlow::SessionContext &,
                                    int case_id, const char *cnic,
                                    const char *injury_type, JusticeFlow::InjurySeverity,
                                    JusticeFlow::VulnerabilityCategory, const char *medical_ref);
        SystemResult<std::vector<JusticeFlow::Victim>> getVictimsByCase(PGconn *, int case_id);

        SystemResult<int> addWitness(PGconn *, const JusticeFlow::SessionContext &,
                                     int case_id, const char *cnic,
                                     const char *statement, const char *file_path,
                                     JusticeFlow::WitnessProtection, bool conceal);
        SystemResult<void> updateWitnessProtection(PGconn *, const JusticeFlow::SessionContext &,
                                                   int witness_id, JusticeFlow::WitnessProtection);
        SystemResult<std::vector<JusticeFlow::Witness>> getWitnessesByCase(PGconn *, int case_id);

        SystemResult<int> addAccused(PGconn *, const JusticeFlow::SessionContext &,
                                     int case_id, const char *cnic, JusticeFlow::InvolvementType);
        SystemResult<void> linkAccusedAssociation(PGconn *, const JusticeFlow::SessionContext &,
                                                  int accused_id, int associated_id,
                                                  JusticeFlow::AssociationType);
        SystemResult<std::vector<JusticeFlow::Accused>> getAccusedByCase(PGconn *, int case_id);

        SystemResult<void> linkVehicleToCase(PGconn *, const JusticeFlow::SessionContext &,
                                             int case_id, int vehicle_id,
                                             JusticeFlow::VehicleRole, const char *notes);
        SystemResult<std::vector<JusticeFlow::VehicleCase>> getVehiclesByCase(PGconn *, int case_id);

    private:
        ISubsystem2Adapter *s2_;
    };

    // -----------------------------------------------------------------------------
    // InvestigationFacade — S2: FIR, mmap-secured evidence, charge sheets
    // -----------------------------------------------------------------------------

    /**
     * All entity-creating calls return std::unique_ptr<T> inside SystemResult.
     * The caller owns the returned entity.  submitChargeSheet receives the
     * entity by raw non-owning pointer (caller keeps ownership of the unique_ptr).
     */
    class InvestigationFacade
    {
    public:
        explicit InvestigationFacade(ISubsystem2Adapter *s2 = nullptr) noexcept : s2_(s2) {}

        void setAdapter(ISubsystem2Adapter *a) noexcept { s2_ = a; }

        /** UC-1 */
        SystemResult<std::unique_ptr<subsystem2::Case>> registerFIR(
            const subsystem2::FIRRegistrationRequest &request,
            const JusticeFlow::SessionContext &session);

        /** UC-2 */
        SystemResult<std::unique_ptr<subsystem2::Evidence>> logAndSecureEvidence(
            int64_t case_id, JusticeFlow::EvidenceType,
            const std::string &description, const std::string &file_path,
            const JusticeFlow::SessionContext &session);

        /** UC-3 */
        SystemResult<std::unique_ptr<subsystem2::ChargeSheet>> draftChargeSheet(
            int64_t case_id, const JusticeFlow::SessionContext &session);

        /** UC-4  sheet must not be nullptr. */
        SystemResult<void> submitChargeSheet(
            subsystem2::ChargeSheet *sheet,
            const JusticeFlow::SessionContext &session);

        /** UC-X */
        SystemResult<std::unique_ptr<subsystem2::Case>> fetchCase(int64_t case_id);

    private:
        ISubsystem2Adapter *s2_;
    };

    // -----------------------------------------------------------------------------
    // PersonnelFacade — S1: officer profiles, rank history, deployments, reports
    // -----------------------------------------------------------------------------

    class PersonnelFacade
    {
    public:
        explicit PersonnelFacade(ISubsystem1Adapter *s1 = nullptr) noexcept : s1_(s1) {}

        void setAdapter(ISubsystem1Adapter *a) noexcept { s1_ = a; }

        SystemResult<JusticeFlow::Officer> getOfficerById(PGconn *, int);
        SystemResult<JusticeFlow::Officer> getOfficerByCnic(PGconn *, const char *);
        SystemResult<std::vector<JusticeFlow::Officer>> getOfficersByStation(PGconn *, int);
        SystemResult<std::vector<JusticeFlow::Officer>> getOfficersByStatus(PGconn *, int,
                                                                            JusticeFlow::OfficerStatus);
        SystemResult<void> updateOfficerStatus(PGconn *,
                                               const JusticeFlow::SessionContext &,
                                               int officer_id,
                                               JusticeFlow::OfficerStatus);
        SystemResult<int> promoteOfficer(PGconn *,
                                         const JusticeFlow::SessionContext &,
                                         int officer_id,
                                         JusticeFlow::OfficerRank,
                                         const char *belt,
                                         const char *type,
                                         const char *effective,
                                         const char *order_date);
        SystemResult<std::vector<JusticeFlow::OfficerRankHistory>> getOfficerRankHistory(PGconn *, int);
        SystemResult<int> deployOfficer(PGconn *,
                                        const JusticeFlow::SessionContext &,
                                        int officer_id, int to_station,
                                        const char *reason,
                                        const char *order_no,
                                        const char *from_date,
                                        const char *until_date);
        SystemResult<void> endDeployment(PGconn *,
                                         const JusticeFlow::SessionContext &,
                                         int deployment_id);
        SystemResult<std::vector<JusticeFlow::OfficerDeployment>> getOfficerDeployments(PGconn *, int,
                                                                                        bool active_only);
        SystemResult<std::string> generateOfficerReport(PGconn *,
                                                        const JusticeFlow::SessionContext &,
                                                        int officer_id,
                                                        JusticeFlow::ReportType);

    private:
        ISubsystem1Adapter *s1_;
    };

    // -----------------------------------------------------------------------------
    // DutyFacade — S1: duty scheduling, shift lifecycle, patrol routes
    // -----------------------------------------------------------------------------

    class DutyFacade
    {
    public:
        explicit DutyFacade(ISubsystem1Adapter *s1 = nullptr) noexcept : s1_(s1) {}

        void setAdapter(ISubsystem1Adapter *a) noexcept { s1_ = a; }

        SystemResult<int> scheduleDuty(PGconn *,
                                       const JusticeFlow::SessionContext &,
                                       int officer_id, int station_id,
                                       int patrol_route_id,
                                       JusticeFlow::ShiftType,
                                       const char *duty_date,
                                       time_t start, time_t end);
        SystemResult<void> markDutyStart(PGconn *,
                                         const JusticeFlow::SessionContext &,
                                         int duty_id);
        SystemResult<void> markDutyEnd(PGconn *,
                                       const JusticeFlow::SessionContext &,
                                       int duty_id);
        SystemResult<void> updateDutyStatus(PGconn *,
                                            const JusticeFlow::SessionContext &,
                                            int duty_id,
                                            JusticeFlow::DutyStatus,
                                            const char *absence_reason);
        SystemResult<void> cancelDuty(PGconn *,
                                      const JusticeFlow::SessionContext &,
                                      int duty_id);
        SystemResult<std::vector<JusticeFlow::DutyRoster>> getDutyRoster(PGconn *, int station_id,
                                                                         const char *duty_date);
        SystemResult<std::vector<JusticeFlow::DutyRoster>> getActiveDuties(PGconn *, int station_id);
        SystemResult<std::vector<JusticeFlow::DutyRoster>> getOfficerDutyHistory(PGconn *, int officer_id,
                                                                                 time_t from, time_t to);
        SystemResult<int> createPatrolRoute(PGconn *,
                                            const JusticeFlow::SessionContext &,
                                            int station_id,
                                            const char *beat_code,
                                            const char *name,
                                            const char *area);
        SystemResult<void> deactivatePatrolRoute(PGconn *,
                                                 const JusticeFlow::SessionContext &,
                                                 int route_id);
        SystemResult<std::vector<JusticeFlow::PatrolRoute>> getPatrolRoutesByStation(PGconn *, int station_id);

    private:
        ISubsystem1Adapter *s1_;
    };

    // -----------------------------------------------------------------------------
    // EnforcementFacade — S3: warrants, arrests, bail
    // -----------------------------------------------------------------------------

    class EnforcementFacade
    {
    public:
        explicit EnforcementFacade(ISubsystem3Adapter *s3 = nullptr) noexcept : s3_(s3) {}

        void setAdapter(ISubsystem3Adapter *a) noexcept { s3_ = a; }

        // Warrants
        SystemResult<int> requestWarrant(PGconn *,
                                         const JusticeFlow::SessionContext &,
                                         int case_id,
                                         const char *accused_cnic,
                                         JusticeFlow::WarrantType,
                                         const char *magistrate,
                                         const char *court,
                                         const char *valid_until,
                                         const char *target_address);
        SystemResult<void> executeWarrant(PGconn *,
                                          const JusticeFlow::SessionContext &,
                                          int warrant_id);
        SystemResult<void> cancelWarrant(PGconn *,
                                         const JusticeFlow::SessionContext &,
                                         int warrant_id, const char *reason);
        SystemResult<std::vector<enforcement::WarrantRecord>> getWarrantsByCase(PGconn *, int case_id);
        SystemResult<std::vector<enforcement::WarrantRecord>> getActiveWarrants(PGconn *, int station_id);

        // Arrests
        SystemResult<int> recordArrest(PGconn *,
                                       const JusticeFlow::SessionContext &,
                                       int case_id,
                                       const char *accused_cnic,
                                       const char *location,
                                       int warrant_id);
        SystemResult<void> updateCustodyStatus(PGconn *,
                                               const JusticeFlow::SessionContext &,
                                               int arrest_id,
                                               JusticeFlow::CustodyStatus,
                                               const char *reason);
        SystemResult<void> markArrestAsDisputed(PGconn *,
                                                const JusticeFlow::SessionContext &,
                                                int arrest_id,
                                                const char *reason);
        SystemResult<std::vector<enforcement::ArrestRecord>> getArrestsByCase(PGconn *, int case_id);

        // Bail
        SystemResult<int> recordBail(PGconn *,
                                     const JusticeFlow::SessionContext &,
                                     int arrest_id,
                                     JusticeFlow::BailType,
                                     uint64_t amount_paise,
                                     const char *court,
                                     const char *magistrate,
                                     const char *valid_until,
                                     const char *surety_name,
                                     const char *surety_cnic,
                                     const char *surety_contact);
        SystemResult<void> revokeBail(PGconn *,
                                      const JusticeFlow::SessionContext &,
                                      int bail_id, const char *reason);
        SystemResult<enforcement::BailRecord> getBailByArrest(PGconn *, int arrest_id);

    private:
        ISubsystem3Adapter *s3_;
    };

    // -----------------------------------------------------------------------------
    // AuditFacade — S3 read-only audit trail (uses its own DB connection via S3)
    // -----------------------------------------------------------------------------

    class AuditFacade
    {
    public:
        explicit AuditFacade(ISubsystem3Adapter *s3 = nullptr) noexcept : s3_(s3) {}

        void setAdapter(ISubsystem3Adapter *a) noexcept { s3_ = a; }

        SystemResult<std::vector<audit::AuditRecord>> getAuditChangeHistory(int case_id);
        SystemResult<std::vector<audit::AuditRecord>> getAuditOfficerActions(int officer_id,
                                                                             time_t from, time_t to);
        SystemResult<std::vector<audit::AuditRecord>> getAuditTableChanges(const char *table_name,
                                                                           int record_id);
        SystemResult<std::vector<audit::AuditRecord>> auditQueryByTimeWindow(time_t from, time_t to);
        SystemResult<std::vector<audit::AuditRecord>> detectSuspiciousActivity(int station_id);

    private:
        ISubsystem3Adapter *s3_;
    };

    // -----------------------------------------------------------------------------
    // ForensicFacade — S3 forensic lab workflow (token-authenticated)
    // -----------------------------------------------------------------------------

    class ForensicFacade
    {
    public:
        explicit ForensicFacade(ISubsystem3Adapter *s3 = nullptr) noexcept : s3_(s3) {}

        void setAdapter(ISubsystem3Adapter *a) noexcept { s3_ = a; }

        SystemResult<int> createForensicRequest(const char *token,
                                                int case_id,
                                                const char *purpose,
                                                const char *purpose_desc,
                                                const char *lab_name,
                                                const char *examiner_name);
        SystemResult<void> linkEvidence(const char *token, int request_id,
                                        int evidence_id, const char *notes);
        SystemResult<void> recordLabReceipt(const char *token, int request_id,
                                            const char *received_date);
        SystemResult<void> recordExaminationStart(const char *token,
                                                  int request_id);
        SystemResult<void> recordFindings(const char *token, int request_id,
                                          const char *findings,
                                          const char *report_file_path,
                                          const char *delivery_date);
        SystemResult<void> recordAmendment(const char *token, int request_id,
                                           const char *amended_findings);
        SystemResult<void> contestReport(const char *token, int request_id,
                                         const char *reason);
        SystemResult<std::vector<forensic::ForensicRecord>> getForensicRequestsByCase(const char *token,
                                                                                      int case_id);
        SystemResult<std::vector<forensic::ForensicRecord>> getPendingForensicRequests(const char *token,
                                                                                       int station_id);
        SystemResult<std::vector<forensic::EvidenceRef>> getEvidenceByForensicRequest(const char *token,
                                                                                      int request_id);

    private:
        ISubsystem3Adapter *s3_;
    };

    // =============================================================================
    // SystemManager — Singleton: lifecycle + sub-facade access (Fix #1 #4 #5 #7)
    // =============================================================================

    /**
     * @class SystemManager
     * @brief Process-wide gateway to the JusticeFlow platform.
     *
     * Obtain sub-facades via typed accessors (auth(), cases(), etc.) rather
     * than calling methods directly — this keeps the manager itself narrow
     * and eliminates the God Object anti-pattern.
     */
    class SystemManager
    {
    public:
        // =========================================================================
        // Singleton
        // =========================================================================

        static SystemManager &getInstance();

        SystemManager(const SystemManager &) = delete;
        SystemManager &operator=(const SystemManager &) = delete;
        SystemManager(SystemManager &&) = delete;
        SystemManager &operator=(SystemManager &&) = delete;

        // =========================================================================
        // Dependency Injection — Fix #7: throws on post-init injection
        // =========================================================================

        /**
         * @throws std::logic_error if init() has already been called.
         * Call from the main thread before init().
         */
        void injectAuth(std::unique_ptr<IAuthAdapter> adapter);
        void injectS1(std::unique_ptr<ISubsystem1Adapter> adapter);
        void injectS2(std::unique_ptr<ISubsystem2Adapter> adapter);
        void injectS3(std::unique_ptr<ISubsystem3Adapter> adapter);

        // =========================================================================
        // Lifecycle — Fix #4: staged, explicit init order
        // =========================================================================

        /**
         * @brief Initialise the entire system in dependency order:
         *
         *   Stage 1 — Install default adapters for any slot not pre-injected.
         *   Stage 2 — Init Auth (no persistent external connection needed).
         *   Stage 3 — Init S1 & S2 (stateless, no startup I/O).
         *   Stage 4 — Init S3 audit: open dedicated read-only audit DB connection.
         *   Stage 5 — Wire all sub-facades to their adapter pointers.
         *
         * On any stage failure the system remains uninitialised.
         * shutdown() is safe to call to release any partially acquired resources.
         */
        SystemResult<void> init(const SystemInitConfig &config);

        /**
         * @brief Release all resources in reverse-init order.
         * Safe to call even if init() never completed (no-op for uninitialised stages).
         */
        void shutdown();

        /** Thread-safe status check. */
        bool isInitialized() const noexcept
        {
            return initialized_.load(std::memory_order_acquire);
        }

        // =========================================================================
        // Sub-facade accessors
        // =========================================================================

        AuthFacade &auth() noexcept { return auth_facade_; }
        CaseFacade &cases() noexcept { return case_facade_; }
        InvestigationFacade &investigation() noexcept { return inv_facade_; }
        PersonnelFacade &personnel() noexcept { return personnel_facade_; }
        DutyFacade &duty() noexcept { return duty_facade_; }
        EnforcementFacade &enforcement() noexcept { return enforcement_facade_; }
        AuditFacade &audit() noexcept { return audit_facade_; }
        ForensicFacade &forensic() noexcept { return forensic_facade_; }

    private:
        SystemManager() = default;
        ~SystemManager() = default;

        // ── Fix #7: guard injection against post-init calls ───────────────────────
        void assertNotInitialized(const char *caller) const;

        // ── Fix #5: atomic flag for thread-safe init check ────────────────────────
        std::atomic<bool> initialized_{false};

        // ── Adapter ownership (unique_ptr = clear lifetime, no leaks) ─────────────
        std::unique_ptr<IAuthAdapter> auth_adapter_;
        std::unique_ptr<ISubsystem1Adapter> s1_adapter_;
        std::unique_ptr<ISubsystem2Adapter> s2_adapter_;
        std::unique_ptr<ISubsystem3Adapter> s3_adapter_;

        // ── Sub-facades (wired during init(); adapters must outlive them) ──────────
        AuthFacade auth_facade_;
        CaseFacade case_facade_;
        InvestigationFacade inv_facade_;
        PersonnelFacade personnel_facade_;
        DutyFacade duty_facade_;
        EnforcementFacade enforcement_facade_;
        AuditFacade audit_facade_;
        ForensicFacade forensic_facade_;
    };

} // namespace system_layer