-- =============================================================================
-- audit_triggers.sql
-- Subsystem 3 — Audit Infrastructure
--
-- BUILD ORDER: Execute this file BEFORE any other S3 module can write to the
-- database. Every DML on the 8 audited tables fires these triggers. If the
-- triggers do not exist, audit entries will not be written and the audit log
-- will be incomplete and inadmissible.
--
-- What this file defines:
--   1. audit.Audit_Log table schema
--   2. audit.log_change()         — SECURITY DEFINER write trigger
--   3. audit.enforce_immutability() — immutability + soft-delete enforcement
--   4. Trigger installations on all 8 audited tables
--
-- Systems Programming Analogues (documented per architecture spec)
-- ----------------------------------------------------------------
-- SECURITY DEFINER     → setuid bit on an executable.
--                         The function runs with the privileges of its owner
--                         (audit_writer role), not the caller's role.
--                         This prevents the application from bypassing audit
--                         by executing as a low-privilege role.
--
-- pg_backend_pid()     → getpid() system call.
--                         Returns the OS process ID of the PostgreSQL backend
--                         handling this connection. Used to correlate bulk
--                         operations from a single process.
--
-- inet_client_addr()   → getpeername() on a socket.
--                         Returns the IP address of the client that opened
--                         this database connection. Captured at write time.
--
-- app.current_officer_id → Process environment variable.
--                         The worker sets this via SET LOCAL before every
--                         privileged operation. The trigger reads it to stamp
--                         every audit entry with the correct officer identity.
--                         Using SET LOCAL (not SET) means the variable resets
--                         automatically at transaction end — no stale identity.
--
-- app.current_belt_number → Companion environment variable for the belt number.
--                         Same SET LOCAL pattern.
-- =============================================================================


-- =============================================================================
-- SCHEMA
-- =============================================================================

CREATE SCHEMA IF NOT EXISTS audit;


-- =============================================================================
-- TABLE: audit.Audit_Log
--
-- Append-only. The enforce_immutability trigger blocks all UPDATE and DELETE.
-- Rows are never physically deleted — the log is the permanent chain of custody.
-- =============================================================================

CREATE TABLE IF NOT EXISTS audit.Audit_Log (
    audit_log_id  SERIAL          PRIMARY KEY,
    table_name    VARCHAR(32)     NOT NULL,
    record_pk     INTEGER         NOT NULL,
    operation     VARCHAR(16)     NOT NULL CHECK (operation IN ('INSERT','UPDATE','DELETE')),
    officer_id    INTEGER         NOT NULL DEFAULT 0,   -- 0 = system/trigger
    belt_number   VARCHAR(32),
    backend_pid   INTEGER         NOT NULL,
    client_addr   TEXT,                                  -- NULL for local socket
    old_values    JSONB,                                 -- NULL for INSERT
    new_values    JSONB,                                 -- NULL for DELETE
    changed_at    TIMESTAMPTZ     NOT NULL DEFAULT NOW()
);

-- Index: dashboards query by case/officer/time frequently
CREATE INDEX IF NOT EXISTS idx_audit_log_officer_time
    ON audit.Audit_Log (officer_id, changed_at DESC);

CREATE INDEX IF NOT EXISTS idx_audit_log_table_record
    ON audit.Audit_Log (table_name, record_pk);

CREATE INDEX IF NOT EXISTS idx_audit_log_changed_at
    ON audit.Audit_Log (changed_at DESC);

CREATE INDEX IF NOT EXISTS idx_audit_log_backend_pid
    ON audit.Audit_Log (backend_pid, changed_at);


-- =============================================================================
-- TRIGGER FUNCTION 1: audit.log_change()
--
-- Fires AFTER INSERT, UPDATE, or DELETE on each of the 8 audited tables.
-- Writes one row to audit.Audit_Log per DML statement row.
--
-- SECURITY DEFINER: Executes as the owner of this function (audit_writer role)
-- regardless of which role triggered the DML. This guarantees:
--   a) Application code cannot suppress audit entries by privilege restriction.
--   b) audit_writer can INSERT into audit.Audit_Log even when the application
--      role has no direct INSERT privilege on that table.
--
-- Session Variables (set by worker.cpp before each operation):
--   app.current_officer_id   — the authenticated officer's officer_id
--   app.current_belt_number  — the authenticated officer's belt number
--
-- If the session variable is not set (e.g., direct DB tooling, migration
-- scripts, or DB-level triggers), officer_id defaults to 0 and belt_number
-- defaults to 'SYSTEM'. This is logged visibly so it can be audited.
-- =============================================================================

CREATE OR REPLACE FUNCTION audit.log_change()
RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, audit, subsystem1
AS $$
DECLARE
    v_officer_id    INTEGER;
    v_belt_number   VARCHAR(32);
    v_record_pk     INTEGER;
    v_old_values    JSONB;
    v_new_values    JSONB;
BEGIN
    -- -----------------------------------------------------------------------
    -- Read officer identity from session variables.
    -- SET LOCAL (set by worker.cpp) resets at transaction end automatically.
    -- Analogous to reading a process environment variable.
    -- -----------------------------------------------------------------------
    BEGIN
        v_officer_id  := current_setting('app.current_officer_id')::INTEGER;
        v_belt_number := current_setting('app.current_belt_number');
    EXCEPTION WHEN OTHERS THEN
        v_officer_id  := 0;
        v_belt_number := 'SYSTEM';
    END;

    -- -----------------------------------------------------------------------
    -- Snapshot OLD and NEW row state as JSONB.
    -- row_to_json() produces the full column snapshot atomically within the
    -- same transaction, guaranteeing the captured state matches the actual
    -- committed change.
    -- -----------------------------------------------------------------------
    IF TG_OP = 'DELETE' THEN
        v_old_values := row_to_json(OLD)::JSONB;
        v_new_values := NULL;
    ELSIF TG_OP = 'INSERT' THEN
        v_old_values := NULL;
        v_new_values := row_to_json(NEW)::JSONB;
    ELSE -- UPDATE
        v_old_values := row_to_json(OLD)::JSONB;
        v_new_values := row_to_json(NEW)::JSONB;
    END IF;

    -- -----------------------------------------------------------------------
    -- Extract the primary key of the row being modified.
    -- Each audited table has a different PK column name.
    -- -----------------------------------------------------------------------
    v_record_pk := CASE TG_TABLE_NAME
        WHEN 'cases'         THEN COALESCE(
            (row_to_json(NEW)->>'case_id'),
            (row_to_json(OLD)->>'case_id'))::INTEGER
        WHEN 'evidence'      THEN COALESCE(
            (row_to_json(NEW)->>'evidence_id'),
            (row_to_json(OLD)->>'evidence_id'))::INTEGER
        WHEN 'officers'      THEN COALESCE(
            (row_to_json(NEW)->>'officer_id'),
            (row_to_json(OLD)->>'officer_id'))::INTEGER
        WHEN 'arrests'       THEN COALESCE(
            (row_to_json(NEW)->>'arrest_id'),
            (row_to_json(OLD)->>'arrest_id'))::INTEGER
        WHEN 'warrants'      THEN COALESCE(
            (row_to_json(NEW)->>'warrant_id'),
            (row_to_json(OLD)->>'warrant_id'))::INTEGER
        WHEN 'charge_sheets' THEN COALESCE(
            (row_to_json(NEW)->>'charge_sheet_id'),
            (row_to_json(OLD)->>'charge_sheet_id'))::INTEGER
        WHEN 'bail_records'  THEN COALESCE(
            (row_to_json(NEW)->>'bail_id'),
            (row_to_json(OLD)->>'bail_id'))::INTEGER
        WHEN 'accused'       THEN COALESCE(
            (row_to_json(NEW)->>'accused_id'),
            (row_to_json(OLD)->>'accused_id'))::INTEGER
        ELSE 0
    END;

    -- -----------------------------------------------------------------------
    -- Insert the audit entry.
    -- This INSERT is atomic with the triggering DML — both commit or both
    -- roll back. There is no window between the operation and its audit entry.
    -- -----------------------------------------------------------------------
    INSERT INTO audit.Audit_Log (
        table_name,
        record_pk,
        operation,
        officer_id,
        belt_number,
        backend_pid,    -- Analogous to getpid() — OS PID of this PG backend
        client_addr,    -- Analogous to getpeername() — IP of connecting client
        old_values,
        new_values,
        changed_at
    ) VALUES (
        UPPER(TG_TABLE_NAME),
        v_record_pk,
        TG_OP,
        v_officer_id,
        v_belt_number,
        pg_backend_pid(),
        inet_client_addr()::TEXT,
        v_old_values,
        v_new_values,
        NOW()
    );

    -- Return value: AFTER triggers use RETURN NULL for statements
    -- or RETURN NEW/OLD for row-level. We return the row unchanged.
    RETURN COALESCE(NEW, OLD);
END;
$$;


-- =============================================================================
-- TRIGGER FUNCTION 2: audit.enforce_immutability()
--
-- Blocks two categories of illegal DML:
--
-- A) Any modification to audit.Audit_Log itself (UPDATE or DELETE).
--    The audit log is append-only. Once written, an entry can never be changed
--    or removed — this is a legal chain-of-custody requirement.
--    An attempt to tamper raises an exception that rolls back the transaction.
--
-- B) Hard DELETE on the evidence table.
--    Evidence can never be physically deleted — only soft-deleted
--    (UPDATE is_deleted = TRUE). The C++ layer (EvidenceRules::enforceSoftDelete)
--    is the first line of defence; this trigger is the second.
--    Defense in Depth: two independent enforcement points for the same rule.
-- =============================================================================

CREATE OR REPLACE FUNCTION audit.enforce_immutability()
RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, audit
AS $$
BEGIN
    IF TG_TABLE_SCHEMA = 'audit' AND TG_TABLE_NAME = 'audit_log' THEN
        RAISE EXCEPTION
            'IMMUTABLE LOG VIOLATION: audit.Audit_Log cannot be modified or deleted. '
            'Operation: %, attempted by backend PID: %',
            TG_OP, pg_backend_pid()
            USING ERRCODE = 'restrict_violation';
    END IF;

    IF TG_TABLE_SCHEMA = 'subsystem2' AND TG_TABLE_NAME = 'evidence'
       AND TG_OP = 'DELETE'
    THEN
        RAISE EXCEPTION
            'EVIDENCE INTEGRITY VIOLATION: Hard DELETE on evidence is prohibited. '
            'Use soft-delete (UPDATE is_deleted = TRUE) instead. '
            'Evidence ID: %, backend PID: %',
            OLD.evidence_id, pg_backend_pid()
            USING ERRCODE = 'restrict_violation';
    END IF;

    -- Should not be reached — trigger installed only on the two tables above
    RETURN COALESCE(NEW, OLD);
END;
$$;


-- =============================================================================
-- TRIGGER INSTALLATIONS
--
-- audit.log_change — installed on all 8 audited tables (AFTER each DML row)
-- audit.enforce_immutability — installed on audit.Audit_Log and subsystem2.evidence
-- =============================================================================

-- ---- subsystem2.cases -------------------------------------------------------
DROP TRIGGER IF EXISTS trg_audit_cases ON subsystem2.cases;
CREATE TRIGGER trg_audit_cases
    AFTER INSERT OR UPDATE OR DELETE ON subsystem2.cases
    FOR EACH ROW EXECUTE FUNCTION audit.log_change();

-- ---- subsystem2.evidence ----------------------------------------------------
DROP TRIGGER IF EXISTS trg_audit_evidence ON subsystem2.evidence;
CREATE TRIGGER trg_audit_evidence
    AFTER INSERT OR UPDATE OR DELETE ON subsystem2.evidence
    FOR EACH ROW EXECUTE FUNCTION audit.log_change();

DROP TRIGGER IF EXISTS trg_immutable_evidence ON subsystem2.evidence;
CREATE TRIGGER trg_immutable_evidence
    BEFORE DELETE ON subsystem2.evidence
    FOR EACH ROW EXECUTE FUNCTION audit.enforce_immutability();

-- ---- subsystem1.officers ----------------------------------------------------
DROP TRIGGER IF EXISTS trg_audit_officers ON subsystem1.officers;
CREATE TRIGGER trg_audit_officers
    AFTER INSERT OR UPDATE OR DELETE ON subsystem1.officers
    FOR EACH ROW EXECUTE FUNCTION audit.log_change();

-- ---- subsystem3.arrests -----------------------------------------------------
DROP TRIGGER IF EXISTS trg_audit_arrests ON subsystem3.arrests;
CREATE TRIGGER trg_audit_arrests
    AFTER INSERT OR UPDATE OR DELETE ON subsystem3.arrests
    FOR EACH ROW EXECUTE FUNCTION audit.log_change();

-- ---- subsystem3.warrants ----------------------------------------------------
DROP TRIGGER IF EXISTS trg_audit_warrants ON subsystem3.warrants;
CREATE TRIGGER trg_audit_warrants
    AFTER INSERT OR UPDATE OR DELETE ON subsystem3.warrants
    FOR EACH ROW EXECUTE FUNCTION audit.log_change();

-- ---- subsystem3.charge_sheets -----------------------------------------------
DROP TRIGGER IF EXISTS trg_audit_charge_sheets ON subsystem3.charge_sheets;
CREATE TRIGGER trg_audit_charge_sheets
    AFTER INSERT OR UPDATE OR DELETE ON subsystem3.charge_sheets
    FOR EACH ROW EXECUTE FUNCTION audit.log_change();

-- ---- subsystem3.bail_records ------------------------------------------------
DROP TRIGGER IF EXISTS trg_audit_bail_records ON subsystem3.bail_records;
CREATE TRIGGER trg_audit_bail_records
    AFTER INSERT OR UPDATE OR DELETE ON subsystem3.bail_records
    FOR EACH ROW EXECUTE FUNCTION audit.log_change();

-- ---- subsystem2.accused -----------------------------------------------------
DROP TRIGGER IF EXISTS trg_audit_accused ON subsystem2.accused;
CREATE TRIGGER trg_audit_accused
    AFTER INSERT OR UPDATE OR DELETE ON subsystem2.accused
    FOR EACH ROW EXECUTE FUNCTION audit.log_change();

-- ---- audit.Audit_Log (immutability) -----------------------------------------
DROP TRIGGER IF EXISTS trg_immutable_audit_log ON audit.Audit_Log;
CREATE TRIGGER trg_immutable_audit_log
    BEFORE UPDATE OR DELETE ON audit.Audit_Log
    FOR EACH ROW EXECUTE FUNCTION audit.enforce_immutability();


-- =============================================================================
-- GRANT (principle of least privilege)
--
-- audit_writer: owns log_change and enforce_immutability (SECURITY DEFINER).
--               Only role that can INSERT into audit.Audit_Log.
--               Application role has NO direct INSERT/UPDATE/DELETE on the log.
--
-- app_role:     Can SELECT audit.Audit_Log (for AuditManager reads).
--               Cannot INSERT/UPDATE/DELETE directly.
--
-- These grants assume audit_writer and app_role roles exist in the cluster.
-- =============================================================================

-- Revoke direct write access from application role (belt-and-suspenders)
REVOKE INSERT, UPDATE, DELETE ON audit.Audit_Log FROM PUBLIC;

-- Allow the audit manager read role to SELECT
GRANT SELECT ON audit.Audit_Log TO app_role;

-- =============================================================================
-- END: audit_triggers.sql
-- =============================================================================