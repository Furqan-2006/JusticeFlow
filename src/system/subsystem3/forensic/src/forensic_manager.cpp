// ============================================================================
// forensic_manager.cpp  —  ForensicManager implementation
// ============================================================================
//
// Every write operation follows the same chain (short-circuit on any failure):
//   1. AuthManager::validateToken()   — session must be valid, loads JusticeFlow::SessionContext
//   2. AuthManager::isDutyActive()    — officer must be on duty
//   3. AuthManager::validateRank()    — only where INSPECTOR+ is required
//   4. _validateTransition()          — state machine enforcement (no DB write if illegal)
//   5. Domain validation              — date ordering, non-empty fields, known enums
//   6. ForensicRepository call        — the actual DB write (triggers fire here)
//
// Connection is obtained from IpcManager — this manager owns no PGconn*.
// ForensicRepository receives it per-call.
//
// EVIDENCE STATUS IS NEVER UPDATED HERE.
// Trigger 1 (Forensic_Request_Evidence INSERT) → SENT_TO_LAB
// Trigger 2 (Forensic_Lab_Requests UPDATE to REPORT_DELIVERED) → RETURNED_FROM_LAB
// ============================================================================

#include "../include/forensic_manager.h"
#include "../include/forensic_repository.h"
#include "../../../shr_infra/auth/include/auth_module.h"
#include "os_layer/os_layer.h"
#include "common/logger.h"
#include <cstring>
#include <cstdio>
#include <ctime>

using namespace JusticeFlow;

namespace forensic
{

    // ============================================================================
    // _validateTransition  —  state machine enforcement
    // Legal edges only. No skipping. CONTESTED is terminal.
    // ============================================================================
    bool ForensicManager::_validateTransition(const char *from, const char *to)
    {
        // Each entry: { from_state, to_state }
        static const char *kEdges[][2] = {
            {"REQUESTED", "RECEIVED_BY_LAB"},
            {"RECEIVED_BY_LAB", "UNDER_EXAMINATION"},
            {"UNDER_EXAMINATION", "REPORT_READY"},
            {"REPORT_READY", "REPORT_DELIVERED"},
            {"REPORT_DELIVERED", "CONTESTED"},
        };
        static const int kEdgeCount = 5;

        for (int i = 0; i < kEdgeCount; ++i)
        {
            if (std::strcmp(kEdges[i][0], from) == 0 &&
                std::strcmp(kEdges[i][1], to) == 0)
                return true;
        }

        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "forensic_manager: Illegal transition %s → %s", from, to);
        Logger::debug(msg);
        return false;
    }

    // ============================================================================
    // _knownPurpose  —  validate examination_purpose against known enum values
    // ============================================================================
    bool ForensicManager::_knownPurpose(const char *purpose)
    {
        static const char *kPurposes[] = {
            "DNA_ANALYSIS", "FINGERPRINT_ANALYSIS", "TOXICOLOGY",
            "BALLISTICS", "DIGITAL_FORENSICS", "DOCUMENT_EXAMINATION", "OTHER"};
        for (const char *p : kPurposes)
        {
            if (std::strcmp(purpose, p) == 0)
                return true;
        }
        return false;
    }

    // ============================================================================
    // _parseDate  —  "YYYY-MM-DD" → time_t (UTC midnight), -1 on failure
    // ============================================================================
    time_t ForensicManager::_parseDate(const char *date_str)
    {
        if (!date_str || date_str[0] == '\0')
            return static_cast<time_t>(-1);
        struct tm tm_val{};
        if (!strptime(date_str, "%Y-%m-%d", &tm_val))
            return static_cast<time_t>(-1);
        tm_val.tm_hour = 0;
        tm_val.tm_min = 0;
        tm_val.tm_sec = 0;
        return timegm(&tm_val);
    }

    // ============================================================================
    // Shared macro-style helper: obtain conn, validate token, load session context.
    // Written as inline logic rather than a macro so the compiler sees it clearly.
    // ============================================================================

    /**
     * ==============================================================================
     * TODO: PERMANENT ARCHITECTURE FIX (Post-Presentation)
     * ==============================================================================
     *
     * ISSUE:
     * Currently, we return a raw `PGconn*` to the caller and rely on the caller to
     * manually call lockDb() and unlockDb(). This is an anti-pattern. If a caller
     * forgets to unlock, or if an exception is thrown while the lock is held, the
     * entire application will deadlock. Furthermore, having only ONE connection
     * locked by a mutex means only one user can access the DB at a time (bottleneck).
     *
     * PERMANENT FIX PLAN:
     *
     * 1. UPGRADE TO C++ std::mutex (RAII)
     *    Replace `pthread_mutex_t` in UnixSocket with `std::mutex db_mutex`.
     *    This allows the use of `std::lock_guard<std::mutex> lock(db_mutex);`
     *    which automatically unlocks when it goes out of scope, guaranteeing we
     *    never leave a mutex locked by accident.
     *
     * 2. REMOVE getConnection()
     *    Stop handing out raw pointers. Instead, callers should ONLY use the safe
     *    `UnixSocket::execute()` method, which handles its own locking internally.
     *    If custom PQ functions are needed, implement a callback pattern:
     *    `void withConnection(std::function<void(PGconn*)> func)` so the socket
     *    manages the lock lifecycle.
     *
     * 3. IMPLEMENT A CONNECTION POOL (For Production Scale)
     *    Instead of a single PGconn, create a class `ConnectionPool`.
     *    - Initialize it with ~10 PGconn objects.
     *    - Callers request a connection: `PGconn* conn = pool.acquire();`
     *    - Callers execute queries (NO mutex locking needed during query execution!).
     *    - Callers return it: `pool.release(conn);`
     *    This completely removes the bottleneck and allows true multi-threading.
     * ==============================================================================
     */

    // Returns DB_ERROR if IpcManager has no connection.
    // Returns SESSION_EXPIRED if token is invalid.
    // Populates ctx on success.
    static ResultCode _authAndConn(const char *token,
                                   PGconn *&out_conn,
                                   JusticeFlow::SessionContext &out_ctx)
    {
        // 1. ACQUIRE THE LOCK BEFORE GETTING THE CONNECTION
        ipc::IpcManager::getInstance().lockDb();

        out_conn = ipc::IpcManager::getInstance().getConnection();
        if (!out_conn)
        {
            Logger::error("forensic_manager: No DB connection available");
            // MUST UNLOCK BEFORE RETURNING ON ERROR
            ipc::IpcManager::getInstance().unlockDb();
            return ResultCode::DB_ERROR;
        }

        ResultCode auth_res = auth::AuthManager::getInstance().validateToken(token, out_ctx);

        if (auth_res != ResultCode::OK)
        {
            // MUST UNLOCK BEFORE RETURNING ON ERROR
            ipc::IpcManager::getInstance().unlockDb();
            out_conn = nullptr;
        }

        // ON SUCCESS: We return the connection, but LEAVE THE DB LOCKED.
        // The caller MUST call unlockDb() when they are finished with out_conn!
        return auth_res;
    }

    // ============================================================================
    // createForensicRequest
    // Chain: token → duty → rank(INSPECTOR) → case state → purpose → non-empty → INSERT
    // ============================================================================
    ResultCode ForensicManager::createForensicRequest(
        const char *token,
        int case_id,
        const char *examination_purpose,
        const char *purpose_description,
        const char *lab_name,
        const char *examiner_name,
        int &out_request_id)
    {
        // --- Step 1: Validate token, load session ---
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
        {
            Logger::debug("forensic_manager: createForensicRequest — session invalid");
            return rc;
        }

        // --- Step 2: Officer must be on active duty ---
        bool on_duty = false;
        rc = auth::AuthManager::getInstance().isDutyActive(ctx.officerId, on_duty);
        if (rc != ResultCode::OK || !on_duty)
        {
            Logger::debug("forensic_manager: createForensicRequest — officer not on duty");
            return ResultCode::DUTY_INACTIVE;
        }

        // --- Step 3: INSPECTOR+ rank required to authorise forensic request ---
        bool rank_ok = false;
        rc = auth::AuthManager::getInstance().validateRank(ctx, static_cast<int>(OfficerRank::INSPECTOR));
        if (rc != ResultCode::OK || !rank_ok)
        {
            Logger::debug("forensic_manager: createForensicRequest — rank insufficient");
            return ResultCode::RANK_INSUFFICIENT;
        }

        // --- Step 4: Case must exist and be in an investigative state ---
        {
            char p0[16];
            std::snprintf(p0, sizeof(p0), "%d", case_id);
            const char *paramValues[1] = {p0};
            PGresult *res = PQexecParams(conn,
                                         "SELECT case_status FROM public.Cases WHERE case_id = $1::int;",
                                         1, nullptr, paramValues, nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
            {
                Logger::debug("forensic_manager: createForensicRequest — case not found");
                PQclear(res);
                return ResultCode::NOT_FOUND;
            }

            const char *status = PQgetvalue(res, 0, 0);
            bool ok = (std::strcmp(status, "REGISTERED") == 0 ||
                       std::strcmp(status, "UNDER_INVESTIGATION") == 0 ||
                       std::strcmp(status, "EVIDENCE_COLLECTED") == 0);
            PQclear(res);

            if (!ok)
            {
                Logger::debug("forensic_manager: createForensicRequest — case in wrong state");
                return ResultCode::INVALID_STATE;
            }
        }

        // --- Step 5: examination_purpose must be a known value ---
        if (!examination_purpose || !_knownPurpose(examination_purpose))
        {
            Logger::debug("forensic_manager: createForensicRequest — unknown purpose");
            return ResultCode::INVALID_INPUT;
        }

        // --- Step 6: lab_name and examiner_name must not be empty ---
        if (!lab_name || lab_name[0] == '\0' ||
            !examiner_name || examiner_name[0] == '\0' ||
            !purpose_description || purpose_description[0] == '\0')
        {
            Logger::debug("forensic_manager: createForensicRequest — empty required field");
            return ResultCode::INVALID_INPUT;
        }

        // --- Step 7: Delegate INSERT to repository (audit trigger fires here) ---
        rc = ForensicRepository::insertRequest(conn, case_id,
                                               examination_purpose, purpose_description,
                                               lab_name, examiner_name,
                                               ctx.officerId, out_request_id);
        if (rc != ResultCode::OK)
        {
            Logger::error("forensic_manager: createForensicRequest — repository INSERT failed");
        }
        return rc;
    }

    // ============================================================================
    // linkEvidence
    // Chain: token → duty → request state (REQUESTED/RECEIVED_BY_LAB) →
    //        evidence not deleted → evidence status → same case → INSERT
    // DB Trigger 1 fires: evidence_status → SENT_TO_LAB
    // ============================================================================
    ResultCode ForensicManager::linkEvidence(
        const char *token,
        int request_id,
        int evidence_id,
        const char *notes)
    {
        // --- Step 1: token + conn ---
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        // --- Step 2: duty ---
        bool on_duty = false;
        rc = auth::AuthManager::getInstance().isDutyActive(ctx.officerId, on_duty);
        if (rc != ResultCode::OK || !on_duty)
            return ResultCode::DUTY_INACTIVE;

        // --- Step 3: Request must be REQUESTED or RECEIVED_BY_LAB ---
        //   (evidence cannot be added once examination has started)
        char current_status[24]{};
        time_t receipt_epoch = 0;
        rc = ForensicRepository::fetchCurrentStatus(conn, request_id,
                                                    current_status, receipt_epoch);
        if (rc != ResultCode::OK)
            return rc; // NOT_FOUND or DB_ERROR

        if (std::strcmp(current_status, "REQUESTED") != 0 &&
            std::strcmp(current_status, "RECEIVED_BY_LAB") != 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "forensic_manager: linkEvidence — request is %s, cannot add evidence",
                          current_status);
            Logger::debug(msg);
            return ResultCode::INVALID_STATE;
        }

        // --- Step 4: Evidence must not be soft-deleted and must have a linkable status ---
        {
            char p0[16];
            std::snprintf(p0, sizeof(p0), "%d", evidence_id);
            const char *paramValues[1] = {p0};
            PGresult *res = PQexecParams(conn,
                                         "SELECT is_deleted, evidence_status, case_id "
                                         "FROM public.Evidence WHERE evidence_id = $1::int;",
                                         1, nullptr, paramValues, nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0)
            {
                Logger::debug("forensic_manager: linkEvidence — evidence not found");
                PQclear(res);
                return ResultCode::NOT_FOUND;
            }

            const char *is_deleted = PQgetvalue(res, 0, 0);
            const char *evid_status = PQgetvalue(res, 0, 1);
            int evidence_case = std::atoi(PQgetvalue(res, 0, 2));
            PQclear(res);

            // Soft-deleted evidence cannot be submitted
            if (std::strcmp(is_deleted, "t") == 0 || std::strcmp(is_deleted, "true") == 0)
            {
                Logger::debug("forensic_manager: linkEvidence — evidence is soft-deleted");
                return ResultCode::INVALID_STATE;
            }

            // Only evidence in RECEIVED, SEALED, or RETURNED_FROM_LAB can be linked
            // (SENT_TO_LAB means already in a lab; PRODUCED_IN_COURT and DISPOSED are terminal)
            bool status_ok = (std::strcmp(evid_status, "RECEIVED") == 0 ||
                              std::strcmp(evid_status, "SEALED") == 0 ||
                              std::strcmp(evid_status, "RETURNED_FROM_LAB") == 0);
            if (!status_ok)
            {
                char msg[128];
                std::snprintf(msg, sizeof(msg),
                              "forensic_manager: linkEvidence — evidence status %s not linkable",
                              evid_status);
                Logger::debug(msg);
                return ResultCode::INVALID_STATE;
            }

            // --- Step 5: Evidence must belong to the same case as the request ---
            {
                char p1[16];
                std::snprintf(p1, sizeof(p1), "%d", request_id);
                const char *paramValues2[1] = {p1};
                PGresult *r2 = PQexecParams(conn,
                                            "SELECT case_id FROM public.Forensic_Lab_Requests WHERE request_id = $1::int;",
                                            1, nullptr, paramValues2, nullptr, nullptr, 0);

                if (PQresultStatus(r2) != PGRES_TUPLES_OK || PQntuples(r2) == 0)
                {
                    PQclear(r2);
                    return ResultCode::NOT_FOUND;
                }
                int request_case = std::atoi(PQgetvalue(r2, 0, 0));
                PQclear(r2);

                if (evidence_case != request_case)
                {
                    Logger::debug("forensic_manager: linkEvidence — chain-of-custody violation: "
                                  "evidence belongs to different case");
                    return ResultCode::INVALID_INPUT;
                }
            }
        }

        // --- Step 6: INSERT link — DB Trigger 1 fires here (SENT_TO_LAB) ---
        rc = ForensicRepository::insertEvidenceLink(conn, request_id, evidence_id,
                                                    notes ? notes : "");
        if (rc != ResultCode::OK)
        {
            Logger::error("forensic_manager: linkEvidence — repository INSERT failed");
        }
        return rc;
    }

    // ============================================================================
    // recordLabReceipt
    // Transition: REQUESTED → RECEIVED_BY_LAB
    // Also stores received_date (must not be in the future).
    // ============================================================================
    ResultCode ForensicManager::recordLabReceipt(
        const char *token,
        int request_id,
        const char *received_date)
    {
        // --- token + conn ---
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        // --- duty ---
        bool on_duty = false;
        rc = auth::AuthManager::getInstance().isDutyActive(ctx.officerId, on_duty);
        if (rc != ResultCode::OK || !on_duty)
            return ResultCode::DUTY_INACTIVE;

        // --- received_date must not be empty ---
        if (!received_date || received_date[0] == '\0')
        {
            Logger::debug("forensic_manager: recordLabReceipt — received_date required");
            return ResultCode::INVALID_INPUT;
        }

        // --- received_date must not be in the future ---
        time_t recv_t = _parseDate(received_date);
        if (recv_t == static_cast<time_t>(-1))
        {
            Logger::debug("forensic_manager: recordLabReceipt — invalid date format");
            return ResultCode::INVALID_INPUT;
        }
        if (recv_t > std::time(nullptr))
        {
            Logger::debug("forensic_manager: recordLabReceipt — received_date is in the future");
            return ResultCode::INVALID_INPUT;
        }

        // --- state check: must be REQUESTED ---
        char current[24]{};
        time_t dummy = 0;
        rc = ForensicRepository::fetchCurrentStatus(conn, request_id, current, dummy);
        if (rc != ResultCode::OK)
            return rc;

        if (!_validateTransition(current, "RECEIVED_BY_LAB"))
            return ResultCode::INVALID_STATE;

        // --- Write received date, then transition status (two UPDATEs, same connection) ---
        rc = ForensicRepository::updateReceivedDate(conn, request_id, received_date);
        if (rc != ResultCode::OK)
            return rc;

        rc = ForensicRepository::updateStatus(conn, request_id, "RECEIVED_BY_LAB");
        if (rc != ResultCode::OK)
        {
            Logger::error("forensic_manager: recordLabReceipt — status update failed");
        }
        return rc;
    }

    // ============================================================================
    // recordExaminationStart
    // Transition: RECEIVED_BY_LAB → UNDER_EXAMINATION
    // ============================================================================
    ResultCode ForensicManager::recordExaminationStart(
        const char *token,
        int request_id)
    {
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        bool on_duty = false;
        rc = auth::AuthManager::getInstance().isDutyActive(ctx.officerId, on_duty);
        if (rc != ResultCode::OK || !on_duty)
            return ResultCode::DUTY_INACTIVE;

        // --- state check ---
        char current[24]{};
        time_t dummy = 0;
        rc = ForensicRepository::fetchCurrentStatus(conn, request_id, current, dummy);
        if (rc != ResultCode::OK)
            return rc;

        if (!_validateTransition(current, "UNDER_EXAMINATION"))
            return ResultCode::INVALID_STATE;

        // --- Write examination start timestamp ---
        rc = ForensicRepository::updateExaminationStartDate(conn, request_id);
        if (rc != ResultCode::OK)
            return rc;

        rc = ForensicRepository::updateStatus(conn, request_id, "UNDER_EXAMINATION");
        if (rc != ResultCode::OK)
        {
            Logger::error("forensic_manager: recordExaminationStart — status update failed");
        }
        return rc;
    }

    // ============================================================================
    // recordFindings
    // Two-step transition atomically: UNDER_EXAMINATION → REPORT_READY → REPORT_DELIVERED
    // DB Trigger 2 fires on REPORT_DELIVERED → RETURNED_FROM_LAB for all linked evidence.
    // ============================================================================
    ResultCode ForensicManager::recordFindings(
        const char *token,
        int request_id,
        const char *findings,
        const char *report_file_path,
        const char *delivery_date)
    {
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        bool on_duty = false;
        rc = auth::AuthManager::getInstance().isDutyActive(ctx.officerId, on_duty);
        if (rc != ResultCode::OK || !on_duty)
            return ResultCode::DUTY_INACTIVE;

        // --- findings must not be empty ---
        if (!findings || findings[0] == '\0')
        {
            Logger::debug("forensic_manager: recordFindings — findings cannot be empty");
            return ResultCode::INVALID_INPUT;
        }

        // --- report_file_path must not be empty ---
        if (!report_file_path || report_file_path[0] == '\0')
        {
            Logger::debug("forensic_manager: recordFindings — report_file_path required");
            return ResultCode::INVALID_INPUT;
        }

        // --- delivery_date must be valid ---
        time_t delivery_t = _parseDate(delivery_date);
        if (delivery_t == static_cast<time_t>(-1))
        {
            Logger::debug("forensic_manager: recordFindings — invalid delivery_date format");
            return ResultCode::INVALID_INPUT;
        }

        // --- state check: must be UNDER_EXAMINATION ---
        char current[24]{};
        time_t receipt_epoch = 0;
        rc = ForensicRepository::fetchCurrentStatus(conn, request_id, current, receipt_epoch);
        if (rc != ResultCode::OK)
            return rc;

        if (!_validateTransition(current, "REPORT_READY"))
            return ResultCode::INVALID_STATE;

        // --- delivery_date must be >= received_by_lab_date ---
        // This prevents recording delivery before the lab even received the evidence.
        if (receipt_epoch > 0 && delivery_t < static_cast<time_t>(receipt_epoch))
        {
            Logger::debug("forensic_manager: recordFindings — delivery_date before receipt_date");
            return ResultCode::INVALID_INPUT;
        }

        // --- Write findings first ---
        rc = ForensicRepository::updateFindings(conn, request_id,
                                                findings, report_file_path, delivery_date);
        if (rc != ResultCode::OK)
            return rc;

        // --- Advance to REPORT_READY ---
        rc = ForensicRepository::updateStatus(conn, request_id, "REPORT_READY");
        if (rc != ResultCode::OK)
            return rc;

        // --- Immediately advance to REPORT_DELIVERED ---
        // DB Trigger 2 fires here:
        //   → UPDATE Evidence SET evidence_status = 'RETURNED_FROM_LAB'
        //      WHERE evidence_id IN (SELECT evidence_id FROM Forensic_Request_Evidence
        //                            WHERE request_id = request_id)
        // We never call that UPDATE ourselves.
        rc = ForensicRepository::updateStatus(conn, request_id, "REPORT_DELIVERED");
        if (rc != ResultCode::OK)
        {
            Logger::error("forensic_manager: recordFindings — REPORT_DELIVERED status update failed");
        }
        return rc;
    }

    // ============================================================================
    // recordAmendment
    // Corrects findings text on a delivered/contested report — status unchanged.
    // INSPECTOR+ required.
    // ============================================================================
    ResultCode ForensicManager::recordAmendment(
        const char *token,
        int request_id,
        const char *amended_findings)
    {
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        // --- duty ---
        bool on_duty = false;
        rc = auth::AuthManager::getInstance().isDutyActive(ctx.officerId, on_duty);
        if (rc != ResultCode::OK || !on_duty)
            return ResultCode::DUTY_INACTIVE;

        // --- INSPECTOR+ rank required ---
        bool rank_ok = false;
        rc = auth::AuthManager::getInstance().validateRank(ctx, static_cast<int>(OfficerRank::INSPECTOR));
        if (rc != ResultCode::OK || !rank_ok)
        {
            Logger::debug("forensic_manager: recordAmendment — INSPECTOR+ required");
            return ResultCode::RANK_INSUFFICIENT;
        }

        // --- amended_findings must not be empty ---
        if (!amended_findings || amended_findings[0] == '\0')
        {
            Logger::debug("forensic_manager: recordAmendment — amended_findings cannot be empty");
            return ResultCode::INVALID_INPUT;
        }

        // --- Status must be REPORT_DELIVERED or CONTESTED (cannot amend pre-delivery) ---
        char current[24]{};
        time_t dummy = 0;
        rc = ForensicRepository::fetchCurrentStatus(conn, request_id, current, dummy);
        if (rc != ResultCode::OK)
            return rc;

        if (std::strcmp(current, "REPORT_DELIVERED") != 0 &&
            std::strcmp(current, "CONTESTED") != 0)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "forensic_manager: recordAmendment — cannot amend in state %s", current);
            Logger::debug(msg);
            return ResultCode::INVALID_STATE;
        }

        // --- Update findings text only (status unchanged) ---
        rc = ForensicRepository::updateAmendment(conn, request_id, amended_findings);
        if (rc != ResultCode::OK)
        {
            Logger::error("forensic_manager: recordAmendment — repository update failed");
        }
        return rc;
    }

    // ============================================================================
    // contestReport
    // Transition: REPORT_DELIVERED → CONTESTED (terminal state)
    // ============================================================================
    ResultCode ForensicManager::contestReport(
        const char *token,
        int request_id,
        const char *contest_reason)
    {
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        bool on_duty = false;
        rc = auth::AuthManager::getInstance().isDutyActive(ctx.officerId, on_duty);
        if (rc != ResultCode::OK || !on_duty)
            return ResultCode::DUTY_INACTIVE;

        // --- contest_reason must not be empty ---
        if (!contest_reason || contest_reason[0] == '\0')
        {
            Logger::debug("forensic_manager: contestReport — contest_reason required");
            return ResultCode::INVALID_INPUT;
        }

        // --- state check: must be REPORT_DELIVERED ---
        char current[24]{};
        time_t dummy = 0;
        rc = ForensicRepository::fetchCurrentStatus(conn, request_id, current, dummy);
        if (rc != ResultCode::OK)
            return rc;

        if (!_validateTransition(current, "CONTESTED"))
            return ResultCode::INVALID_STATE;

        // --- Write contestation fields ---
        rc = ForensicRepository::updateContest(conn, request_id,
                                               contest_reason, ctx.officerId);
        if (rc != ResultCode::OK)
            return rc;

        // --- Advance status to CONTESTED ---
        rc = ForensicRepository::updateStatus(conn, request_id, "CONTESTED");
        if (rc != ResultCode::OK)
        {
            Logger::error("forensic_manager: contestReport — status update failed");
        }
        return rc;
    }

    // ============================================================================
    // QUERY OPERATIONS
    // ============================================================================

    ResultCode ForensicManager::getRequestsByCase(
        const char *token,
        int case_id,
        std::vector<ForensicRecord> &out)
    {
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        return ForensicRepository::selectByCase(conn, case_id, out);
    }

    ResultCode ForensicManager::getPendingRequests(
        const char *token,
        int station_id,
        std::vector<ForensicRecord> &out)
    {
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        return ForensicRepository::selectPending(conn, station_id, out);
    }

    ResultCode ForensicManager::getEvidenceByRequest(
        const char *token,
        int request_id,
        std::vector<EvidenceRef> &out)
    {
        PGconn *conn = nullptr;
        JusticeFlow::SessionContext ctx{};
        ResultCode rc = _authAndConn(token, conn, ctx);
        if (rc != ResultCode::OK)
            return rc;

        return ForensicRepository::selectEvidenceByRequest(conn, request_id, out);
    }

} // namespace forensic