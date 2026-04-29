#pragma once

/**
 * @file system.h
 * @brief Top-level System Gateway for the JusticeFlow platform.
 *
 * This is the ONLY header the API gateway / routing layer should include.
 * It unifies all three subsystems and the auth layer behind a single,
 * coherent entry point.
 *
 * ============================================================================
 * Design Patterns
 * ============================================================================
 *
 *  1. FACADE
 *       SystemManager presents one clean surface over four independent
 *       subsystems (Auth, S1, S2, S3) that otherwise expose different calling
 *       conventions (static methods, singletons, token-auth vs session-auth,
 *       heap-allocated entity returns vs out-params, etc.).
 *
 *  2. ADAPTER
 *       Each subsystem is wrapped in a pure-virtual adapter interface
 *       (IAuthAdapter, ISubsystem1Adapter, ISubsystem2Adapter,
 *       ISubsystem3Adapter).  The adapters bridge the structural mismatch:
 *         - S1  : all-static, PGconn* + SessionContext on every call.
 *         - S2  : singleton, entity-returning (heap-allocated, caller owns),
 *                 object-oriented models.
 *         - S3  : mixed — static enforcement managers, a singleton AuditManager
 *                 with its own lifecycle, token-authenticated ForensicManager.
 *         - Auth: singleton with isDutyActive() cache and token lifecycle.
 *       Concrete default adapters (DefaultXxxAdapter) live in system.cpp and
 *       delegate straight through to the real subsystem code.
 *
 *  3. DEPENDENCY INJECTION
 *       SystemManager holds each adapter via std::unique_ptr.
 *       Call injectXxx() *before* init() to replace any adapter with a mock,
 *       stub, or alternative implementation (e.g. for unit testing or
 *       gradual migration of a subsystem).
 *
 *  4. MANAGER
 *       SystemManager owns the system lifecycle:
 *         - init()     : initialises all adapters; boots the S3 audit connection.
 *         - shutdown() : tears down resources in reverse-init order.
 *       It also guards every public call with an initialisation check so that
 *       the gateway can never accidentally reach an uninitialised subsystem.
 *
 * ============================================================================
 * Usage Pattern (API Gateway / Worker Thread)
 * ============================================================================
 *
 * @code
 * // ── process startup ──────────────────────────────────────────────────────
 * auto& sys = system_layer::SystemManager::getInstance();
 *
 * // (optional) inject mocks / alternative adapters before init
 * // sys.injectS2(std::make_unique<MyS2Stub>());
 *
 * JusticeFlow::ResultCode rc = sys.init("host=localhost dbname=audit_db");
 * if (rc != JusticeFlow::ResultCode::OK) { /* handle */
}
** // ── per-request ──────────────────────────────────────────────────────────
 *JusticeFlow::SessionContext session;
*rc = sys.validateToken(token, session);
*if (rc != JusticeFlow::ResultCode::OK){/* reject */} *
    *int case_id = 0;
*JusticeFlow::ResultCode op_code;
*bool ok = sys.registerCase(conn, session, ..., case_id, op_code);
** // ── process shutdown ─────────────────────────────────────────────────────
 *sys.shutdown();
*@endcode
        *
            *@author JusticeFlow Platform Team
                * /

#include <memory>
#include <vector>
#include <string>
#include <ctime>
#include <cstdint>

#include <postgresql/libpq-fe.h>

#include "common/constants.h"
#include "common/common.h"

// ── Subsystem public facades (adapter targets) ────────────────────────────
#include "subsystem1/subsystem1.h"
#include "subsystem2/subsystem2.h"
#include "subsystem3/subsystem3.h"
#include "shr_infra/auth/include/auth_module.h"

// ── S2 domain types (needed for UC return types) ──────────────────────────
#include "subsystem2/include/models/Case.h"
#include "subsystem2/include/models/Evidence.h"
#include "subsystem2/include/models/ChargeSheet.h"
#include "subsystem2/include/s2_types.h"

    namespace system_layer
{

    // =============================================================================
    // IAuthAdapter — Adapter interface for the Auth & Session subsystem
    // =============================================================================

    /**
     * @interface IAuthAdapter
     * @brief Abstract adapter bridging SystemManager to the auth::AuthManager singleton.
     *
     * Dependency-inject a concrete implementation (or mock) via
     * SystemManager::injectAuth() before calling init().
     */
    class IAuthAdapter
    {
    public:
        virtual ~IAuthAdapter() = default;

        /** Authenticate an officer by credentials; returns a session token on OK. */
        virtual JusticeFlow::ResultCode login(
            const char *cnic,
            const char *password,
            std::string &out_token) = 0;

        /**
         * Validate a session token on every inbound request.
         * Populates out_session with officer identity and rank if valid.
         */
        virtual JusticeFlow::ResultCode validateToken(
            const char *token,
            JusticeFlow::SessionContext &out_session) = 0;

        /**
         * Check whether the caller's rank meets the minimum required rank.
         * Called by subsystem facades internally, but exposed here for gateway
         * pre-flight checks.
         */
        virtual JusticeFlow::ResultCode validateRank(
            const JusticeFlow::SessionContext &session,
            JusticeFlow::OfficerRank required_rank) = 0;

        /** Returns true if the officer currently has an active duty assignment. */
        virtual bool isDutyActive(int officer_id) = 0;

        /** Extend the idle-timeout window for an active session. */
        virtual JusticeFlow::ResultCode refreshSession(const char *token) = 0;

        /** Destroy the session, invalidating the token immediately. */
        virtual JusticeFlow::ResultCode logout(const char *token) = 0;
    };

    // =============================================================================
    // ISubsystem1Adapter — Adapter interface for S1 (Crime Intelligence & Resources)
    // =============================================================================

    /**
     * @interface ISubsystem1Adapter
     * @brief Abstract adapter over the all-static Subsystem1 facade.
     *
     * Bridges the gap between SystemManager's instance-based dispatch and S1's
     * purely static calling convention.  Every method mirrors the corresponding
     * Subsystem1 static exactly.
     */
    class ISubsystem1Adapter
    {
    public:
        virtual ~ISubsystem1Adapter() = default;

        // ── Case Management — Core CRUD ──────────────────────────────────────────

        virtual bool registerCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            JusticeFlow::CaseType case_type,
            time_t incident_date,
            const char *incident_address,
            const char *description,
            double lat, double lon,
            int station_id,
            const char *complainant_cnic,
            int &out_case_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getCaseById(
            PGconn *conn, int case_id,
            JusticeFlow::Case &out) = 0;

        virtual JusticeFlow::ResultCode getCasesByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::Case> &out) = 0;

        virtual JusticeFlow::ResultCode getCasesByStatus(
            PGconn *conn, int station_id,
            JusticeFlow::CaseStatus status,
            std::vector<JusticeFlow::Case> &out) = 0;

        // ── Case Management — Status Transitions ─────────────────────────────────

        virtual bool updateCaseStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            JusticeFlow::CaseStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool closeCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *closure_reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool reopenCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *reopen_reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool transferCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            int to_station_id,
            const char *transfer_reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getCaseStatusLog(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::CaseStatusLog> &out) = 0;

        // ── Case Management — Officer Assignment ──────────────────────────────────

        virtual bool assignOfficerToCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int officer_id,
            JusticeFlow::CaseOfficerRole role,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool relieveOfficerFromCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int officer_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getAssignedOfficers(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::CaseOfficer> &out) = 0;

        // ── Case Management — Parties ─────────────────────────────────────────────

        virtual bool addComplainant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *person_cnic,
            JusticeFlow::RelationshipToVictim relation,
            bool notify_on_update,
            int &out_complainant_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool updateComplainantStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int complainant_id,
            JusticeFlow::ComplainantStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getComplainantsByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Complainant> &out) = 0;

        virtual bool addVictim(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *person_cnic,
            const char *injury_type,
            JusticeFlow::InjurySeverity injury_severity,
            JusticeFlow::VulnerabilityCategory vulnerability,
            const char *medical_report_ref,
            int &out_victim_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getVictimsByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Victim> &out) = 0;

        virtual bool addWitness(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *person_cnic,
            const char *statement_text,
            const char *statement_file_path,
            JusticeFlow::WitnessProtection protection_status,
            bool conceal_identity,
            int &out_witness_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool updateWitnessProtection(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int witness_id,
            JusticeFlow::WitnessProtection new_status,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getWitnessesByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Witness> &out) = 0;

        virtual bool addAccused(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *person_cnic,
            JusticeFlow::InvolvementType involvement,
            int &out_accused_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool linkAccusedAssociation(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int accused_id,
            int associated_accused_id,
            JusticeFlow::AssociationType association_type,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getAccusedByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Accused> &out) = 0;

        virtual bool linkVehicleToCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int vehicle_id,
            JusticeFlow::VehicleRole role,
            const char *condition_notes,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getVehiclesByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::VehicleCase> &out) = 0;

        // ── Duty & Patrol — Scheduling ────────────────────────────────────────────

        virtual bool scheduleDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id, int station_id, int patrol_route_id,
            JusticeFlow::ShiftType shift_type,
            const char *duty_date,
            time_t scheduled_start, time_t scheduled_end,
            int &out_duty_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool markDutyStart(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool markDutyEnd(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool updateDutyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::DutyStatus new_status,
            const char *absence_reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool cancelDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code) = 0;

        // ── Duty & Patrol — Queries ───────────────────────────────────────────────

        virtual JusticeFlow::ResultCode getDutyRoster(
            PGconn *conn, int station_id,
            const char *duty_date,
            std::vector<JusticeFlow::DutyRoster> &out) = 0;

        virtual JusticeFlow::ResultCode getActiveDuties(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::DutyRoster> &out) = 0;

        virtual JusticeFlow::ResultCode getOfficerDutyHistory(
            PGconn *conn, int officer_id,
            time_t from, time_t to,
            std::vector<JusticeFlow::DutyRoster> &out) = 0;

        // ── Duty & Patrol — Routes ────────────────────────────────────────────────

        virtual bool createPatrolRoute(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int station_id,
            const char *beat_code,
            const char *route_name,
            const char *area_description,
            int &out_route_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool deactivatePatrolRoute(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int route_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getPatrolRoutesByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::PatrolRoute> &out) = 0;

        // ── Personnel — Profiles ──────────────────────────────────────────────────

        virtual JusticeFlow::ResultCode getOfficerById(
            PGconn *conn, int officer_id,
            JusticeFlow::Officer &out) = 0;

        virtual JusticeFlow::ResultCode getOfficerByCnic(
            PGconn *conn, const char *cnic,
            JusticeFlow::Officer &out) = 0;

        virtual JusticeFlow::ResultCode getOfficersByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::Officer> &out) = 0;

        virtual JusticeFlow::ResultCode getOfficersByStatus(
            PGconn *conn, int station_id,
            JusticeFlow::OfficerStatus status,
            std::vector<JusticeFlow::Officer> &out) = 0;

        // ── Personnel — Status & Rank ─────────────────────────────────────────────

        virtual bool updateOfficerStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerStatus new_status,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool promoteOfficer(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerRank new_rank,
            const char *new_belt_number,
            const char *promotion_type,
            const char *effective_date,
            const char *order_date,
            int &out_history_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getOfficerRankHistory(
            PGconn *conn, int officer_id,
            std::vector<JusticeFlow::OfficerRankHistory> &out) = 0;

        // ── Personnel — Deployments ───────────────────────────────────────────────

        virtual bool deployOfficer(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id, int to_station_id,
            const char *deployment_reason,
            const char *order_number,
            const char *deployed_from,
            const char *deployed_until,
            int &out_deployment_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool endDeployment(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int deployment_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getOfficerDeployments(
            PGconn *conn, int officer_id,
            bool active_only,
            std::vector<JusticeFlow::OfficerDeployment> &out) = 0;

        virtual JusticeFlow::ResultCode generateOfficerReport(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            subsystem1::ReportType type,
            std::string &out_report_text) = 0;
    };

    // =============================================================================
    // ISubsystem2Adapter — Adapter interface for S2 (Investigation & Case Processing)
    // =============================================================================

    /**
     * @interface ISubsystem2Adapter
     * @brief Abstract adapter over the Subsystem2 singleton facade.
     *
     * S2 returns heap-allocated entity pointers (caller takes ownership).
     * The adapter preserves this contract so that the gateway layer can manage
     * entity lifetimes through RAII wrappers if desired.
     */
    class ISubsystem2Adapter
    {
    public:
        virtual ~ISubsystem2Adapter() = default;

        /** UC-1: Validate session, check officer rank, insert FIR. */
        virtual JusticeFlow::ResultCode registerFIR(
            const subsystem2::FIRRegistrationRequest &request,
            const JusticeFlow::SessionContext &session,
            subsystem2::Case *&out_case) = 0;

        /** UC-2: mmap evidence file, persist record, fire Observer chain. */
        virtual JusticeFlow::ResultCode logAndSecureEvidence(
            int64_t case_id,
            JusticeFlow::EvidenceType type,
            const std::string &description,
            const std::string &file_path,
            const JusticeFlow::SessionContext &session,
            subsystem2::Evidence *&out_evidence) = 0;

        /** UC-3: Create a DRAFT charge sheet for the given case. */
        virtual JusticeFlow::ResultCode draftChargeSheet(
            int64_t case_id,
            const JusticeFlow::SessionContext &session,
            subsystem2::ChargeSheet *&out_sheet) = 0;

        /** UC-4: Validate, lock, and persist a charge sheet as SUBMITTED_TO_COURT. */
        virtual JusticeFlow::ResultCode submitChargeSheet(
            subsystem2::ChargeSheet *sheet,
            const JusticeFlow::SessionContext &session) = 0;

        /** UC-X: Retrieve a case record by primary key (read-only). */
        virtual JusticeFlow::ResultCode fetchCase(
            int64_t case_id,
            subsystem2::Case *&out_case) = 0;
    };

    // =============================================================================
    // ISubsystem3Adapter — Adapter interface for S3 (Security & Enforcement)
    // =============================================================================

    /**
     * @interface ISubsystem3Adapter
     * @brief Abstract adapter over the Subsystem3 static facade.
     *
     * S3 has three calling conventions internally (AuditManager singleton with
     * its own connection, enforcement static managers sharing the caller's
     * PGconn*, ForensicManager using token-based auth).  The adapter presents
     * a single uniform interface; concrete implementations hide these details.
     *
     * Note: The audit lifecycle (initAudit / shutdownAudit) is managed by
     * SystemManager::init() / shutdown() rather than exposed here.
     */
    class ISubsystem3Adapter
    {
    public:
        virtual ~ISubsystem3Adapter() = default;

        // ── Audit ─────────────────────────────────────────────────────────────────

        virtual JusticeFlow::ResultCode getAuditChangeHistory(
            int case_id,
            std::vector<audit::AuditRecord> &out) = 0;

        virtual JusticeFlow::ResultCode getAuditOfficerActions(
            int officer_id,
            time_t from, time_t to,
            std::vector<audit::AuditRecord> &out) = 0;

        virtual JusticeFlow::ResultCode getAuditTableChanges(
            const char *table_name,
            int record_id,
            std::vector<audit::AuditRecord> &out) = 0;

        virtual JusticeFlow::ResultCode auditQueryByTimeWindow(
            time_t from, time_t to,
            std::vector<audit::AuditRecord> &out) = 0;

        virtual JusticeFlow::ResultCode detectSuspiciousActivity(
            int station_id,
            std::vector<audit::AuditRecord> &out) = 0;

        // ── Enforcement — Warrants ────────────────────────────────────────────────

        virtual bool requestWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *accused_cnic,
            JusticeFlow::WarrantType warrant_type,
            const char *magistrate_name,
            const char *issuing_court,
            const char *valid_until,
            const char *target_address,
            int &out_warrant_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool executeWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int warrant_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool cancelWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int warrant_id,
            const char *cancellation_reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getWarrantsByCase(
            PGconn *conn, int case_id,
            std::vector<enforcement::WarrantRecord> &out) = 0;

        virtual JusticeFlow::ResultCode getActiveWarrants(
            PGconn *conn, int station_id,
            std::vector<enforcement::WarrantRecord> &out) = 0;

        // ── Enforcement — Arrests ─────────────────────────────────────────────────

        virtual bool recordArrest(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *accused_cnic,
            const char *arrest_location,
            int warrant_id,
            int &out_arrest_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool updateCustodyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            JusticeFlow::CustodyStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool markArrestAsDisputed(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            const char *dispute_reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getArrestsByCase(
            PGconn *conn, int case_id,
            std::vector<enforcement::ArrestRecord> &out) = 0;

        // ── Enforcement — Bail ────────────────────────────────────────────────────

        virtual bool recordBail(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            JusticeFlow::BailType bail_type,
            uint64_t bail_amount_paise,
            const char *court_name,
            const char *magistrate_name,
            const char *valid_until,
            const char *surety_name,
            const char *surety_cnic,
            const char *surety_contact,
            int &out_bail_id,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual bool revokeBail(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int bail_id,
            const char *revocation_reason,
            JusticeFlow::ResultCode &out_code) = 0;

        virtual JusticeFlow::ResultCode getBailByArrest(
            PGconn *conn, int arrest_id,
            enforcement::BailRecord &out) = 0;

        // ── Forensic & Lab ────────────────────────────────────────────────────────

        virtual JusticeFlow::ResultCode createForensicRequest(
            const char *token,
            int case_id,
            const char *examination_purpose,
            const char *purpose_description,
            const char *lab_name,
            const char *examiner_name,
            int &out_request_id) = 0;

        virtual JusticeFlow::ResultCode linkEvidence(
            const char *token,
            int request_id,
            int evidence_id,
            const char *notes) = 0;

        virtual JusticeFlow::ResultCode recordLabReceipt(
            const char *token,
            int request_id,
            const char *received_date) = 0;

        virtual JusticeFlow::ResultCode recordExaminationStart(
            const char *token,
            int request_id) = 0;

        virtual JusticeFlow::ResultCode recordFindings(
            const char *token,
            int request_id,
            const char *findings,
            const char *report_file_path,
            const char *delivery_date) = 0;

        virtual JusticeFlow::ResultCode recordAmendment(
            const char *token,
            int request_id,
            const char *amended_findings) = 0;

        virtual JusticeFlow::ResultCode contestReport(
            const char *token,
            int request_id,
            const char *contest_reason) = 0;

        virtual JusticeFlow::ResultCode getForensicRequestsByCase(
            const char *token, int case_id,
            std::vector<forensic::ForensicRecord> &out) = 0;

        virtual JusticeFlow::ResultCode getPendingForensicRequests(
            const char *token, int station_id,
            std::vector<forensic::ForensicRecord> &out) = 0;

        virtual JusticeFlow::ResultCode getEvidenceByForensicRequest(
            const char *token, int request_id,
            std::vector<forensic::EvidenceRef> &out) = 0;
    };

    // =============================================================================
    // SystemManager — Singleton Facade + Dependency Injection Manager
    // =============================================================================

    /**
     * @class SystemManager
     * @brief Top-level gateway to the entire JusticeFlow platform.
     *
     * Roles:
     *   - FACADE   : single include, single object, uniform call surface.
     *   - MANAGER  : owns system lifecycle (init / shutdown) and guards every
     *                method call against use-before-init.
     *   - DI HOST  : holds adapter slots that can be replaced before init(),
     *                enabling testing and phased subsystem migration.
     *
     * Thread Safety:
     *   getInstance() is thread-safe under C++11 §6.7.
     *   Individual method calls are NOT synchronised here; the underlying
     *   subsystem managers and the DB/IPC layers own their concurrency contracts.
     *   The gateway is responsible for per-request connection pool management.
     */
    class SystemManager
    {
    public:
        // =========================================================================
        // Singleton
        // =========================================================================

        /** @brief Returns the single process-wide SystemManager instance. */
        static SystemManager &getInstance();

        // Non-copyable, non-movable
        SystemManager(const SystemManager &) = delete;
        SystemManager &operator=(const SystemManager &) = delete;
        SystemManager(SystemManager &&) = delete;
        SystemManager &operator=(SystemManager &&) = delete;

        // =========================================================================
        // Dependency Injection — call before init() to override defaults
        // =========================================================================

        /**
         * @brief Replace the auth adapter.
         * If not called, the default adapter delegates to auth::AuthManager.
         * @pre Must be called before init().
         */
        void injectAuth(std::unique_ptr<IAuthAdapter> adapter);

        /**
         * @brief Replace the Subsystem 1 adapter.
         * If not called, the default adapter delegates to subsystem1::Subsystem1.
         * @pre Must be called before init().
         */
        void injectS1(std::unique_ptr<ISubsystem1Adapter> adapter);

        /**
         * @brief Replace the Subsystem 2 adapter.
         * If not called, the default adapter delegates to subsystem2::Subsystem2.
         * @pre Must be called before init().
         */
        void injectS2(std::unique_ptr<ISubsystem2Adapter> adapter);

        /**
         * @brief Replace the Subsystem 3 adapter.
         * If not called, the default adapter delegates to subsystem3::Subsystem3.
         * @pre Must be called before init().
         */
        void injectS3(std::unique_ptr<ISubsystem3Adapter> adapter);

        // =========================================================================
        // Lifecycle — Manager responsibility
        // =========================================================================

        /**
         * @brief Initialise the system.
         *
         * Installs default adapters for any slot not already filled via
         * injectXxx(), then calls subsystem-specific startup routines
         * (notably S3 audit connection setup).
         *
         * @param audit_conninfo  libpq connection string for the read-only audit DB.
         * @return OK on full success; first failing ResultCode otherwise.
         */
        JusticeFlow::ResultCode init(const char *audit_conninfo);

        /**
         * @brief Tear down all subsystem resources in reverse-init order.
         * Safe to call even if init() was never called (no-op).
         */
        void shutdown();

        /** @return true once init() has completed successfully. */
        bool isInitialized() const noexcept { return initialized_; }

        // =========================================================================
        // Auth facade
        // =========================================================================

        JusticeFlow::ResultCode login(
            const char *cnic,
            const char *password,
            std::string &out_token);

        JusticeFlow::ResultCode validateToken(
            const char *token,
            JusticeFlow::SessionContext &out_session);

        JusticeFlow::ResultCode validateRank(
            const JusticeFlow::SessionContext &session,
            JusticeFlow::OfficerRank required_rank);

        bool isDutyActive(int officer_id);

        JusticeFlow::ResultCode refreshSession(const char *token);

        JusticeFlow::ResultCode logout(const char *token);

        // =========================================================================
        // Subsystem 1 facade — Case Management
        // =========================================================================

        bool registerCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            JusticeFlow::CaseType case_type,
            time_t incident_date,
            const char *incident_address,
            const char *description,
            double lat, double lon,
            int station_id,
            const char *complainant_cnic,
            int &out_case_id,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getCaseById(
            PGconn *conn, int case_id,
            JusticeFlow::Case &out);

        JusticeFlow::ResultCode getCasesByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::Case> &out);

        JusticeFlow::ResultCode getCasesByStatus(
            PGconn *conn, int station_id,
            JusticeFlow::CaseStatus status,
            std::vector<JusticeFlow::Case> &out);

        bool updateCaseStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            JusticeFlow::CaseStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code);

        bool closeCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *closure_reason,
            JusticeFlow::ResultCode &out_code);

        bool reopenCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *reopen_reason,
            JusticeFlow::ResultCode &out_code);

        bool transferCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            int to_station_id,
            const char *transfer_reason,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getCaseStatusLog(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::CaseStatusLog> &out);

        // ── Officer Assignment ────────────────────────────────────────────────────

        bool assignOfficerToCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int officer_id,
            JusticeFlow::CaseOfficerRole role,
            JusticeFlow::ResultCode &out_code);

        bool relieveOfficerFromCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int officer_id,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getAssignedOfficers(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::CaseOfficer> &out);

        // ── Complainants ──────────────────────────────────────────────────────────

        bool addComplainant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, const char *person_cnic,
            JusticeFlow::RelationshipToVictim relation,
            bool notify_on_update,
            int &out_complainant_id,
            JusticeFlow::ResultCode &out_code);

        bool updateComplainantStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int complainant_id,
            JusticeFlow::ComplainantStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getComplainantsByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Complainant> &out);

        // ── Victims ───────────────────────────────────────────────────────────────

        bool addVictim(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, const char *person_cnic,
            const char *injury_type,
            JusticeFlow::InjurySeverity injury_severity,
            JusticeFlow::VulnerabilityCategory vulnerability,
            const char *medical_report_ref,
            int &out_victim_id,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getVictimsByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Victim> &out);

        // ── Witnesses ─────────────────────────────────────────────────────────────

        bool addWitness(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, const char *person_cnic,
            const char *statement_text,
            const char *statement_file_path,
            JusticeFlow::WitnessProtection protection_status,
            bool conceal_identity,
            int &out_witness_id,
            JusticeFlow::ResultCode &out_code);

        bool updateWitnessProtection(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int witness_id,
            JusticeFlow::WitnessProtection new_status,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getWitnessesByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Witness> &out);

        // ── Accused ───────────────────────────────────────────────────────────────

        bool addAccused(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, const char *person_cnic,
            JusticeFlow::InvolvementType involvement,
            int &out_accused_id,
            JusticeFlow::ResultCode &out_code);

        bool linkAccusedAssociation(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int accused_id, int associated_accused_id,
            JusticeFlow::AssociationType association_type,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getAccusedByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::Accused> &out);

        // ── Vehicles ──────────────────────────────────────────────────────────────

        bool linkVehicleToCase(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id, int vehicle_id,
            JusticeFlow::VehicleRole role,
            const char *condition_notes,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getVehiclesByCase(
            PGconn *conn, int case_id,
            std::vector<JusticeFlow::VehicleCase> &out);

        // =========================================================================
        // Subsystem 1 facade — Duty & Patrol
        // =========================================================================

        bool scheduleDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id, int station_id, int patrol_route_id,
            JusticeFlow::ShiftType shift_type,
            const char *duty_date,
            time_t scheduled_start, time_t scheduled_end,
            int &out_duty_id,
            JusticeFlow::ResultCode &out_code);

        bool markDutyStart(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        bool markDutyEnd(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        bool updateDutyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::DutyStatus new_status,
            const char *absence_reason,
            JusticeFlow::ResultCode &out_code);

        bool cancelDuty(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int duty_id,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getDutyRoster(
            PGconn *conn, int station_id,
            const char *duty_date,
            std::vector<JusticeFlow::DutyRoster> &out);

        JusticeFlow::ResultCode getActiveDuties(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::DutyRoster> &out);

        JusticeFlow::ResultCode getOfficerDutyHistory(
            PGconn *conn, int officer_id,
            time_t from, time_t to,
            std::vector<JusticeFlow::DutyRoster> &out);

        bool createPatrolRoute(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int station_id,
            const char *beat_code,
            const char *route_name,
            const char *area_description,
            int &out_route_id,
            JusticeFlow::ResultCode &out_code);

        bool deactivatePatrolRoute(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int route_id,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getPatrolRoutesByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::PatrolRoute> &out);

        // =========================================================================
        // Subsystem 1 facade — Personnel
        // =========================================================================

        JusticeFlow::ResultCode getOfficerById(
            PGconn *conn, int officer_id,
            JusticeFlow::Officer &out);

        JusticeFlow::ResultCode getOfficerByCnic(
            PGconn *conn, const char *cnic,
            JusticeFlow::Officer &out);

        JusticeFlow::ResultCode getOfficersByStation(
            PGconn *conn, int station_id,
            std::vector<JusticeFlow::Officer> &out);

        JusticeFlow::ResultCode getOfficersByStatus(
            PGconn *conn, int station_id,
            JusticeFlow::OfficerStatus status,
            std::vector<JusticeFlow::Officer> &out);

        bool updateOfficerStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerStatus new_status,
            JusticeFlow::ResultCode &out_code);

        bool promoteOfficer(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            JusticeFlow::OfficerRank new_rank,
            const char *new_belt_number,
            const char *promotion_type,
            const char *effective_date,
            const char *order_date,
            int &out_history_id,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getOfficerRankHistory(
            PGconn *conn, int officer_id,
            std::vector<JusticeFlow::OfficerRankHistory> &out);

        bool deployOfficer(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id, int to_station_id,
            const char *deployment_reason,
            const char *order_number,
            const char *deployed_from,
            const char *deployed_until,
            int &out_deployment_id,
            JusticeFlow::ResultCode &out_code);

        bool endDeployment(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int deployment_id,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getOfficerDeployments(
            PGconn *conn, int officer_id,
            bool active_only,
            std::vector<JusticeFlow::OfficerDeployment> &out);

        JusticeFlow::ResultCode generateOfficerReport(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int officer_id,
            subsystem1::ReportType type,
            std::string &out_report_text);

        // =========================================================================
        // Subsystem 2 facade — Investigation & Case Processing
        // =========================================================================

        /** UC-1 */
        JusticeFlow::ResultCode registerFIR(
            const subsystem2::FIRRegistrationRequest &request,
            const JusticeFlow::SessionContext &session,
            subsystem2::Case *&out_case);

        /** UC-2 */
        JusticeFlow::ResultCode logAndSecureEvidence(
            int64_t case_id,
            JusticeFlow::EvidenceType type,
            const std::string &description,
            const std::string &file_path,
            const JusticeFlow::SessionContext &session,
            subsystem2::Evidence *&out_evidence);

        /** UC-3 */
        JusticeFlow::ResultCode draftChargeSheet(
            int64_t case_id,
            const JusticeFlow::SessionContext &session,
            subsystem2::ChargeSheet *&out_sheet);

        /** UC-4 */
        JusticeFlow::ResultCode submitChargeSheet(
            subsystem2::ChargeSheet *sheet,
            const JusticeFlow::SessionContext &session);

        /** UC-X */
        JusticeFlow::ResultCode fetchCase(
            int64_t case_id,
            subsystem2::Case *&out_case);

        // =========================================================================
        // Subsystem 3 facade — Audit
        // =========================================================================

        JusticeFlow::ResultCode getAuditChangeHistory(
            int case_id,
            std::vector<audit::AuditRecord> &out);

        JusticeFlow::ResultCode getAuditOfficerActions(
            int officer_id,
            time_t from, time_t to,
            std::vector<audit::AuditRecord> &out);

        JusticeFlow::ResultCode getAuditTableChanges(
            const char *table_name,
            int record_id,
            std::vector<audit::AuditRecord> &out);

        JusticeFlow::ResultCode auditQueryByTimeWindow(
            time_t from, time_t to,
            std::vector<audit::AuditRecord> &out);

        JusticeFlow::ResultCode detectSuspiciousActivity(
            int station_id,
            std::vector<audit::AuditRecord> &out);

        // =========================================================================
        // Subsystem 3 facade — Warrants
        // =========================================================================

        bool requestWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *accused_cnic,
            JusticeFlow::WarrantType warrant_type,
            const char *magistrate_name,
            const char *issuing_court,
            const char *valid_until,
            const char *target_address,
            int &out_warrant_id,
            JusticeFlow::ResultCode &out_code);

        bool executeWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int warrant_id,
            JusticeFlow::ResultCode &out_code);

        bool cancelWarrant(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int warrant_id,
            const char *cancellation_reason,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getWarrantsByCase(
            PGconn *conn, int case_id,
            std::vector<enforcement::WarrantRecord> &out);

        JusticeFlow::ResultCode getActiveWarrants(
            PGconn *conn, int station_id,
            std::vector<enforcement::WarrantRecord> &out);

        // =========================================================================
        // Subsystem 3 facade — Arrests
        // =========================================================================

        bool recordArrest(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int case_id,
            const char *accused_cnic,
            const char *arrest_location,
            int warrant_id,
            int &out_arrest_id,
            JusticeFlow::ResultCode &out_code);

        bool updateCustodyStatus(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            JusticeFlow::CustodyStatus new_status,
            const char *reason,
            JusticeFlow::ResultCode &out_code);

        bool markArrestAsDisputed(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            const char *dispute_reason,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getArrestsByCase(
            PGconn *conn, int case_id,
            std::vector<enforcement::ArrestRecord> &out);

        // =========================================================================
        // Subsystem 3 facade — Bail
        // =========================================================================

        bool recordBail(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int arrest_id,
            JusticeFlow::BailType bail_type,
            uint64_t bail_amount_paise,
            const char *court_name,
            const char *magistrate_name,
            const char *valid_until,
            const char *surety_name,
            const char *surety_cnic,
            const char *surety_contact,
            int &out_bail_id,
            JusticeFlow::ResultCode &out_code);

        bool revokeBail(
            PGconn *conn,
            const JusticeFlow::SessionContext &session,
            int bail_id,
            const char *revocation_reason,
            JusticeFlow::ResultCode &out_code);

        JusticeFlow::ResultCode getBailByArrest(
            PGconn *conn, int arrest_id,
            enforcement::BailRecord &out);

        // =========================================================================
        // Subsystem 3 facade — Forensic & Lab
        // =========================================================================

        JusticeFlow::ResultCode createForensicRequest(
            const char *token,
            int case_id,
            const char *examination_purpose,
            const char *purpose_description,
            const char *lab_name,
            const char *examiner_name,
            int &out_request_id);

        JusticeFlow::ResultCode linkEvidence(
            const char *token,
            int request_id,
            int evidence_id,
            const char *notes);

        JusticeFlow::ResultCode recordLabReceipt(
            const char *token,
            int request_id,
            const char *received_date);

        JusticeFlow::ResultCode recordExaminationStart(
            const char *token,
            int request_id);

        JusticeFlow::ResultCode recordFindings(
            const char *token,
            int request_id,
            const char *findings,
            const char *report_file_path,
            const char *delivery_date);

        JusticeFlow::ResultCode recordAmendment(
            const char *token,
            int request_id,
            const char *amended_findings);

        JusticeFlow::ResultCode contestReport(
            const char *token,
            int request_id,
            const char *contest_reason);

        JusticeFlow::ResultCode getForensicRequestsByCase(
            const char *token, int case_id,
            std::vector<forensic::ForensicRecord> &out);

        JusticeFlow::ResultCode getPendingForensicRequests(
            const char *token, int station_id,
            std::vector<forensic::ForensicRecord> &out);

        JusticeFlow::ResultCode getEvidenceByForensicRequest(
            const char *token, int request_id,
            std::vector<forensic::EvidenceRef> &out);

    private:
        SystemManager() = default;
        ~SystemManager() = default;

        // ── Injected adapter slots ────────────────────────────────────────────────
        std::unique_ptr<IAuthAdapter> auth_;
        std::unique_ptr<ISubsystem1Adapter> s1_;
        std::unique_ptr<ISubsystem2Adapter> s2_;
        std::unique_ptr<ISubsystem3Adapter> s3_;

        bool initialized_ = false;

        /**
         * @brief Guard used at the top of every public method.
         * Logs a fatal message and returns NOT_INITIALIZED if called before init().
         */
        JusticeFlow::ResultCode guardInitialized(const char *caller) const;
    };

} // namespace system_layer