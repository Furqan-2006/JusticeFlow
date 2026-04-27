-- =============================================================================
-- enforcement_jobs.sql
-- Subsystem 3 — Enforcement SQL Jobs
--
-- Two SQL functions called directly by the OS scheduler at 00:01 UTC daily.
-- No C++ wrapper — the job_executor calls these as raw SQL via libpq.
-- Audit trigger fires automatically on every row updated — no explicit
-- audit call is needed in these functions.
--
-- Both functions:
--   1. Perform a filtered UPDATE (status → EXPIRED)
--   2. Capture affected row count via GET DIAGNOSTICS
--   3. Log a summary row into analytics.Job_Run_Log
--   4. Return the affected row count to the scheduler
--
-- Error handling:
--   If either function raises an exception, the calling job_executor catches
--   the non-zero exit from PQexec, logs the error, and marks the job as
--   FAILED in analytics.Job_Run_Log. The OS scheduler retries at the next
--   tick. Both functions are idempotent — re-running them is safe.
--
-- SECURITY: Both functions run as the scheduled_job role which has:
--   UPDATE on subsystem3.warrants
--   UPDATE on subsystem3.bail_records
--   INSERT on analytics.Job_Run_Log
--   No SELECT/INSERT/DELETE on any other table
-- =============================================================================


-- =============================================================================
-- FUNCTION: subsystem3.expire_warrants()
--
-- Marks all ISSUED warrants whose valid_until < CURRENT_DATE as EXPIRED.
-- Called nightly at 00:01 UTC by the OS scheduler.
--
-- State transition enforced here:  ISSUED → EXPIRED
-- The C++ state machine does NOT handle this transition — it is DB-only.
-- This is intentional: the OS scheduler is the authoritative clock source
-- for expiry, preventing race conditions between application writes and
-- expiry checks.
--
-- Returns: INTEGER — count of warrants expired in this run.
-- =============================================================================

CREATE OR REPLACE FUNCTION subsystem3.expire_warrants()
RETURNS INTEGER
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, subsystem3, analytics
AS $$
DECLARE
    v_rows_affected INTEGER;
    v_started_at    TIMESTAMPTZ := NOW();
BEGIN
    UPDATE subsystem3.warrants
    SET    warrant_status = 'EXPIRED',
           updated_at     = NOW()
    WHERE  warrant_status = 'ISSUED'
    AND    valid_until    < CURRENT_DATE;
    -- Audit trigger (audit.log_change) fires for every row updated above.
    -- officer_id will be 0 (SYSTEM) since no session variable is set by scheduler.

    GET DIAGNOSTICS v_rows_affected = ROW_COUNT;

    -- Log run to analytics
    INSERT INTO analytics.Job_Run_Log (
        job_name, run_started_at, run_finished_at,
        rows_affected, status
    ) VALUES (
        'expire_warrants',
        v_started_at,
        NOW(),
        v_rows_affected,
        'SUCCESS'
    );

    RETURN v_rows_affected;

EXCEPTION WHEN OTHERS THEN
    -- Record failure without re-raising so the scheduler can read the return
    INSERT INTO analytics.Job_Run_Log (
        job_name, run_started_at, run_finished_at,
        rows_affected, status, error_detail
    ) VALUES (
        'expire_warrants',
        v_started_at,
        NOW(),
        0,
        'FAILED',
        SQLERRM
    );
    RAISE; -- Re-raise so job_executor sees a non-OK result
END;
$$;


-- =============================================================================
-- FUNCTION: subsystem3.expire_bail_records()
--
-- Marks all ACTIVE bail records whose valid_until < CURRENT_DATE as EXPIRED.
-- Records with valid_until IS NULL (personal recognizance bail) are skipped.
-- Called nightly at 00:01 UTC by the OS scheduler.
--
-- State transition enforced here:  ACTIVE → EXPIRED
--
-- Returns: INTEGER — count of bail records expired in this run.
-- =============================================================================

CREATE OR REPLACE FUNCTION subsystem3.expire_bail_records()
RETURNS INTEGER
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, subsystem3, analytics
AS $$
DECLARE
    v_rows_affected INTEGER;
    v_started_at    TIMESTAMPTZ := NOW();
BEGIN
    UPDATE subsystem3.bail_records
    SET    bail_status = 'EXPIRED',
           updated_at  = NOW()
    WHERE  bail_status = 'ACTIVE'
    AND    valid_until IS NOT NULL
    AND    valid_until < CURRENT_DATE;
    -- Audit trigger fires for every row updated above.

    GET DIAGNOSTICS v_rows_affected = ROW_COUNT;

    INSERT INTO analytics.Job_Run_Log (
        job_name, run_started_at, run_finished_at,
        rows_affected, status
    ) VALUES (
        'expire_bail_records',
        v_started_at,
        NOW(),
        v_rows_affected,
        'SUCCESS'
    );

    RETURN v_rows_affected;

EXCEPTION WHEN OTHERS THEN
    INSERT INTO analytics.Job_Run_Log (
        job_name, run_started_at, run_finished_at,
        rows_affected, status, error_detail
    ) VALUES (
        'expire_bail_records',
        v_started_at,
        NOW(),
        0,
        'FAILED',
        SQLERRM
    );
    RAISE;
END;
$$;


-- =============================================================================
-- Supporting table: analytics.Job_Run_Log
-- Created here so enforcement_jobs.sql is self-contained.
-- =============================================================================

CREATE SCHEMA IF NOT EXISTS analytics;

CREATE TABLE IF NOT EXISTS analytics.Job_Run_Log (
    run_id          SERIAL       PRIMARY KEY,
    job_name        VARCHAR(64)  NOT NULL,
    run_started_at  TIMESTAMPTZ  NOT NULL,
    run_finished_at TIMESTAMPTZ  NOT NULL,
    rows_affected   INTEGER      NOT NULL DEFAULT 0,
    status          VARCHAR(16)  NOT NULL CHECK (status IN ('SUCCESS', 'FAILED')),
    error_detail    TEXT
);

CREATE INDEX IF NOT EXISTS idx_job_run_log_job_name_time
    ON analytics.Job_Run_Log (job_name, run_started_at DESC);


-- =============================================================================
-- GRANTS
-- =============================================================================

GRANT EXECUTE ON FUNCTION subsystem3.expire_warrants()     TO scheduled_job;
GRANT EXECUTE ON FUNCTION subsystem3.expire_bail_records() TO scheduled_job;
GRANT INSERT  ON analytics.Job_Run_Log                     TO scheduled_job;

-- =============================================================================
-- END: enforcement_jobs.sql
-- =============================================================================