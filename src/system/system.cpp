/**
 * @file system.cpp
 * @brief SystemManager implementation: concrete adapters + sub-facade routing.
 *
 * ============================================================================
 * File layout
 * ============================================================================
 *
 *  Section 1  Concrete Adapter Helpers
 *               adapt_bool()       — converts S1's bool+out_code into SystemResult
 *               adapt_bool_void()  — converts S1's bool+out_code (void) into SystemResult
 *               adapt_rc()         — converts S1's ResultCode+out-param into SystemResult
 *               adapt_rc_void()    — converts S1's bare ResultCode into SystemResult
 *
 *  Section 2  Default concrete adapter implementations
 *               DefaultAuthAdapter
 *               DefaultSubsystem1Adapter
 *               DefaultSubsystem2Adapter
 *               DefaultSubsystem3Adapter
 *
 *  Section 3  SystemManager — Singleton + Lifecycle
 *
 *  Section 4  AuthFacade method bodies
 *
 *  Section 5  CaseFacade method bodies
 *
 *  Section 6  InvestigationFacade method bodies
 *
 *  Section 7  PersonnelFacade method bodies
 *
 *  Section 8  DutyFacade method bodies
 *
 *  Section 9  EnforcementFacade method bodies
 *
 *  Section 10 AuditFacade method bodies
 *
 *  Section 11 ForensicFacade method bodies
 *
 * ============================================================================
 * Adapter translation pattern
 * ============================================================================
 *
 *  S1 and S3 expose:  bool fn(..., T &out, ResultCode &out_code)
 *  We want:           SystemResult<T> fn(...)
 *
 *  The adapt_*() helpers below make this conversion explicit and reusable,
 *  so that the concrete adapters stay as thin pass-throughs with no ad-hoc
 *  result construction scattered across 50+ methods.
 */

#include "system.h"
#include "common/logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace system_layer
{

    // =============================================================================
    // Section 1 — Adapter Helper: result translation utilities
    // =============================================================================

    namespace detail
    {
        /**
         * Convert the S1/S3 bool+out_code+out_value pattern into SystemResult<T>.
         * Usage:
         *   return adapt_bool<int>(
         *       [&](int &val, JusticeFlow::ResultCode &rc) {
         *           return subsystem1::Subsystem1::scheduleDuty(..., val, rc);
         *       });
         */
        template <typename T, typename Fn>
        SystemResult<T> adapt_bool(Fn &&fn)
        {
            T val{};
            JusticeFlow::ResultCode rc{};
            if (fn(val, rc))
                return SystemResult<T>::success(std::move(val));
            return SystemResult<T>::failure(rc);
        }

        /**
         * Overload for bool+out_code operations that produce no value (void).
         */
        template <typename Fn>
        SystemResult<void> adapt_bool_void(Fn &&fn)
        {
            JusticeFlow::ResultCode rc{};
            if (fn(rc))
                return SystemResult<void>::success();
            return SystemResult<void>::failure(rc);
        }

        /**
         * Convert the S1/S3 ResultCode+out_value pattern into SystemResult<T>.
         * Usage:
         *   return adapt_rc<JusticeFlow::Officer>(
         *       [&](JusticeFlow::Officer &out) {
         *           return subsystem1::Subsystem1::getOfficerById(conn, id, out);
         *       });
         */
        template <typename T, typename Fn>
        SystemResult<T> adapt_rc(Fn &&fn)
        {
            T val{};
            JusticeFlow::ResultCode rc = fn(val);
            if (rc == JusticeFlow::ResultCode::OK)
                return SystemResult<T>::success(std::move(val));
            return SystemResult<T>::failure(rc);
        }

        /**
         * Convert bare ResultCode into SystemResult<void>.
         */
        template <typename Fn>
        SystemResult<void> adapt_rc_void(Fn &&fn)
        {
            JusticeFlow::ResultCode rc = fn();
            if (rc == JusticeFlow::ResultCode::OK)
                return SystemResult<void>::success();
            return SystemResult<void>::failure(rc);
        }

        static JusticeFlow::ResultCode mapPgFailure(PGconn *conn, PGresult *res)
        {
            const char *state = res ? PQresultErrorField(res, PG_DIAG_SQLSTATE) : nullptr;
            if (state != nullptr)
            {
                if (std::strcmp(state, "23505") == 0)
                    return JusticeFlow::ResultCode::ALREADY_EXISTS;
                if (std::strcmp(state, "23503") == 0)
                    return JusticeFlow::ResultCode::FOREIGN_KEY_VIOLATION;
                if (std::strcmp(state, "22000") == 0 || std::strcmp(state, "22P02") == 0)
                    return JusticeFlow::ResultCode::INVALID_INPUT;
            }
            Logger::error(res ? PQresultErrorMessage(res) : PQerrorMessage(conn));
            return JusticeFlow::ResultCode::DB_ERROR;
        }

        static const char *toCaseTypeEnum(JusticeFlow::CaseType t)
        {
            static const char *names[] = {
                "MURDER", "ATTEMPTED_MURDER", "MANSLAUGHTER", "KIDNAPPING", "HUMAN_TRAFFICKING",
                "ROBBERY", "ARMED_ROBBERY", "ASSAULT", "AGGRAVATED_ASSAULT", "RAPE",
                "SEXUAL_ASSAULT", "BURGLARY", "HOME_INVASION", "ARSON", "VANDALISM",
                "DRUG_TRAFFICKING", "DRUG_POSSESSION", "TERRORISM", "EXTORTION", "GANG_ACTIVITY",
                "THEFT", "FRAUD", "CYBERCRIME", "HIT_AND_RUN", "VEHICLE_THEFT",
                "DOMESTIC_VIOLENCE", "HARASSMENT", "BRIBERY", "FORGERY", "PUBLIC_DISTURBANCE"};
            const int idx = static_cast<int>(t);
            return (idx >= 0 && idx < static_cast<int>(sizeof(names) / sizeof(names[0]))) ? names[idx] : "MURDER";
        }

        static const char *toCaseStatusEnum(JusticeFlow::CaseStatus s)
        {
            static const char *names[] = {"REGISTERED", "UNDER_INVESTIGATION", "EVIDENCE_COLLECTED",
                                          "PENDING_TRIAL", "CLOSED", "REOPENED"};
            const int idx = static_cast<int>(s);
            return (idx >= 0 && idx < static_cast<int>(sizeof(names) / sizeof(names[0]))) ? names[idx] : "REGISTERED";
        }

        static const char *toInjurySeverityEnum(JusticeFlow::InjurySeverity s)
        {
            static const char *names[] = {"NONE", "MINOR", "MODERATE", "SEVERE", "FATAL"};
            const int idx = static_cast<int>(s);
            return (idx >= 0 && idx < 5) ? names[idx] : "NONE";
        }

        static const char *toVulnerabilityEnum(JusticeFlow::VulnerabilityCategory s)
        {
            static const char *names[] = {"NONE", "MINOR", "ELDERLY", "DIFFERENTLY_ABLED", "FEMALE_ALONE"};
            const int idx = static_cast<int>(s);
            return (idx >= 0 && idx < 5) ? names[idx] : "NONE";
        }

        static const char *toWitnessProtectionEnum(JusticeFlow::WitnessProtection s)
        {
            static const char *names[] = {"NONE", "MONITORED", "PROTECTED", "RELOCATED"};
            const int idx = static_cast<int>(s);
            return (idx >= 0 && idx < 4) ? names[idx] : "NONE";
        }

        static const char *toInvolvementTypeEnum(JusticeFlow::InvolvementType s)
        {
            static const char *names[] = {"SUSPECT", "ACCUSED", "CONVICTED", "ACQUITTED"};
            const int idx = static_cast<int>(s);
            return (idx >= 0 && idx < 4) ? names[idx] : "SUSPECT";
        }

        static const char *toAssociationTypeEnum(JusticeFlow::AssociationType s)
        {
            static const char *names[] = {"CO_ACCUSED", "GANG_MEMBER", "ACCOMPLICE", "FAMILY", "KNOWN_ASSOCIATE"};
            const int idx = static_cast<int>(s);
            return (idx >= 0 && idx < 5) ? names[idx] : "KNOWN_ASSOCIATE";
        }

        static const char *toVehicleRoleEnum(JusticeFlow::VehicleRole s)
        {
            static const char *names[] = {"STOLEN", "USED_IN_CRIME", "ABANDONED", "EVIDENCE", "SUSPECTS_VEHICLE", "VICTIMS_VEHICLE"};
            const int idx = static_cast<int>(s);
            return (idx >= 0 && idx < 6) ? names[idx] : "EVIDENCE";
        }

    } // namespace detail

    // =============================================================================
    // Section 2 — Default Concrete Adapters
    // =============================================================================
    // These are process-internal — not declared in any public header.
    // They are constructed by SystemManager::init() when no injection is provided.
    // =============================================================================

    // -----------------------------------------------------------------------------
    // DefaultAuthAdapter
    // -----------------------------------------------------------------------------

    class DefaultAuthAdapter final : public IAuthAdapter
    {
    public:
        SystemResult<std::string> login(const char *cnic, const char *password) override
        {
            JusticeFlow::SessionContext session{};
            JusticeFlow::ResultCode rc =
                auth::AuthManager::getInstance().login(cnic, password, session);
            if (rc == JusticeFlow::ResultCode::OK)
            {
                // extract token from session (may be empty if not provided)
                return SystemResult<std::string>::success(std::move(session.sessionToken));
            }
            return SystemResult<std::string>::failure(rc);
        }

        SystemResult<JusticeFlow::SessionContext> validateToken(const char *token) override
        {
            JusticeFlow::SessionContext session{};
            JusticeFlow::ResultCode rc =
                auth::AuthManager::getInstance().validateToken(token, session);
            if (rc == JusticeFlow::ResultCode::OK)
                return SystemResult<JusticeFlow::SessionContext>::success(std::move(session));
            return SystemResult<JusticeFlow::SessionContext>::failure(rc);
        }

        SystemResult<void> validateRank(
            const JusticeFlow::SessionContext &session,
            JusticeFlow::OfficerRank required) override
        {
            JusticeFlow::ResultCode rc =
                auth::AuthManager::getInstance().validateRank(session, static_cast<int>(required));
            if (rc == JusticeFlow::ResultCode::OK)
                return SystemResult<void>::success();
            return SystemResult<void>::failure(rc);
        }

        bool isDutyActive(int officer_id) override
        {
            bool out_active = false;
            auth::AuthManager::getInstance().isDutyActive(officer_id, out_active);
            return out_active;
        }

        SystemResult<void> refreshSession(const char *token) override
        {
            JusticeFlow::SessionContext session{};
            if (token)
                session.sessionToken = token;
            JusticeFlow::ResultCode rc =
                auth::AuthManager::getInstance().refreshSession(session);
            if (rc == JusticeFlow::ResultCode::OK)
                return SystemResult<void>::success();
            return SystemResult<void>::failure(rc);
        }

        SystemResult<void> logout(const char *token) override
        {
            // 1. Fail early if token is invalid (if applicable)
            if (!token || token[0] == '\0')
            {
                return SystemResult<void>::failure(JusticeFlow::ResultCode::INVALID_ARGUMENT);
            }

            // 2. Prepare Context
            JusticeFlow::SessionContext session{};
            session.sessionToken = token;

            // 3. Execute
            auto &authManager = auth::AuthManager::getInstance();
            const JusticeFlow::ResultCode rc = authManager.logout(session);

            // 4. Return mapped result
            return (rc == JusticeFlow::ResultCode::OK)
                       ? SystemResult<void>::success()
                       : SystemResult<void>::failure(rc);
        }
    };

    // -----------------------------------------------------------------------------
    // DefaultSubsystem1Adapter
    // S1 now handles ONLY: Duty & Patrol + Officers & Personnel
    // Case/Party CRUD moved entirely to S2.
    // Translates the all-static S1 API (bool+out_code / ResultCode+out-param)
    // into uniform SystemResult<T> using the Section-1 helpers.
    // -----------------------------------------------------------------------------

    // ---- Replace the entire DefaultSubsystem1Adapter implementation with this ----

    class DefaultSubsystem1Adapter final : public ISubsystem1Adapter
    {
    public:
        // ── Duty Scheduling ───────────────────────────────────────────────────────

        SystemResult<int> scheduleDuty(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, int station_id, int patrol_route_id,
            JusticeFlow::ShiftType shift_type,
            const char *duty_date, time_t scheduled_start,
            time_t scheduled_end) override
        {
            return detail::adapt_bool<int>(
                [&](int &id, JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::scheduleDuty(
                        conn, session, officer_id, station_id, patrol_route_id,
                        shift_type, duty_date, scheduled_start, scheduled_end,
                        id, rc);
                });
        }

        SystemResult<void> markDutyStart(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int duty_id) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::markDutyStart(conn, session, duty_id, rc);
                });
        }

        SystemResult<void> markDutyEnd(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int duty_id) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::markDutyEnd(conn, session, duty_id, rc);
                });
        }

        SystemResult<void> updateDutyStatus(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int duty_id, JusticeFlow::DutyStatus new_status,
            const char *absence_reason) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::updateDutyStatus(
                        conn, session, duty_id, new_status, absence_reason, rc);
                });
        }

        SystemResult<void> cancelDuty(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int duty_id) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::cancelDuty(conn, session, duty_id, rc);
                });
        }

        // ── Duty Queries ──────────────────────────────────────────────────────────

        SystemResult<std::vector<JusticeFlow::DutyRoster>> getDutyRoster(
            PGconn *conn, int station_id, const char *duty_date) override
        {
            return detail::adapt_rc<std::vector<JusticeFlow::DutyRoster>>(
                [&](std::vector<JusticeFlow::DutyRoster> &out)
                {
                    return subsystem1::Subsystem1::getDutyRoster(conn, station_id, duty_date, out);
                });
        }

        SystemResult<std::vector<JusticeFlow::DutyRoster>> getActiveDuties(
            PGconn *conn, int station_id) override
        {
            return detail::adapt_rc<std::vector<JusticeFlow::DutyRoster>>(
                [&](std::vector<JusticeFlow::DutyRoster> &out)
                {
                    return subsystem1::Subsystem1::getActiveDuties(conn, station_id, out);
                });
        }

        SystemResult<std::vector<JusticeFlow::DutyRoster>> getOfficerDutyHistory(
            PGconn *conn, int officer_id, time_t from, time_t to) override
        {
            return detail::adapt_rc<std::vector<JusticeFlow::DutyRoster>>(
                [&](std::vector<JusticeFlow::DutyRoster> &out)
                {
                    return subsystem1::Subsystem1::getOfficerDutyHistory(
                        conn, officer_id, from, to, out);
                });
        }

        // ── Patrol Routes ─────────────────────────────────────────────────────────

        SystemResult<int> createPatrolRoute(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int station_id, const char *beat_code,
            const char *route_name, const char *area_description) override
        {
            return detail::adapt_bool<int>(
                [&](int &id, JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::createPatrolRoute(
                        conn, session, station_id, beat_code, route_name,
                        area_description, id, rc);
                });
        }

        SystemResult<void> deactivatePatrolRoute(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int route_id) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::deactivatePatrolRoute(
                        conn, session, route_id, rc);
                });
        }

        SystemResult<std::vector<JusticeFlow::PatrolRoute>> getPatrolRoutesByStation(
            PGconn *conn, int station_id) override
        {
            return detail::adapt_rc<std::vector<JusticeFlow::PatrolRoute>>(
                [&](std::vector<JusticeFlow::PatrolRoute> &out)
                {
                    return subsystem1::Subsystem1::getPatrolRoutesByStation(conn, station_id, out);
                });
        }

        // ── Personnel ─────────────────────────────────────────────────────────────

        SystemResult<JusticeFlow::Officer> getOfficerById(
            PGconn *conn, int officer_id) override
        {
            return detail::adapt_rc<JusticeFlow::Officer>(
                [&](JusticeFlow::Officer &out)
                {
                    return subsystem1::Subsystem1::getOfficerById(conn, officer_id, out);
                });
        }

        SystemResult<JusticeFlow::Officer> getOfficerByCnic(
            PGconn *conn, const char *cnic) override
        {
            return detail::adapt_rc<JusticeFlow::Officer>(
                [&](JusticeFlow::Officer &out)
                {
                    return subsystem1::Subsystem1::getOfficerByCnic(conn, cnic, out);
                });
        }

        SystemResult<std::vector<JusticeFlow::Officer>> getOfficersByStation(
            PGconn *conn, int station_id) override
        {
            return detail::adapt_rc<std::vector<JusticeFlow::Officer>>(
                [&](std::vector<JusticeFlow::Officer> &out)
                {
                    return subsystem1::Subsystem1::getOfficersByStation(conn, station_id, out);
                });
        }

        SystemResult<std::vector<JusticeFlow::Officer>> getOfficersByStatus(
            PGconn *conn, int station_id, JusticeFlow::OfficerStatus status) override
        {
            return detail::adapt_rc<std::vector<JusticeFlow::Officer>>(
                [&](std::vector<JusticeFlow::Officer> &out)
                {
                    return subsystem1::Subsystem1::getOfficersByStatus(
                        conn, station_id, status, out);
                });
        }

        SystemResult<void> updateOfficerStatus(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, JusticeFlow::OfficerStatus new_status) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::updateOfficerStatus(
                        conn, session, officer_id, new_status, rc);
                });
        }

        SystemResult<int> promoteOfficer(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, JusticeFlow::OfficerRank new_rank,
            const char *new_belt_number, const char *promotion_type,
            const char *effective_date, const char *order_date) override
        {
            return detail::adapt_bool<int>(
                [&](int &id, JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::promoteOfficer(
                        conn, session, officer_id, new_rank, new_belt_number,
                        promotion_type, effective_date, order_date, id, rc);
                });
        }

        SystemResult<std::vector<JusticeFlow::OfficerRankHistory>> getOfficerRankHistory(
            PGconn *conn, int officer_id) override
        {
            return detail::adapt_rc<std::vector<JusticeFlow::OfficerRankHistory>>(
                [&](std::vector<JusticeFlow::OfficerRankHistory> &out)
                {
                    return subsystem1::Subsystem1::getOfficerRankHistory(conn, officer_id, out);
                });
        }

        SystemResult<int> deployOfficer(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, int to_station_id,
            const char *deployment_reason, const char *order_number,
            const char *deployed_from, const char *deployed_until) override
        {
            return detail::adapt_bool<int>(
                [&](int &id, JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::deployOfficer(
                        conn, session, officer_id, to_station_id, deployment_reason,
                        order_number, deployed_from, deployed_until, id, rc);
                });
        }

        SystemResult<void> endDeployment(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int deployment_id) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem1::Subsystem1::endDeployment(conn, session, deployment_id, rc);
                });
        }

        SystemResult<std::vector<JusticeFlow::OfficerDeployment>> getOfficerDeployments(
            PGconn *conn, int officer_id, bool active_only) override
        {
            return detail::adapt_rc<std::vector<JusticeFlow::OfficerDeployment>>(
                [&](std::vector<JusticeFlow::OfficerDeployment> &out)
                {
                    return subsystem1::Subsystem1::getOfficerDeployments(
                        conn, officer_id, active_only, out);
                });
        }

        // ── Reports ───────────────────────────────────────────────────────────────

        SystemResult<std::string> generateOfficerReport(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int officer_id, JusticeFlow::ReportType type) override
        {
            return detail::adapt_rc<std::string>(
                [&](std::string &out)
                {
                    return subsystem1::Subsystem1::generateOfficerReport(
                        conn, session, officer_id, type, out);
                });
        }
    };

    // -----------------------------------------------------------------------------
    // DefaultSubsystem2Adapter
    // Wraps the Subsystem2 singleton.  S2 already returns ResultCode throughout;
    // Fix #3: entity out-params become unique_ptr inside SystemResult.
    // S2 now handles: FIR registration, evidence, charge sheets, AND case CRUD/parties.
    // -----------------------------------------------------------------------------

    class DefaultSubsystem2Adapter final : public ISubsystem2Adapter
    {
    public:
        SystemResult<std::unique_ptr<subsystem2::Case>> registerFIR(
            const subsystem2::FIRRegistrationRequest &request,
            const JusticeFlow::SessionContext &session) override
        {
            subsystem2::Case *raw = nullptr;
            JusticeFlow::ResultCode rc =
                subsystem2::Subsystem2::getInstance().registerFIR(request, session, raw);
            if (rc == JusticeFlow::ResultCode::OK && raw)
                return SystemResult<std::unique_ptr<subsystem2::Case>>::success(
                    std::unique_ptr<subsystem2::Case>(raw));
            return SystemResult<std::unique_ptr<subsystem2::Case>>::failure(rc);
        }

        SystemResult<std::unique_ptr<subsystem2::Evidence>> logAndSecureEvidence(
            int64_t case_id, JusticeFlow::EvidenceType type,
            const std::string &desc, const std::string &file_path,
            const JusticeFlow::SessionContext &session) override
        {
            subsystem2::Evidence *raw = nullptr;
            JusticeFlow::ResultCode rc =
                subsystem2::Subsystem2::getInstance().logAndSecureEvidence(
                    case_id, type, desc, file_path, session, raw);
            if (rc == JusticeFlow::ResultCode::OK && raw)
                return SystemResult<std::unique_ptr<subsystem2::Evidence>>::success(
                    std::unique_ptr<subsystem2::Evidence>(raw));
            return SystemResult<std::unique_ptr<subsystem2::Evidence>>::failure(rc);
        }

        SystemResult<std::unique_ptr<subsystem2::ChargeSheet>> draftChargeSheet(
            int64_t case_id, const JusticeFlow::SessionContext &session) override
        {
            subsystem2::ChargeSheet *raw = nullptr;
            JusticeFlow::ResultCode rc =
                subsystem2::Subsystem2::getInstance().draftChargeSheet(case_id, session, raw);
            if (rc == JusticeFlow::ResultCode::OK && raw)
                return SystemResult<std::unique_ptr<subsystem2::ChargeSheet>>::success(
                    std::unique_ptr<subsystem2::ChargeSheet>(raw));
            return SystemResult<std::unique_ptr<subsystem2::ChargeSheet>>::failure(rc);
        }

        SystemResult<void> submitChargeSheet(
            subsystem2::ChargeSheet *sheet,
            const JusticeFlow::SessionContext &session) override
        {
            JusticeFlow::ResultCode rc =
                subsystem2::Subsystem2::getInstance().submitChargeSheet(sheet, session);
            if (rc == JusticeFlow::ResultCode::OK)
                return SystemResult<void>::success();
            return SystemResult<void>::failure(rc);
        }

        SystemResult<std::unique_ptr<subsystem2::Case>> fetchCase(int64_t case_id) override
        {
            subsystem2::Case *raw = nullptr;
            JusticeFlow::ResultCode rc =
                subsystem2::Subsystem2::getInstance().fetchCase(case_id, raw);
            if (rc == JusticeFlow::ResultCode::OK && raw)
                return SystemResult<std::unique_ptr<subsystem2::Case>>::success(
                    std::unique_ptr<subsystem2::Case>(raw));
            return SystemResult<std::unique_ptr<subsystem2::Case>>::failure(rc);
        }
    };

    // -----------------------------------------------------------------------------
    // DefaultSubsystem3Adapter
    // Wraps the mixed-convention Subsystem3 static facade.
    // Audit lifecycle is managed by SystemManager::init/shutdown, not here.
    // -----------------------------------------------------------------------------

    class DefaultSubsystem3Adapter final : public ISubsystem3Adapter
    {
    public:
        // ── Audit ─────────────────────────────────────────────────────────────────

        SystemResult<std::vector<audit::AuditRecord>> getAuditChangeHistory(
            int case_id) override
        {
            return detail::adapt_rc<std::vector<audit::AuditRecord>>(
                [&](std::vector<audit::AuditRecord> &out)
                {
                    return subsystem3::Subsystem3::getAuditChangeHistory(case_id, out);
                });
        }

        SystemResult<std::vector<audit::AuditRecord>> getAuditOfficerActions(
            int officer_id, time_t from, time_t to) override
        {
            return detail::adapt_rc<std::vector<audit::AuditRecord>>(
                [&](std::vector<audit::AuditRecord> &out)
                {
                    return subsystem3::Subsystem3::getAuditOfficerActions(officer_id, from, to, out);
                });
        }

        SystemResult<std::vector<audit::AuditRecord>> getAuditTableChanges(
            const char *table_name, int record_id) override
        {
            return detail::adapt_rc<std::vector<audit::AuditRecord>>(
                [&](std::vector<audit::AuditRecord> &out)
                {
                    return subsystem3::Subsystem3::getAuditTableChanges(table_name, record_id, out);
                });
        }

        SystemResult<std::vector<audit::AuditRecord>> auditQueryByTimeWindow(
            time_t from, time_t to) override
        {
            return detail::adapt_rc<std::vector<audit::AuditRecord>>(
                [&](std::vector<audit::AuditRecord> &out)
                {
                    return subsystem3::Subsystem3::auditQueryByTimeWindow(from, to, out);
                });
        }

        SystemResult<std::vector<audit::AuditRecord>> detectSuspiciousActivity(
            int station_id) override
        {
            return detail::adapt_rc<std::vector<audit::AuditRecord>>(
                [&](std::vector<audit::AuditRecord> &out)
                {
                    return subsystem3::Subsystem3::detectSuspiciousActivity(station_id, out);
                });
        }

        // ── Warrants ──────────────────────────────────────────────────────────────

        SystemResult<int> requestWarrant(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int case_id, const char *accused_cnic, JusticeFlow::WarrantType wt,
            const char *magistrate, const char *court,
            const char *valid_until, const char *target_address) override
        {
            return detail::adapt_bool<int>(
                [&](int &id, JusticeFlow::ResultCode &rc)
                {
                    return subsystem3::Subsystem3::requestWarrant(
                        conn, session, case_id, accused_cnic, wt,
                        magistrate, court, valid_until, target_address, id, rc);
                });
        }

        SystemResult<void> executeWarrant(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int warrant_id) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem3::Subsystem3::executeWarrant(conn, session, warrant_id, rc);
                });
        }

        SystemResult<void> cancelWarrant(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int warrant_id, const char *reason) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem3::Subsystem3::cancelWarrant(conn, session, warrant_id, reason, rc);
                });
        }

        SystemResult<std::vector<enforcement::WarrantRecord>> getWarrantsByCase(
            PGconn *conn, int case_id) override
        {
            return detail::adapt_rc<std::vector<enforcement::WarrantRecord>>(
                [&](std::vector<enforcement::WarrantRecord> &out)
                {
                    return subsystem3::Subsystem3::getWarrantsByCase(conn, case_id, out);
                });
        }

        SystemResult<std::vector<enforcement::WarrantRecord>> getActiveWarrants(
            PGconn *conn, int station_id) override
        {
            return detail::adapt_rc<std::vector<enforcement::WarrantRecord>>(
                [&](std::vector<enforcement::WarrantRecord> &out)
                {
                    return subsystem3::Subsystem3::getActiveWarrants(conn, station_id, out);
                });
        }

        // ── Arrests ───────────────────────────────────────────────────────────────

        SystemResult<int> recordArrest(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int case_id, const char *cnic,
            const char *location, int warrant_id) override
        {
            return detail::adapt_bool<int>(
                [&](int &id, JusticeFlow::ResultCode &rc)
                {
                    return subsystem3::Subsystem3::recordArrest(
                        conn, session, case_id, cnic, location, warrant_id, id, rc);
                });
        }

        SystemResult<void> updateCustodyStatus(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int arrest_id, JusticeFlow::CustodyStatus st, const char *reason) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem3::Subsystem3::updateCustodyStatus(
                        conn, session, arrest_id, st, reason, rc);
                });
        }

        SystemResult<void> markArrestAsDisputed(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int arrest_id, const char *reason) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem3::Subsystem3::markArrestAsDisputed(
                        conn, session, arrest_id, reason, rc);
                });
        }

        SystemResult<std::vector<enforcement::ArrestRecord>> getArrestsByCase(
            PGconn *conn, int case_id) override
        {
            return detail::adapt_rc<std::vector<enforcement::ArrestRecord>>(
                [&](std::vector<enforcement::ArrestRecord> &out)
                {
                    return subsystem3::Subsystem3::getArrestsByCase(conn, case_id, out);
                });
        }

        // ── Bail ──────────────────────────────────────────────────────────────────

        SystemResult<int> recordBail(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int arrest_id, JusticeFlow::BailType bt, uint64_t amount,
            const char *court, const char *magistrate, const char *valid_until,
            const char *surety_name, const char *surety_cnic,
            const char *surety_contact) override
        {
            return detail::adapt_bool<int>(
                [&](int &id, JusticeFlow::ResultCode &rc)
                {
                    return subsystem3::Subsystem3::recordBail(
                        conn, session, arrest_id, bt, amount,
                        court, magistrate, valid_until,
                        surety_name, surety_cnic, surety_contact, id, rc);
                });
        }

        SystemResult<void> revokeBail(
            PGconn *conn, const JusticeFlow::SessionContext &session,
            int bail_id, const char *reason) override
        {
            return detail::adapt_bool_void(
                [&](JusticeFlow::ResultCode &rc)
                {
                    return subsystem3::Subsystem3::revokeBail(conn, session, bail_id, reason, rc);
                });
        }

        SystemResult<enforcement::BailRecord> getBailByArrest(
            PGconn *conn, int arrest_id) override
        {
            return detail::adapt_rc<enforcement::BailRecord>(
                [&](enforcement::BailRecord &out)
                {
                    return subsystem3::Subsystem3::getBailByArrest(conn, arrest_id, out);
                });
        }

        // ── Forensic & Lab ────────────────────────────────────────────────────────

        SystemResult<int> createForensicRequest(
            const char *token, int case_id,
            const char *purpose, const char *purpose_desc,
            const char *lab_name, const char *examiner_name) override
        {
            return detail::adapt_rc<int>(
                [&](int &id)
                {
                    return subsystem3::Subsystem3::createForensicRequest(
                        token, case_id, purpose, purpose_desc,
                        lab_name, examiner_name, id);
                });
        }

        SystemResult<void> linkEvidence(
            const char *token, int request_id,
            int evidence_id, const char *notes) override
        {
            return detail::adapt_rc_void(
                [&]()
                {
                    return subsystem3::Subsystem3::linkEvidence(token, request_id, evidence_id, notes);
                });
        }

        SystemResult<void> recordLabReceipt(
            const char *token, int request_id, const char *date) override
        {
            return detail::adapt_rc_void(
                [&]()
                {
                    return subsystem3::Subsystem3::recordLabReceipt(token, request_id, date);
                });
        }

        SystemResult<void> recordExaminationStart(
            const char *token, int request_id) override
        {
            return detail::adapt_rc_void(
                [&]()
                {
                    return subsystem3::Subsystem3::recordExaminationStart(token, request_id);
                });
        }

        SystemResult<void> recordFindings(
            const char *token, int request_id,
            const char *findings, const char *file_path,
            const char *delivery_date) override
        {
            return detail::adapt_rc_void(
                [&]()
                {
                    return subsystem3::Subsystem3::recordFindings(
                        token, request_id, findings, file_path, delivery_date);
                });
        }

        SystemResult<void> recordAmendment(
            const char *token, int request_id, const char *amended) override
        {
            return detail::adapt_rc_void(
                [&]()
                {
                    return subsystem3::Subsystem3::recordAmendment(token, request_id, amended);
                });
        }

        SystemResult<void> contestReport(
            const char *token, int request_id, const char *reason) override
        {
            return detail::adapt_rc_void(
                [&]()
                {
                    return subsystem3::Subsystem3::contestReport(token, request_id, reason);
                });
        }

        SystemResult<std::vector<forensic::ForensicRecord>> getForensicRequestsByCase(
            const char *token, int case_id) override
        {
            return detail::adapt_rc<std::vector<forensic::ForensicRecord>>(
                [&](std::vector<forensic::ForensicRecord> &out)
                {
                    return subsystem3::Subsystem3::getForensicRequestsByCase(token, case_id, out);
                });
        }

        SystemResult<std::vector<forensic::ForensicRecord>> getPendingForensicRequests(
            const char *token, int station_id) override
        {
            return detail::adapt_rc<std::vector<forensic::ForensicRecord>>(
                [&](std::vector<forensic::ForensicRecord> &out)
                {
                    return subsystem3::Subsystem3::getPendingForensicRequests(token, station_id, out);
                });
        }

        SystemResult<std::vector<forensic::EvidenceRef>> getEvidenceByForensicRequest(
            const char *token, int request_id) override
        {
            return detail::adapt_rc<std::vector<forensic::EvidenceRef>>(
                [&](std::vector<forensic::EvidenceRef> &out)
                {
                    return subsystem3::Subsystem3::getEvidenceByForensicRequest(token, request_id, out);
                });
        }
    };

    // =============================================================================
    // Section 3 — SystemManager: Singleton + Lifecycle
    // =============================================================================

    SystemManager &SystemManager::getInstance()
    {
        static SystemManager instance; // C++11 §6.7: thread-safe
        return instance;
    }

    // ── Fix #7: injection guard ───────────────────────────────────────────────────

    void SystemManager::assertNotInitialized(const char *caller) const
    {
        if (initialized_.load(std::memory_order_acquire))
        {
            throw std::logic_error(
                std::string("[System] ") + caller +
                " called after init(). Adapter injection must precede init().");
        }
    }

    void SystemManager::injectAuth(std::unique_ptr<IAuthAdapter> adapter)
    {
        assertNotInitialized("injectAuth");
        auth_adapter_ = std::move(adapter);
    }

    void SystemManager::injectS1(std::unique_ptr<ISubsystem1Adapter> adapter)
    {
        assertNotInitialized("injectS1");
        s1_adapter_ = std::move(adapter);
    }

    void SystemManager::injectS2(std::unique_ptr<ISubsystem2Adapter> adapter)
    {
        assertNotInitialized("injectS2");
        s2_adapter_ = std::move(adapter);
    }

    void SystemManager::injectS3(std::unique_ptr<ISubsystem3Adapter> adapter)
    {
        assertNotInitialized("injectS3");
        s3_adapter_ = std::move(adapter);
    }

    // ── Fix #4: staged init ───────────────────────────────────────────────────────

    SystemResult<void> SystemManager::init(const SystemInitConfig &config)
    {
        if (initialized_.load(std::memory_order_acquire))
            return SystemResult<void>::success();

        Logger::info("[System] Initialising JusticeFlow SystemManager.");

        // ── Stage 1: install default adapters for any uninjected slot ─────────────
        if (!auth_adapter_)
            auth_adapter_ = std::make_unique<DefaultAuthAdapter>();
        if (!s1_adapter_)
            s1_adapter_ = std::make_unique<DefaultSubsystem1Adapter>();
        if (!s2_adapter_)
            s2_adapter_ = std::make_unique<DefaultSubsystem2Adapter>();
        if (!s3_adapter_)
            s3_adapter_ = std::make_unique<DefaultSubsystem3Adapter>();

        if (!auth_adapter_ || !s1_adapter_ || !s2_adapter_ || !s3_adapter_)
        {
            Logger::error("[System] Adapter initialization failed.");
            return SystemResult<void>::failure(JusticeFlow::ResultCode::DB_ERROR);
        }

        // ── Stage 2: Auth init (no external I/O; AuthManager boots on first use) ──
        Logger::info("[System] Stage 2: Auth ready.");

        // ── Stage 3: S1 + S2 are stateless; nothing to boot ──────────────────────
        Logger::info("[System] Stage 3: S1/S2 adapters ready.");

        // ── Stage 4: S3 audit — opens a dedicated read-only DB connection ─────────
        Logger::info("[System] Stage 4: Connecting S3 audit subsystem.");
        try
        {
            JusticeFlow::ResultCode rc =
                subsystem3::Subsystem3::initAudit(config.audit_db_conninfo);
            if (rc != JusticeFlow::ResultCode::OK)
                Logger::error("[System] S3 audit init failed. Proceeding anyway (audit disabled).");
            else
                Logger::info("[System] Stage 4: S3 audit connected.");
        }
        catch (const std::exception &e)
        {
            Logger::error((std::string("[System] S3 audit threw: ") + e.what() + ". Continuing.").c_str());
        }
        catch (...)
        {
            Logger::error("[System] S3 audit threw unknown exception. Continuing.");
        }

        // ── Stage 5: wire sub-facades to their adapter raw pointers ──────────────
        auth_facade_.setAdapter(auth_adapter_.get());
        case_facade_.setAdapter(s2_adapter_.get());
        inv_facade_.setAdapter(s2_adapter_.get());
        personnel_facade_.setAdapter(s1_adapter_.get());
        duty_facade_.setAdapter(s1_adapter_.get());
        enforcement_facade_.setAdapter(s3_adapter_.get());
        audit_facade_.setAdapter(s3_adapter_.get());
        forensic_facade_.setAdapter(s3_adapter_.get());

        // Fix #5: release ordering ensures facade pointers are visible before flag
        initialized_.store(true, std::memory_order_release);
        Logger::info("[System] SystemManager fully initialised.");
        return SystemResult<void>::success();
    }

    void SystemManager::shutdown()
    {
        if (!initialized_.load(std::memory_order_acquire))
            return;

        Logger::info("[System] Shutting down JusticeFlow SystemManager.");

        // Reverse-init order: Stage 4 first
        subsystem3::Subsystem3::shutdownAudit();
        Logger::info("[System] S3 audit connection closed.");

        // Stages 3, 2 — stateless adapters; nothing to tear down

        // Nullify facade adapter pointers before releasing adapters
        forensic_facade_.setAdapter(nullptr);
        audit_facade_.setAdapter(nullptr);
        enforcement_facade_.setAdapter(nullptr);
        duty_facade_.setAdapter(nullptr);
        personnel_facade_.setAdapter(nullptr);
        inv_facade_.setAdapter(nullptr);
        case_facade_.setAdapter(nullptr);
        auth_facade_.setAdapter(nullptr);

        // Release adapters (unique_ptr destructor handles deallocation)
        s3_adapter_.reset();
        s2_adapter_.reset();
        s1_adapter_.reset();
        auth_adapter_.reset();

        initialized_.store(false, std::memory_order_release);
        Logger::info("[System] SystemManager shutdown complete.");
    }

    // =============================================================================
    // Section 4 — AuthFacade
    // =============================================================================

    SystemResult<std::string> AuthFacade::login(const char *cnic, const char *password)
    {
        return adapter_->login(cnic, password);
    }

    SystemResult<JusticeFlow::SessionContext> AuthFacade::validateToken(const char *token)
    {
        return adapter_->validateToken(token);
    }

    // ✅ FIXED
    SystemResult<void> AuthFacade::validateRank(
        const JusticeFlow::SessionContext &session,
        JusticeFlow::OfficerRank required)
    {
        // Verify session rank meets minimum requirement
        if (session.rank < required)
        {
            return SystemResult<void>::failure(JusticeFlow::ResultCode::RANK_INSUFFICIENT);
        }
        return SystemResult<void>::success();
    }

    bool AuthFacade::isDutyActive(int officer_id)
    {
        return adapter_->isDutyActive(officer_id);
    }

    SystemResult<void> AuthFacade::refreshSession(const char *token)
    {
        return adapter_->refreshSession(token);
    }

    SystemResult<void> AuthFacade::logout(const char *token)
    {
        return adapter_->logout(token);
    }

    // =============================================================================
    // Section 5 — CaseFacade (now delegates to S2, not S1)
    // =============================================================================

    SystemResult<int> CaseFacade::registerCase(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        JusticeFlow::CaseType type, time_t filed_time,
        const char *address, const char *desc,
        double lat, double lon,
        int station_id, const char *cnic)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK || cnic == nullptr || cnic[0] == '\0')
            return SystemResult<int>::failure(JusticeFlow::ResultCode::INVALID_INPUT);

        static std::atomic<unsigned long> fir_seq{0};
        char fir[64], ts_file[32], lat_s[32], lon_s[32], station_s[32], filed_by_s[32];
        const unsigned long seq = fir_seq.fetch_add(1, std::memory_order_relaxed) + 1;
        std::snprintf(fir, sizeof(fir), "FIR-%ld-%06lu", static_cast<long>(filed_time), seq);
        std::snprintf(ts_file, sizeof(ts_file), "%ld", static_cast<long>(filed_time));
        std::snprintf(lat_s, sizeof(lat_s), "%.6f", lat);
        std::snprintf(lon_s, sizeof(lon_s), "%.6f", lon);
        std::snprintf(station_s, sizeof(station_s), "%d", station_id);
        std::snprintf(filed_by_s, sizeof(filed_by_s), "%d", session.officerId > 0 ? session.officerId : 1);

        const char *values[] = {fir, detail::toCaseTypeEnum(type), ts_file, address ? address : "",
                                desc ? desc : "", lat_s, lon_s, station_s, cnic, filed_by_s, ts_file};
        PGresult *res = PQexecParams(
            conn,
            "INSERT INTO cases (fir_number, case_type, case_status, incident_date, incident_address, incident_description, "
            "incident_lat, incident_lon, station_id, primary_complainant_cnic, filed_by, filed_at) "
            "VALUES ($1, $2::case_type_enum, 'REGISTERED', to_timestamp($3::bigint), $4, $5, $6::numeric, $7::numeric, "
            "$8::bigint, $9, $10::bigint, to_timestamp($11::bigint)) RETURNING case_id",
            11, nullptr, values, nullptr, nullptr, 0);

        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<int>::failure(rc);
        }
        int case_id = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        return SystemResult<int>::success(case_id);
    }

    SystemResult<JusticeFlow::Case> CaseFacade::getCaseById(PGconn *conn, int case_id)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
        {
            auto result = s2_->fetchCase(case_id);
            if (!result.ok() || !result.value)
                return SystemResult<JusticeFlow::Case>::failure(result.code);
            JusticeFlow::Case fallback{};
            fallback.case_id = result.value->getCaseId();
            return SystemResult<JusticeFlow::Case>::success(std::move(fallback));
        }

        char case_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        const char *values[] = {case_s};
        PGresult *res = PQexecParams(
            conn,
            "SELECT case_id, fir_number, station_id, filed_by FROM cases WHERE case_id = $1::bigint",
            1, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<JusticeFlow::Case>::failure(rc);
        }
        if (PQntuples(res) == 0)
        {
            PQclear(res);
            return SystemResult<JusticeFlow::Case>::failure(JusticeFlow::ResultCode::NOT_FOUND);
        }
        JusticeFlow::Case out{};
        out.case_id = std::atoi(PQgetvalue(res, 0, 0));
        out.fir_number = PQgetvalue(res, 0, 1);
        out.station_id = std::atoi(PQgetvalue(res, 0, 2));
        out.filed_by = std::atoi(PQgetvalue(res, 0, 3));
        PQclear(res);
        return SystemResult<JusticeFlow::Case>::success(std::move(out));
    }

    SystemResult<std::vector<JusticeFlow::Case>> CaseFacade::getCasesByStation(PGconn *conn, int station_id)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<std::vector<JusticeFlow::Case>>::failure(JusticeFlow::ResultCode::DB_ERROR);

        char station_s[32];
        std::snprintf(station_s, sizeof(station_s), "%d", station_id);
        const char *values[] = {station_s};
        PGresult *res = PQexecParams(
            conn,
            "SELECT case_id, fir_number, station_id, filed_by FROM cases WHERE station_id = $1::bigint ORDER BY case_id",
            1, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<std::vector<JusticeFlow::Case>>::failure(rc);
        }

        std::vector<JusticeFlow::Case> out;
        for (int i = 0; i < PQntuples(res); ++i)
        {
            JusticeFlow::Case c{};
            c.case_id = std::atoi(PQgetvalue(res, i, 0));
            c.fir_number = PQgetvalue(res, i, 1);
            c.station_id = std::atoi(PQgetvalue(res, i, 2));
            c.filed_by = std::atoi(PQgetvalue(res, i, 3));
            out.push_back(std::move(c));
        }
        PQclear(res);
        return SystemResult<std::vector<JusticeFlow::Case>>::success(std::move(out));
    }

    SystemResult<std::vector<JusticeFlow::Case>> CaseFacade::getCasesByStatus(PGconn *conn, int station_id, JusticeFlow::CaseStatus status)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<std::vector<JusticeFlow::Case>>::failure(JusticeFlow::ResultCode::DB_ERROR);

        char station_s[32];
        std::snprintf(station_s, sizeof(station_s), "%d", station_id);
        const char *values[] = {station_s, detail::toCaseStatusEnum(status)};
        PGresult *res = PQexecParams(
            conn,
            "SELECT case_id, fir_number, station_id, filed_by FROM cases "
            "WHERE station_id = $1::bigint AND case_status = $2::case_status_enum ORDER BY case_id",
            2, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<std::vector<JusticeFlow::Case>>::failure(rc);
        }

        std::vector<JusticeFlow::Case> out;
        for (int i = 0; i < PQntuples(res); ++i)
        {
            JusticeFlow::Case c{};
            c.case_id = std::atoi(PQgetvalue(res, i, 0));
            c.fir_number = PQgetvalue(res, i, 1);
            c.station_id = std::atoi(PQgetvalue(res, i, 2));
            c.filed_by = std::atoi(PQgetvalue(res, i, 3));
            out.push_back(std::move(c));
        }
        PQclear(res);
        return SystemResult<std::vector<JusticeFlow::Case>>::success(std::move(out));
    }

    SystemResult<void> CaseFacade::updateCaseStatus(PGconn *, const JusticeFlow::SessionContext &, int, JusticeFlow::CaseStatus, const char *)
    {
        return SystemResult<void>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<void> CaseFacade::closeCase(PGconn *conn, const JusticeFlow::SessionContext &session, int case_id, const char *reason)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<void>::failure(JusticeFlow::ResultCode::DB_ERROR);
        char case_s[32], by_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        std::snprintf(by_s, sizeof(by_s), "%d", session.officerId > 0 ? session.officerId : 1);
        const char *values[] = {case_s, reason ? reason : "Closed", by_s};
        PGresult *res = PQexecParams(
            conn,
            "UPDATE cases SET case_status = 'CLOSED', closed_at = NOW(), closure_reason = $2, approved_by = $3::bigint, updated_at = NOW() "
            "WHERE case_id = $1::bigint",
            3, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<void>::failure(rc);
        }
        const bool found = std::atoi(PQcmdTuples(res)) > 0;
        PQclear(res);
        return found ? SystemResult<void>::success() : SystemResult<void>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<void> CaseFacade::reopenCase(PGconn *, const JusticeFlow::SessionContext &, int, const char *)
    {
        return SystemResult<void>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<void> CaseFacade::transferCase(PGconn *, const JusticeFlow::SessionContext &, int, int, const char *)
    {
        return SystemResult<void>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<std::vector<JusticeFlow::CaseStatusLog>> CaseFacade::getCaseStatusLog(PGconn *, int)
    {
        return SystemResult<std::vector<JusticeFlow::CaseStatusLog>>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<void> CaseFacade::assignOfficerToCase(PGconn *, const JusticeFlow::SessionContext &, int, int, JusticeFlow::CaseOfficerRole)
    {
        return SystemResult<void>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<void> CaseFacade::relieveOfficerFromCase(PGconn *, const JusticeFlow::SessionContext &, int, int)
    {
        return SystemResult<void>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<std::vector<JusticeFlow::CaseOfficer>> CaseFacade::getAssignedOfficers(PGconn *, int)
    {
        return SystemResult<std::vector<JusticeFlow::CaseOfficer>>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<int> CaseFacade::addComplainant(PGconn *, const JusticeFlow::SessionContext &, int, const char *, JusticeFlow::RelationshipToVictim, bool)
    {
        return SystemResult<int>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<void> CaseFacade::updateComplainantStatus(PGconn *, const JusticeFlow::SessionContext &, int, JusticeFlow::ComplainantStatus, const char *)
    {
        return SystemResult<void>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<std::vector<JusticeFlow::Complainant>> CaseFacade::getComplainantsByCase(PGconn *, int)
    {
        return SystemResult<std::vector<JusticeFlow::Complainant>>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<int> CaseFacade::addVictim(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int case_id, const char *cnic,
        const char *injury_type, JusticeFlow::InjurySeverity sev,
        JusticeFlow::VulnerabilityCategory vuln, const char *medical_ref)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK || cnic == nullptr || cnic[0] == '\0')
            return SystemResult<int>::failure(JusticeFlow::ResultCode::INVALID_INPUT);

        char case_s[32], by_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        std::snprintf(by_s, sizeof(by_s), "%d", session.officerId > 0 ? session.officerId : 1);
        const char *values[] = {case_s, cnic, injury_type ? injury_type : "", detail::toInjurySeverityEnum(sev),
                                detail::toVulnerabilityEnum(vuln), medical_ref ? medical_ref : "", by_s};
        PGresult *res = PQexecParams(
            conn,
            "INSERT INTO victims (case_id, person_cnic, injury_type, injury_severity, vulnerability_category, medical_report_ref, added_by) "
            "VALUES ($1::bigint, $2, $3, $4::injury_severity_enum, $5::vulnerability_category_enum, $6, $7::bigint) RETURNING victim_id",
            7, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<int>::failure(rc);
        }
        int id = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        return SystemResult<int>::success(id);
    }

    SystemResult<std::vector<JusticeFlow::Victim>> CaseFacade::getVictimsByCase(
        PGconn *conn, int case_id)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<std::vector<JusticeFlow::Victim>>::failure(JusticeFlow::ResultCode::DB_ERROR);
        char case_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        const char *values[] = {case_s};
        PGresult *res = PQexecParams(
            conn,
            "SELECT victim_id, case_id, person_cnic, injury_type, medical_report_ref, added_by FROM victims WHERE case_id = $1::bigint ORDER BY victim_id",
            1, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<std::vector<JusticeFlow::Victim>>::failure(rc);
        }
        std::vector<JusticeFlow::Victim> out;
        for (int i = 0; i < PQntuples(res); ++i)
        {
            JusticeFlow::Victim v{};
            v.victim_id = std::atoi(PQgetvalue(res, i, 0));
            v.case_id = std::atoi(PQgetvalue(res, i, 1));
            v.person_cnic = PQgetvalue(res, i, 2);
            v.injury_type = PQgetvalue(res, i, 3);
            v.medical_report_ref = PQgetvalue(res, i, 4);
            v.added_by = std::atoi(PQgetvalue(res, i, 5));
            out.push_back(std::move(v));
        }
        PQclear(res);
        return SystemResult<std::vector<JusticeFlow::Victim>>::success(std::move(out));
    }

    SystemResult<int> CaseFacade::addWitness(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int case_id, const char *cnic,
        const char *statement, const char *file_path,
        JusticeFlow::WitnessProtection prot, bool conceal)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK || cnic == nullptr || cnic[0] == '\0')
            return SystemResult<int>::failure(JusticeFlow::ResultCode::INVALID_INPUT);
        char case_s[32], by_s[32], conceal_s[2];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        std::snprintf(by_s, sizeof(by_s), "%d", session.officerId > 0 ? session.officerId : 1);
        std::snprintf(conceal_s, sizeof(conceal_s), "%d", conceal ? 1 : 0);
        const char *values[] = {case_s, cnic, statement ? statement : "", file_path ? file_path : "",
                                detail::toWitnessProtectionEnum(prot), conceal_s, by_s};
        PGresult *res = PQexecParams(
            conn,
            "INSERT INTO witnesses (case_id, person_cnic, statement_text, statement_file_path, statement_recorded_at, "
            "recorded_by, protection_status, is_identity_concealed, added_by) "
            "VALUES ($1::bigint, $2, NULLIF($3,''), NULLIF($4,''), NOW(), $7::bigint, $5::witness_protection_enum, "
            "($6::int = 1), $7::bigint) RETURNING witness_id",
            7, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<int>::failure(rc);
        }
        int id = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        return SystemResult<int>::success(id);
    }

    SystemResult<void> CaseFacade::updateWitnessProtection(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int witness_id, JusticeFlow::WitnessProtection st)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<void>::failure(JusticeFlow::ResultCode::DB_ERROR);
        char witness_s[32], by_s[32];
        std::snprintf(witness_s, sizeof(witness_s), "%d", witness_id);
        std::snprintf(by_s, sizeof(by_s), "%d", session.officerId > 0 ? session.officerId : 1);
        const char *values[] = {detail::toWitnessProtectionEnum(st), by_s, witness_s};
        PGresult *res = PQexecParams(
            conn,
            "UPDATE witnesses SET protection_status = $1::witness_protection_enum, recorded_by = $2::bigint, updated_at = NOW() "
            "WHERE witness_id = $3::bigint",
            3, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<void>::failure(rc);
        }
        const bool found = std::atoi(PQcmdTuples(res)) > 0;
        PQclear(res);
        return found ? SystemResult<void>::success() : SystemResult<void>::failure(JusticeFlow::ResultCode::NOT_FOUND);
    }

    SystemResult<std::vector<JusticeFlow::Witness>> CaseFacade::getWitnessesByCase(
        PGconn *conn, int case_id)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<std::vector<JusticeFlow::Witness>>::failure(JusticeFlow::ResultCode::DB_ERROR);
        char case_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        const char *values[] = {case_s};
        PGresult *res = PQexecParams(
            conn,
            "SELECT witness_id, case_id, person_cnic, COALESCE(statement_text,''), COALESCE(statement_file_path,''), "
            "COALESCE(recorded_by,0), is_identity_concealed, added_by FROM witnesses WHERE case_id = $1::bigint ORDER BY witness_id",
            1, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<std::vector<JusticeFlow::Witness>>::failure(rc);
        }
        std::vector<JusticeFlow::Witness> out;
        for (int i = 0; i < PQntuples(res); ++i)
        {
            JusticeFlow::Witness w{};
            w.witness_id = std::atoi(PQgetvalue(res, i, 0));
            w.case_id = std::atoi(PQgetvalue(res, i, 1));
            w.person_cnic = PQgetvalue(res, i, 2);
            w.statement_text = PQgetvalue(res, i, 3);
            w.statement_file_path = PQgetvalue(res, i, 4);
            w.recorded_by = std::atoi(PQgetvalue(res, i, 5));
            w.is_identity_concealed = std::strcmp(PQgetvalue(res, i, 6), "t") == 0;
            w.added_by = std::atoi(PQgetvalue(res, i, 7));
            out.push_back(std::move(w));
        }
        PQclear(res);
        return SystemResult<std::vector<JusticeFlow::Witness>>::success(std::move(out));
    }

    SystemResult<int> CaseFacade::addAccused(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int case_id, const char *cnic, JusticeFlow::InvolvementType inv)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK || cnic == nullptr || cnic[0] == '\0')
            return SystemResult<int>::failure(JusticeFlow::ResultCode::INVALID_INPUT);
        char case_s[32], by_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        std::snprintf(by_s, sizeof(by_s), "%d", session.officerId > 0 ? session.officerId : 1);
        const char *values[] = {case_s, cnic, detail::toInvolvementTypeEnum(inv), by_s};
        PGresult *res = PQexecParams(
            conn,
            "INSERT INTO accused (case_id, person_cnic, involvement_type, added_by) "
            "VALUES ($1::bigint, $2, $3::involvement_type_enum, $4::bigint) RETURNING accused_id",
            4, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<int>::failure(rc);
        }
        int id = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        return SystemResult<int>::success(id);
    }

    SystemResult<void> CaseFacade::linkAccusedAssociation(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int accused_id, int associated_id, JusticeFlow::AssociationType atype)
    {
        (void)session;
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<void>::failure(JusticeFlow::ResultCode::DB_ERROR);
        char accused_s[32], assoc_s[32];
        std::snprintf(accused_s, sizeof(accused_s), "%d", accused_id);
        std::snprintf(assoc_s, sizeof(assoc_s), "%d", associated_id);
        const char *values[] = {accused_s, assoc_s, detail::toAssociationTypeEnum(atype)};
        PGresult *res = PQexecParams(
            conn,
            "INSERT INTO accused_associations (accused_id, associated_accused_id, association_type) "
            "VALUES ($1::bigint, $2::bigint, $3::association_type_enum)",
            3, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<void>::failure(rc);
        }
        PQclear(res);
        return SystemResult<void>::success();
    }

    SystemResult<std::vector<JusticeFlow::Accused>> CaseFacade::getAccusedByCase(
        PGconn *conn, int case_id)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<std::vector<JusticeFlow::Accused>>::failure(JusticeFlow::ResultCode::DB_ERROR);
        char case_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        const char *values[] = {case_s};
        PGresult *res = PQexecParams(
            conn,
            "SELECT accused_id, case_id, person_cnic, COALESCE(master_accused_cnic,''), added_by FROM accused "
            "WHERE case_id = $1::bigint ORDER BY accused_id",
            1, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<std::vector<JusticeFlow::Accused>>::failure(rc);
        }
        std::vector<JusticeFlow::Accused> out;
        for (int i = 0; i < PQntuples(res); ++i)
        {
            JusticeFlow::Accused a{};
            a.accused_id = std::atoi(PQgetvalue(res, i, 0));
            a.case_id = std::atoi(PQgetvalue(res, i, 1));
            a.person_cnic = PQgetvalue(res, i, 2);
            a.master_accused_cnic = PQgetvalue(res, i, 3);
            a.added_by = std::atoi(PQgetvalue(res, i, 4));
            out.push_back(std::move(a));
        }
        PQclear(res);
        return SystemResult<std::vector<JusticeFlow::Accused>>::success(std::move(out));
    }

    SystemResult<void> CaseFacade::linkVehicleToCase(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int case_id, int vehicle_id, JusticeFlow::VehicleRole role, const char *notes)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<void>::failure(JusticeFlow::ResultCode::DB_ERROR);
        char case_s[32], vehicle_s[32], by_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        std::snprintf(vehicle_s, sizeof(vehicle_s), "%d", vehicle_id);
        std::snprintf(by_s, sizeof(by_s), "%d", session.officerId > 0 ? session.officerId : 1);
        const char *values[] = {vehicle_s, case_s, detail::toVehicleRoleEnum(role), notes ? notes : "", by_s};
        PGresult *res = PQexecParams(
            conn,
            "INSERT INTO vehicle_cases (vehicle_id, case_id, vehicle_role, condition_notes, added_by) "
            "VALUES ($1::bigint, $2::bigint, $3::vehicle_role_enum, $4, $5::bigint)",
            5, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<void>::failure(rc);
        }
        PQclear(res);
        return SystemResult<void>::success();
    }

    SystemResult<std::vector<JusticeFlow::VehicleCase>> CaseFacade::getVehiclesByCase(
        PGconn *conn, int case_id)
    {
        if (conn == nullptr || PQstatus(conn) != CONNECTION_OK)
            return SystemResult<std::vector<JusticeFlow::VehicleCase>>::failure(JusticeFlow::ResultCode::DB_ERROR);
        char case_s[32];
        std::snprintf(case_s, sizeof(case_s), "%d", case_id);
        const char *values[] = {case_s};
        PGresult *res = PQexecParams(
            conn,
            "SELECT vehicle_case_id, vehicle_id, case_id, condition_notes, added_by FROM vehicle_cases "
            "WHERE case_id = $1::bigint ORDER BY vehicle_case_id",
            1, nullptr, values, nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            JusticeFlow::ResultCode rc = detail::mapPgFailure(conn, res);
            PQclear(res);
            return SystemResult<std::vector<JusticeFlow::VehicleCase>>::failure(rc);
        }
        std::vector<JusticeFlow::VehicleCase> out;
        for (int i = 0; i < PQntuples(res); ++i)
        {
            JusticeFlow::VehicleCase v{};
            v.vehicle_case_id = std::atoi(PQgetvalue(res, i, 0));
            v.vehicle_id = std::atoi(PQgetvalue(res, i, 1));
            v.case_id = std::atoi(PQgetvalue(res, i, 2));
            v.condition_notes = PQgetvalue(res, i, 3);
            v.added_by = std::atoi(PQgetvalue(res, i, 4));
            out.push_back(std::move(v));
        }
        PQclear(res);
        return SystemResult<std::vector<JusticeFlow::VehicleCase>>::success(std::move(out));
    }

    // =============================================================================
    // Section 6 — InvestigationFacade
    // =============================================================================

    SystemResult<std::unique_ptr<subsystem2::Case>> InvestigationFacade::registerFIR(
        const subsystem2::FIRRegistrationRequest &request,
        const JusticeFlow::SessionContext &session)
    {
        return s2_->registerFIR(request, session);
    }

    SystemResult<std::unique_ptr<subsystem2::Evidence>> InvestigationFacade::logAndSecureEvidence(
        int64_t case_id, JusticeFlow::EvidenceType type,
        const std::string &desc, const std::string &file_path,
        const JusticeFlow::SessionContext &session)
    {
        return s2_->logAndSecureEvidence(case_id, type, desc, file_path, session);
    }

    SystemResult<std::unique_ptr<subsystem2::ChargeSheet>> InvestigationFacade::draftChargeSheet(
        int64_t case_id, const JusticeFlow::SessionContext &session)
    {
        return s2_->draftChargeSheet(case_id, session);
    }

    SystemResult<void> InvestigationFacade::submitChargeSheet(
        subsystem2::ChargeSheet *sheet, const JusticeFlow::SessionContext &session)
    {
        if (sheet == nullptr)
        {
            Logger::error("[System][InvestigationFacade] submitChargeSheet: null sheet pointer rejected.");
            return SystemResult<void>::failure(JusticeFlow::ResultCode::INVALID_INPUT);
        }
        return s2_->submitChargeSheet(sheet, session);
    }

    SystemResult<std::unique_ptr<subsystem2::Case>> InvestigationFacade::fetchCase(int64_t case_id)
    {
        return s2_->fetchCase(case_id);
    }

    // =============================================================================
    // Section 7 — PersonnelFacade (delegates to S1)
    // =============================================================================

    SystemResult<JusticeFlow::Officer> PersonnelFacade::getOfficerById(PGconn *conn, int id)
    {
        return s1_->getOfficerById(conn, id);
    }

    SystemResult<JusticeFlow::Officer> PersonnelFacade::getOfficerByCnic(PGconn *conn, const char *cnic)
    {
        return s1_->getOfficerByCnic(conn, cnic);
    }

    SystemResult<std::vector<JusticeFlow::Officer>> PersonnelFacade::getOfficersByStation(
        PGconn *conn, int station_id)
    {
        return s1_->getOfficersByStation(conn, station_id);
    }

    SystemResult<std::vector<JusticeFlow::Officer>> PersonnelFacade::getOfficersByStatus(
        PGconn *conn, int station_id, JusticeFlow::OfficerStatus st)
    {
        return s1_->getOfficersByStatus(conn, station_id, st);
    }

    SystemResult<void> PersonnelFacade::updateOfficerStatus(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int officer_id, JusticeFlow::OfficerStatus st)
    {
        return s1_->updateOfficerStatus(conn, session, officer_id, st);
    }

    SystemResult<int> PersonnelFacade::promoteOfficer(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int officer_id, JusticeFlow::OfficerRank rank,
        const char *belt, const char *type,
        const char *effective, const char *order_date)
    {
        return s1_->promoteOfficer(conn, session, officer_id, rank, belt, type, effective, order_date);
    }

    SystemResult<std::vector<JusticeFlow::OfficerRankHistory>> PersonnelFacade::getOfficerRankHistory(
        PGconn *conn, int officer_id)
    {
        return s1_->getOfficerRankHistory(conn, officer_id);
    }

    SystemResult<int> PersonnelFacade::deployOfficer(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int officer_id, int to_station,
        const char *reason, const char *order_no,
        const char *from_date, const char *until_date)
    {
        return s1_->deployOfficer(conn, session, officer_id, to_station, reason, order_no, from_date, until_date);
    }

    SystemResult<void> PersonnelFacade::endDeployment(
        PGconn *conn, const JusticeFlow::SessionContext &session, int deployment_id)
    {
        return s1_->endDeployment(conn, session, deployment_id);
    }

    SystemResult<std::vector<JusticeFlow::OfficerDeployment>> PersonnelFacade::getOfficerDeployments(
        PGconn *conn, int officer_id, bool active_only)
    {
        return s1_->getOfficerDeployments(conn, officer_id, active_only);
    }

    SystemResult<std::string> PersonnelFacade::generateOfficerReport(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int officer_id, JusticeFlow::ReportType type)
    {
        return s1_->generateOfficerReport(conn, session, officer_id, type);
    }

    // =============================================================================
    // Section 8 — DutyFacade (delegates to S1)
    // =============================================================================

    SystemResult<int> DutyFacade::scheduleDuty(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int officer_id, int station_id, int patrol_route_id,
        JusticeFlow::ShiftType st, const char *duty_date,
        time_t start, time_t end)
    {
        return s1_->scheduleDuty(conn, session, officer_id, station_id, patrol_route_id, st, duty_date, start, end);
    }

    SystemResult<void> DutyFacade::markDutyStart(
        PGconn *conn, const JusticeFlow::SessionContext &session, int duty_id)
    {
        return s1_->markDutyStart(conn, session, duty_id);
    }

    SystemResult<void> DutyFacade::markDutyEnd(
        PGconn *conn, const JusticeFlow::SessionContext &session, int duty_id)
    {
        return s1_->markDutyEnd(conn, session, duty_id);
    }

    SystemResult<void> DutyFacade::updateDutyStatus(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int duty_id, JusticeFlow::DutyStatus st, const char *reason)
    {
        return s1_->updateDutyStatus(conn, session, duty_id, st, reason);
    }

    SystemResult<void> DutyFacade::cancelDuty(
        PGconn *conn, const JusticeFlow::SessionContext &session, int duty_id)
    {
        return s1_->cancelDuty(conn, session, duty_id);
    }

    SystemResult<std::vector<JusticeFlow::DutyRoster>> DutyFacade::getDutyRoster(
        PGconn *conn, int station_id, const char *duty_date)
    {
        return s1_->getDutyRoster(conn, station_id, duty_date);
    }

    SystemResult<std::vector<JusticeFlow::DutyRoster>> DutyFacade::getActiveDuties(
        PGconn *conn, int station_id)
    {
        return s1_->getActiveDuties(conn, station_id);
    }

    SystemResult<std::vector<JusticeFlow::DutyRoster>> DutyFacade::getOfficerDutyHistory(
        PGconn *conn, int officer_id, time_t from, time_t to)
    {
        return s1_->getOfficerDutyHistory(conn, officer_id, from, to);
    }

    SystemResult<int> DutyFacade::createPatrolRoute(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int station_id, const char *beat_code, const char *name, const char *area)
    {
        return s1_->createPatrolRoute(conn, session, station_id, beat_code, name, area);
    }

    SystemResult<void> DutyFacade::deactivatePatrolRoute(
        PGconn *conn, const JusticeFlow::SessionContext &session, int route_id)
    {
        return s1_->deactivatePatrolRoute(conn, session, route_id);
    }

    SystemResult<std::vector<JusticeFlow::PatrolRoute>> DutyFacade::getPatrolRoutesByStation(
        PGconn *conn, int station_id)
    {
        return s1_->getPatrolRoutesByStation(conn, station_id);
    }

    // =============================================================================
    // Section 9 — EnforcementFacade (delegates to S3)
    // =============================================================================

    SystemResult<int> EnforcementFacade::requestWarrant(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int case_id, const char *accused_cnic, JusticeFlow::WarrantType wt,
        const char *magistrate, const char *court,
        const char *valid_until, const char *target_address)
    {
        return s3_->requestWarrant(conn, session, case_id, accused_cnic, wt, magistrate, court, valid_until, target_address);
    }

    SystemResult<void> EnforcementFacade::executeWarrant(
        PGconn *conn, const JusticeFlow::SessionContext &session, int warrant_id)
    {
        return s3_->executeWarrant(conn, session, warrant_id);
    }

    SystemResult<void> EnforcementFacade::cancelWarrant(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int warrant_id, const char *reason)
    {
        return s3_->cancelWarrant(conn, session, warrant_id, reason);
    }

    SystemResult<std::vector<enforcement::WarrantRecord>> EnforcementFacade::getWarrantsByCase(
        PGconn *conn, int case_id)
    {
        return s3_->getWarrantsByCase(conn, case_id);
    }

    SystemResult<std::vector<enforcement::WarrantRecord>> EnforcementFacade::getActiveWarrants(
        PGconn *conn, int station_id)
    {
        return s3_->getActiveWarrants(conn, station_id);
    }

    SystemResult<int> EnforcementFacade::recordArrest(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int case_id, const char *cnic, const char *location, int warrant_id)
    {
        return s3_->recordArrest(conn, session, case_id, cnic, location, warrant_id);
    }

    SystemResult<void> EnforcementFacade::updateCustodyStatus(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int arrest_id, JusticeFlow::CustodyStatus st, const char *reason)
    {
        return s3_->updateCustodyStatus(conn, session, arrest_id, st, reason);
    }

    SystemResult<void> EnforcementFacade::markArrestAsDisputed(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int arrest_id, const char *reason)
    {
        return s3_->markArrestAsDisputed(conn, session, arrest_id, reason);
    }

    SystemResult<std::vector<enforcement::ArrestRecord>> EnforcementFacade::getArrestsByCase(
        PGconn *conn, int case_id)
    {
        return s3_->getArrestsByCase(conn, case_id);
    }

    SystemResult<int> EnforcementFacade::recordBail(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int arrest_id, JusticeFlow::BailType bt, uint64_t amount,
        const char *court, const char *magistrate, const char *valid_until,
        const char *surety_name, const char *surety_cnic, const char *surety_contact)
    {
        return s3_->recordBail(conn, session, arrest_id, bt, amount, court, magistrate, valid_until, surety_name, surety_cnic, surety_contact);
    }

    SystemResult<void> EnforcementFacade::revokeBail(
        PGconn *conn, const JusticeFlow::SessionContext &session,
        int bail_id, const char *reason)
    {
        return s3_->revokeBail(conn, session, bail_id, reason);
    }

    SystemResult<enforcement::BailRecord> EnforcementFacade::getBailByArrest(
        PGconn *conn, int arrest_id)
    {
        return s3_->getBailByArrest(conn, arrest_id);
    }

    // =============================================================================
    // Section 10 — AuditFacade (delegates to S3)
    // =============================================================================

    SystemResult<std::vector<audit::AuditRecord>> AuditFacade::getAuditChangeHistory(int case_id)
    {
        return s3_->getAuditChangeHistory(case_id);
    }

    SystemResult<std::vector<audit::AuditRecord>> AuditFacade::getAuditOfficerActions(
        int officer_id, time_t from, time_t to)
    {
        return s3_->getAuditOfficerActions(officer_id, from, to);
    }

    SystemResult<std::vector<audit::AuditRecord>> AuditFacade::getAuditTableChanges(
        const char *table_name, int record_id)
    {
        return s3_->getAuditTableChanges(table_name, record_id);
    }

    SystemResult<std::vector<audit::AuditRecord>> AuditFacade::auditQueryByTimeWindow(
        time_t from, time_t to)
    {
        return s3_->auditQueryByTimeWindow(from, to);
    }

    SystemResult<std::vector<audit::AuditRecord>> AuditFacade::detectSuspiciousActivity(
        int station_id)
    {
        return s3_->detectSuspiciousActivity(station_id);
    }

    // =============================================================================
    // Section 11 — ForensicFacade (delegates to S3)
    // =============================================================================

    SystemResult<int> ForensicFacade::createForensicRequest(
        const char *token, int case_id,
        const char *purpose, const char *purpose_desc,
        const char *lab_name, const char *examiner_name)
    {
        return s3_->createForensicRequest(token, case_id, purpose, purpose_desc, lab_name, examiner_name);
    }

    SystemResult<void> ForensicFacade::linkEvidence(
        const char *token, int request_id, int evidence_id, const char *notes)
    {
        return s3_->linkEvidence(token, request_id, evidence_id, notes);
    }

    SystemResult<void> ForensicFacade::recordLabReceipt(
        const char *token, int request_id, const char *received_date)
    {
        return s3_->recordLabReceipt(token, request_id, received_date);
    }

    SystemResult<void> ForensicFacade::recordExaminationStart(const char *token, int request_id)
    {
        return s3_->recordExaminationStart(token, request_id);
    }

    SystemResult<void> ForensicFacade::recordFindings(
        const char *token, int request_id,
        const char *findings, const char *report_file_path, const char *delivery_date)
    {
        return s3_->recordFindings(token, request_id, findings, report_file_path, delivery_date);
    }

    SystemResult<void> ForensicFacade::recordAmendment(
        const char *token, int request_id, const char *amended_findings)
    {
        return s3_->recordAmendment(token, request_id, amended_findings);
    }

    SystemResult<void> ForensicFacade::contestReport(
        const char *token, int request_id, const char *reason)
    {
        return s3_->contestReport(token, request_id, reason);
    }

    SystemResult<std::vector<forensic::ForensicRecord>> ForensicFacade::getForensicRequestsByCase(
        const char *token, int case_id)
    {
        return s3_->getForensicRequestsByCase(token, case_id);
    }

    SystemResult<std::vector<forensic::ForensicRecord>> ForensicFacade::getPendingForensicRequests(
        const char *token, int station_id)
    {
        return s3_->getPendingForensicRequests(token, station_id);
    }

    SystemResult<std::vector<forensic::EvidenceRef>> ForensicFacade::getEvidenceByForensicRequest(
        const char *token, int request_id)
    {
        return s3_->getEvidenceByForensicRequest(token, request_id);
    }

} // namespace system_layer
