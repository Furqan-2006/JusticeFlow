-- ============================================================
-- JusticeFlow — 03_functions.sql
-- All stored functions: number generators, expiry,
-- validation, and audit
-- ============================================================


-- ============================================================
-- SECTION 1: NUMBER GENERATOR FUNCTIONS
-- Called by BEFORE INSERT triggers on respective tables
-- ============================================================

-- ------------------------------------------------------------
-- HELPER: get_next_seq(entity, scope_key)
-- Atomically increments and returns next sequence value
-- SELECT FOR UPDATE ensures no two concurrent calls get same value
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION get_next_seq(
    p_entity    VARCHAR(20),
    p_scope_key VARCHAR(50)
)
RETURNS INT AS $$
DECLARE
    v_next INT;
BEGIN
    -- try to update existing row
    UPDATE Sequence_Registry
    SET    last_value = last_value + 1
    WHERE  entity     = p_entity
    AND    scope_key  = p_scope_key
    RETURNING last_value INTO v_next;

    -- if no row exists yet, create it (first record of the year)
    IF NOT FOUND THEN
        INSERT INTO Sequence_Registry (entity, scope_key, last_value)
        VALUES (p_entity, p_scope_key, 1)
        ON CONFLICT (entity, scope_key)
        DO UPDATE SET last_value = Sequence_Registry.last_value + 1
        RETURNING last_value INTO v_next;
    END IF;

    RETURN v_next;
END;
$$ LANGUAGE plpgsql;

-- ------------------------------------------------------------
-- 1.1  FIR Number Generator
-- Format: FIR-YYYY-STATIONCODE-NNNN
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_fir_number()
RETURNS TRIGGER AS $$
DECLARE
    v_station_code  VARCHAR(20);
    v_year          VARCHAR(4);
    v_seq           INT;
BEGIN
    SELECT station_code INTO v_station_code
    FROM   Stations
    WHERE  station_id = NEW.station_id;

    v_year := TO_CHAR(NOW(), 'YYYY');

    v_seq := get_next_seq('FIR', v_station_code || '-' || v_year);

    NEW.fir_number := 'FIR-' || v_year
                             || '-' || v_station_code
                             || '-' || LPAD(v_seq::TEXT, 4, '0');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- ------------------------------------------------------------
-- 1.2  Evidence Number Generator
-- Format: EVD-FIR-CASENUMBER-NNNN
-- e.g.   EVD-FIR-2024-KHD-0001-0003
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_evidence_number()
RETURNS TRIGGER AS $$
DECLARE
    v_fir_number    VARCHAR(30);
    v_seq           INT;
BEGIN
    SELECT fir_number INTO v_fir_number
    FROM   Cases
    WHERE  case_id = NEW.case_id;

    v_seq := get_next_seq('EVD', v_fir_number);

    NEW.evidence_number := 'EVD-' || v_fir_number
                                  || '-' || LPAD(v_seq::TEXT, 4, '0');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- ------------------------------------------------------------
-- 1.3  Arrest Number Generator
-- Format: ARR-YYYY-STATIONCODE-NNNN
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_arrest_number()
RETURNS TRIGGER AS $$
DECLARE
    v_station_code  VARCHAR(20);
    v_year          VARCHAR(4);
    v_seq           INT;
BEGIN
    SELECT st.station_code INTO v_station_code
    FROM   Stations st
    JOIN   Cases    c  ON c.station_id = st.station_id
    WHERE  c.case_id = NEW.case_id;

    v_year := TO_CHAR(NOW(), 'YYYY');

    v_seq := get_next_seq('ARR', v_station_code || '-' || v_year);

    NEW.arrest_number := 'ARR-' || v_year
                                || '-' || v_station_code
                                || '-' || LPAD(v_seq::TEXT, 4, '0');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 1.4  Warrant Number Generator
-- Format: WRT-YYYY-STATIONCODE-NNNN
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_warrant_number()
RETURNS TRIGGER AS $$
DECLARE
    v_station_code  VARCHAR(20);
    v_year          VARCHAR(4);
    v_seq           INT;
BEGIN
    SELECT st.station_code INTO v_station_code
    FROM   Stations st
    JOIN   Cases    c  ON c.station_id = st.station_id
    WHERE  c.case_id = NEW.case_id;

    v_year := TO_CHAR(NOW(), 'YYYY');

    v_seq := get_next_seq('WRT', v_station_code || '-' || v_year);

    NEW.warrant_number := 'WRT-' || v_year
                                 || '-' || v_station_code
                                 || '-' || LPAD(v_seq::TEXT, 4, '0');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 1.5  Bail Number Generator
-- Format: BAIL-YYYY-STATIONCODE-NNNN
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_bail_number()
RETURNS TRIGGER AS $$
DECLARE
    v_station_code  VARCHAR(20);
    v_year          VARCHAR(4);
    v_seq           INT;
BEGIN
    SELECT st.station_code INTO v_station_code
    FROM   Stations  st
    JOIN   Cases     c  ON c.station_id = st.station_id
    JOIN   Arrests   a  ON a.case_id    = c.case_id
    WHERE  a.arrest_id = NEW.arrest_id;

    v_year := TO_CHAR(NOW(), 'YYYY');

    v_seq := get_next_seq('BAIL', v_station_code || '-' || v_year);

    NEW.bail_number := 'BAIL-' || v_year
                               || '-' || v_station_code
                               || '-' || LPAD(v_seq::TEXT, 4, '0');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 1.6  Forensic Request Number Generator
-- Format: FLR-YYYY-STATIONCODE-NNNN
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_forensic_request_number()
RETURNS TRIGGER AS $$
DECLARE
    v_station_code  VARCHAR(20);
    v_year          VARCHAR(4);
    v_seq           INT;
BEGIN
    SELECT st.station_code INTO v_station_code
    FROM   Stations st
    JOIN   Cases    c  ON c.station_id = st.station_id
    WHERE  c.case_id = NEW.case_id;

    v_year := TO_CHAR(NOW(), 'YYYY');

    v_seq := get_next_seq('FLR', v_station_code || '-' || v_year);

    NEW.request_number := 'FLR-' || v_year
                                  || '-' || v_station_code
                                  || '-' || LPAD(v_seq::TEXT, 4, '0');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 1.7  Charge Sheet Number Generator
-- Format: CS-YYYY-STATIONCODE-NNNN
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_charge_sheet_number()
RETURNS TRIGGER AS $$
DECLARE
    v_station_code  VARCHAR(20);
    v_year          VARCHAR(4);
    v_seq           INT;
BEGIN
    SELECT st.station_code INTO v_station_code
    FROM   Stations st
    JOIN   Cases    c  ON c.station_id = st.station_id
    WHERE  c.case_id = NEW.case_id;

    v_year := TO_CHAR(NOW(), 'YYYY');

    v_seq := get_next_seq('CS', v_station_code || '-' || v_year);

    NEW.charge_sheet_number := 'CS-' || v_year
                                     || '-' || v_station_code
                                     || '-' || LPAD(v_seq::TEXT, 4, '0');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 1.8  Duty Number Generator
-- Format: DUTY-YYYY-STATIONCODE-NNNN
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION generate_duty_number()
RETURNS TRIGGER AS $$
DECLARE
    v_station_code  VARCHAR(20);
    v_year          VARCHAR(4);
    v_seq           INT;
BEGIN
    SELECT station_code INTO v_station_code
    FROM   Stations
    WHERE  station_id = NEW.station_id;

    v_year := TO_CHAR(NOW(), 'YYYY');

    v_seq := get_next_seq('DUTY', v_station_code || '-' || v_year);

    NEW.duty_number := 'DUTY-' || v_year
                               || '-' || v_station_code
                               || '-' || LPAD(v_seq::TEXT, 4, '0');
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ============================================================
-- SECTION 2: EXPIRY FUNCTIONS
-- Called by OS job scheduler — not by triggers
-- OS layer registers these as cron-style jobs:
--   expire_warrants()    → runs daily at 00:01
--   expire_bail_records() → runs daily at 00:01
-- ============================================================

-- ------------------------------------------------------------
-- 2.1  Warrant Expiry
-- Marks all ISSUED warrants past valid_until as EXPIRED
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION expire_warrants()
RETURNS VOID AS $$
DECLARE
    v_count INT;
BEGIN
    UPDATE Warrants
    SET    warrant_status = 'EXPIRED',
           updated_at     = NOW()
    WHERE  warrant_status = 'ISSUED'
    AND    valid_until    < CURRENT_DATE;

    GET DIAGNOSTICS v_count = ROW_COUNT;

    RAISE NOTICE '[expire_warrants] % warrant(s) marked EXPIRED at %',
        v_count, NOW();
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 2.2  Bail Record Expiry
-- Marks all ACTIVE bail records past valid_until as EXPIRED
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION expire_bail_records()
RETURNS VOID AS $$
DECLARE
    v_count INT;
BEGIN
    UPDATE Bail_Records
    SET    bail_status = 'EXPIRED',
           updated_at  = NOW()
    WHERE  bail_status = 'ACTIVE'
    AND    valid_until IS NOT NULL
    AND    valid_until < CURRENT_DATE;

    GET DIAGNOSTICS v_count = ROW_COUNT;

    RAISE NOTICE '[expire_bail_records] % bail record(s) marked EXPIRED at %',
        v_count, NOW();
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 2.3  Workload Assignment Expiry
-- Marks SUGGESTED assignments past expires_at as AUTO_EXPIRED
-- Called by OS job scheduler hourly
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION expire_workload_assignments()
RETURNS VOID AS $$
DECLARE
    v_count INT;
BEGIN
    UPDATE analytics.Officer_Workload_Assignments
    SET    assignment_status = 'AUTO_EXPIRED'
    WHERE  assignment_status = 'SUGGESTED'
    AND    expires_at        < NOW();

    GET DIAGNOSTICS v_count = ROW_COUNT;

    RAISE NOTICE '[expire_workload_assignments] % suggestion(s) expired at %',
        v_count, NOW();
END;
$$ LANGUAGE plpgsql;


-- ============================================================
-- SECTION 3: VALIDATION FUNCTIONS
-- Called by BEFORE INSERT/UPDATE triggers
-- ============================================================

-- ------------------------------------------------------------
-- 3.1  Validate Patrol Route belongs to duty station
-- Prevents officer being assigned a route from another station
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION validate_patrol_route_station()
RETURNS TRIGGER AS $$
DECLARE
    v_route_station_id BIGINT;
BEGIN
    -- if no patrol route assigned, nothing to validate
    IF NEW.patrol_route_id IS NULL THEN
        RETURN NEW;
    END IF;

    SELECT station_id INTO v_route_station_id
    FROM   Patrol_Routes
    WHERE  route_id = NEW.patrol_route_id;

    IF v_route_station_id <> NEW.station_id THEN
        RAISE EXCEPTION
            'Patrol route % belongs to station %, '
            'not the duty station %. '
            'Officer cannot be assigned a route outside their station.',
            NEW.patrol_route_id,
            v_route_station_id,
            NEW.station_id;
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 3.2  Validate Officer Duty Status before case assignment
-- Blocks assignment if officer is ON_LEAVE, SUSPENDED, or ABSENT
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION validate_officer_duty_status()
RETURNS TRIGGER AS $$
DECLARE
    v_duty_status   duty_status_enum;
    v_officer_status officer_status_enum;
BEGIN
    -- check officer-level status first
    SELECT status INTO v_officer_status
    FROM   Officers
    WHERE  officer_id = NEW.officer_id;

    IF v_officer_status IN ('SUSPENDED', 'RETIRED', 'TERMINATED') THEN
        RAISE EXCEPTION
            'Officer % cannot be assigned to a case. '
            'Officer status: %',
            NEW.officer_id,
            v_officer_status;
    END IF;

    -- check today's duty roster status
    SELECT duty_status INTO v_duty_status
    FROM   Duty_Roster
    WHERE  officer_id = NEW.officer_id
    AND    duty_date  = CURRENT_DATE
    LIMIT  1;

    IF v_duty_status IN ('ON_LEAVE', 'SUSPENDED', 'ABSENT') THEN
        RAISE EXCEPTION
            'Officer % cannot be assigned to a case. '
            'Current duty status: %',
            NEW.officer_id,
            v_duty_status;
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 3.3  Validate FIR filing officer rank
-- Only SI or ASI can file an FIR (duty incharge)
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION validate_fir_filing_rank()
RETURNS TRIGGER AS $$
DECLARE
    v_rank officer_rank_enum;
BEGIN
    SELECT current_rank INTO v_rank
    FROM   Officers
    WHERE  officer_id = NEW.filed_by;

    IF v_rank NOT IN ('SI', 'ASI', 'INSPECTOR') THEN
        RAISE EXCEPTION
            'Officer % (rank: %) is not authorized to file an FIR. '
            'Only ASI, SI, or INSPECTOR (duty incharge) can file FIRs.',
            NEW.filed_by,
            v_rank;
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 3.4  Validate warrant request rank
-- Only INSPECTOR or above can request a warrant
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION validate_warrant_request_rank()
RETURNS TRIGGER AS $$
DECLARE
    v_rank officer_rank_enum;
BEGIN
    SELECT current_rank INTO v_rank
    FROM   Officers
    WHERE  officer_id = NEW.requested_by;

    IF v_rank NOT IN (
        'INSPECTOR', 'DSP', 'SP', 'SSP', 'DIG', 'ADDL_IG', 'IGP'
    ) THEN
        RAISE EXCEPTION
            'Officer % (rank: %) is not authorized to request a warrant. '
            'Minimum rank required: INSPECTOR.',
            NEW.requested_by,
            v_rank;
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 3.5  Validate case closure approval rank
-- Only INSPECTOR (SHO) or above can approve case closure
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION validate_case_closure_rank()
RETURNS TRIGGER AS $$
DECLARE
    v_rank officer_rank_enum;
BEGIN
    -- only check on status transition to CLOSED
    IF NEW.case_status = 'CLOSED'
    AND OLD.case_status <> 'CLOSED'
    AND NEW.approved_by IS NOT NULL THEN

        SELECT current_rank INTO v_rank
        FROM   Officers
        WHERE  officer_id = NEW.approved_by;

        IF v_rank NOT IN (
            'INSPECTOR', 'DSP', 'SP', 'SSP',
            'DIG', 'ADDL_IG', 'IGP'
        ) THEN
            RAISE EXCEPTION
                'Officer % (rank: %) is not authorized to approve case closure. '
                'Minimum rank required: INSPECTOR (SHO).',
                NEW.approved_by,
                v_rank;
        END IF;
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 3.6  Validate active case limit per officer
-- Max 10 active cases per officer at any time
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION validate_officer_case_limit()
RETURNS TRIGGER AS $$
DECLARE
    v_active_count INT;
BEGIN
    SELECT COUNT(*) INTO v_active_count
    FROM   Case_Officers co
    JOIN   Cases         c  ON c.case_id = co.case_id
    WHERE  co.officer_id  = NEW.officer_id
    AND    co.relieved_at IS NULL
    AND    c.case_status  NOT IN ('CLOSED');

    IF v_active_count >= 10 THEN
        RAISE EXCEPTION
            'Officer % already has % active case(s). '
            'Maximum allowed is 10. '
            'Relieve officer from an existing case before assigning a new one.',
            NEW.officer_id,
            v_active_count;
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ============================================================
-- SECTION 4: IMMUTABILITY FUNCTIONS
-- Protect critical legal records from modification
-- ============================================================

-- ------------------------------------------------------------
-- 4.1  Prevent hard DELETE on Evidence
-- Evidence can only be soft-deleted (is_deleted = TRUE)
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION prevent_evidence_hard_delete()
RETURNS TRIGGER AS $$
BEGIN
    RAISE EXCEPTION
        'Hard delete on Evidence is strictly forbidden. '
        'Use soft delete: UPDATE Evidence SET is_deleted = TRUE. '
        'Evidence ID: %', OLD.evidence_id;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 4.2  Prevent DELETE on Audit_Log
-- Audit records are permanent legal records
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION prevent_audit_log_delete()
RETURNS TRIGGER AS $$
BEGIN
    RAISE EXCEPTION
        'Deletion from audit.Audit_Log is strictly forbidden. '
        'Audit ID: % is a permanent legal record. '
        'Contact the system administrator if this is an error.',
        OLD.audit_id;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 4.3  Prevent UPDATE on Audit_Log
-- No modification of audit records allowed
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION prevent_audit_log_update()
RETURNS TRIGGER AS $$
BEGIN
    RAISE EXCEPTION
        'Modification of audit.Audit_Log is strictly forbidden. '
        'Audit ID: % is a permanent legal record.',
        OLD.audit_id;
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 4.4  Prevent UPDATE on locked Charge Sheet
-- Once submitted to court the charge sheet is immutable
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION prevent_locked_charge_sheet_update()
RETURNS TRIGGER AS $$
BEGIN
    IF OLD.is_locked = TRUE THEN
        RAISE EXCEPTION
            'Charge Sheet % is locked after court submission. '
            'No modifications are permitted on a locked charge sheet.',
            OLD.charge_sheet_number;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ============================================================
-- SECTION 5: STATE SYNC FUNCTIONS
-- Keep related records in sync automatically
-- ============================================================

-- ------------------------------------------------------------
-- 5.1  Sync Evidence status → SENT_TO_LAB
-- Fires when evidence is added to a forensic request
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION sync_evidence_sent_to_lab()
RETURNS TRIGGER AS $$
BEGIN
    UPDATE Evidence
    SET    evidence_status = 'SENT_TO_LAB',
           updated_at      = NOW()
    WHERE  evidence_id     = NEW.evidence_id
    AND    evidence_status <> 'SENT_TO_LAB';

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 5.2  Sync Evidence status → RETURNED_FROM_LAB
-- Fires when forensic request status changes to REPORT_DELIVERED
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION sync_evidence_returned_from_lab()
RETURNS TRIGGER AS $$
BEGIN
    IF NEW.request_status = 'REPORT_DELIVERED'
    AND OLD.request_status <> 'REPORT_DELIVERED' THEN

        UPDATE Evidence
        SET    evidence_status = 'RETURNED_FROM_LAB',
               updated_at      = NOW()
        WHERE  evidence_id IN (
            SELECT evidence_id
            FROM   Forensic_Request_Evidence
            WHERE  request_id = NEW.request_id
        );

    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 5.3  Auto-lock Charge Sheet on court submission
-- Fires when charge_sheet_status changes to SUBMITTED_TO_COURT
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION auto_lock_charge_sheet()
RETURNS TRIGGER AS $$
BEGIN
    IF NEW.charge_sheet_status = 'SUBMITTED_TO_COURT'
    AND OLD.charge_sheet_status <> 'SUBMITTED_TO_COURT' THEN
        NEW.is_locked  := TRUE;
        NEW.locked_at  := NOW();
        NEW.locked_by  := NEW.submitted_by;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ------------------------------------------------------------
-- 5.4  Log Case Status Change
-- Fires on every case_status UPDATE
-- Writes to Case_Status_Log automatically
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION log_case_status_change()
RETURNS TRIGGER AS $$
DECLARE
    v_officer_id BIGINT;
BEGIN
    IF NEW.case_status <> OLD.case_status THEN

        -- resolve officer from session variable
        BEGIN
            v_officer_id := current_setting('app.current_officer_id')::BIGINT;
        EXCEPTION WHEN OTHERS THEN
            v_officer_id := NULL;
        END;

        INSERT INTO Case_Status_Log (
            case_id,
            old_status,
            new_status,
            changed_by,
            changed_at
        ) VALUES (
            NEW.case_id,
            OLD.case_status,
            NEW.case_status,
            COALESCE(v_officer_id, NEW.lead_officer_id, NEW.filed_by),
            NOW()
        );

    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;


-- ============================================================
-- SECTION 6: AUDIT FUNCTION
-- Single reusable function attached to all 8 audited tables
-- SECURITY DEFINER ensures only the trigger can write to audit
-- ============================================================

-- ------------------------------------------------------------
-- 6.1  Core Audit Log Function
-- Captures: table, record PK, action, old/new JSONB,
--           session user, officer ID, belt number,
--           process ID, client IP
-- ------------------------------------------------------------
CREATE OR REPLACE FUNCTION audit.log_change()
RETURNS TRIGGER AS $$
DECLARE
    v_record_id         BIGINT;
    v_old_value         JSONB;
    v_new_value         JSONB;
    v_officer_id        BIGINT;
    v_belt_number       VARCHAR(20);
BEGIN
    -- build old/new snapshots based on operation
    IF TG_OP = 'DELETE' THEN
        v_old_value  := row_to_json(OLD)::JSONB;
        v_new_value  := NULL;

        -- extract PK from old row based on table
        CASE TG_TABLE_NAME
            WHEN 'cases'        THEN v_record_id := OLD.case_id;
            WHEN 'evidence'     THEN v_record_id := OLD.evidence_id;
            WHEN 'officers'     THEN v_record_id := OLD.officer_id;
            WHEN 'arrests'      THEN v_record_id := OLD.arrest_id;
            WHEN 'warrants'     THEN v_record_id := OLD.warrant_id;
            WHEN 'charge_sheets' THEN v_record_id := OLD.charge_sheet_id;
            WHEN 'bail_records' THEN v_record_id := OLD.bail_id;
            WHEN 'accused'      THEN v_record_id := OLD.accused_id;
            ELSE v_record_id := -1;
        END CASE;

    ELSIF TG_OP = 'INSERT' THEN
        v_old_value  := NULL;
        v_new_value  := row_to_json(NEW)::JSONB;

        CASE TG_TABLE_NAME
            WHEN 'cases'        THEN v_record_id := NEW.case_id;
            WHEN 'evidence'     THEN v_record_id := NEW.evidence_id;
            WHEN 'officers'     THEN v_record_id := NEW.officer_id;
            WHEN 'arrests'      THEN v_record_id := NEW.arrest_id;
            WHEN 'warrants'     THEN v_record_id := NEW.warrant_id;
            WHEN 'charge_sheets' THEN v_record_id := NEW.charge_sheet_id;
            WHEN 'bail_records' THEN v_record_id := NEW.bail_id;
            WHEN 'accused'      THEN v_record_id := NEW.accused_id;
            ELSE v_record_id := -1;
        END CASE;

    ELSE -- UPDATE
        v_old_value  := row_to_json(OLD)::JSONB;
        v_new_value  := row_to_json(NEW)::JSONB;

        CASE TG_TABLE_NAME
            WHEN 'cases'        THEN v_record_id := NEW.case_id;
            WHEN 'evidence'     THEN v_record_id := NEW.evidence_id;
            WHEN 'officers'     THEN v_record_id := NEW.officer_id;
            WHEN 'arrests'      THEN v_record_id := NEW.arrest_id;
            WHEN 'warrants'     THEN v_record_id := NEW.warrant_id;
            WHEN 'charge_sheets' THEN v_record_id := NEW.charge_sheet_id;
            WHEN 'bail_records' THEN v_record_id := NEW.bail_id;
            WHEN 'accused'      THEN v_record_id := NEW.accused_id;
            ELSE v_record_id := -1;
        END CASE;
    END IF;

    -- resolve application-level officer from session variable
    -- application sets this at login:
    -- SET LOCAL app.current_officer_id = '42';
    BEGIN
        v_officer_id := current_setting('app.current_officer_id')::BIGINT;

        SELECT belt_number INTO v_belt_number
        FROM   public.Officers
        WHERE  officer_id = v_officer_id;

    EXCEPTION WHEN OTHERS THEN
        v_officer_id  := NULL;
        v_belt_number := NULL;
    END;

    -- write the permanent audit record
    INSERT INTO audit.Audit_Log (
        table_name,
        record_id,
        action,
        old_value,
        new_value,
        changed_by_user,
        changed_by_officer_id,
        changed_by_belt,
        client_process_id,
        client_ip,
        changed_at
    ) VALUES (
        TG_TABLE_NAME::audited_table_enum,
        v_record_id,
        TG_OP::audit_action_enum,
        v_old_value,
        v_new_value,
        SESSION_USER,
        v_officer_id,
        v_belt_number,
        pg_backend_pid(),
        inet_client_addr(),
        NOW()
    );

    -- return correct row for trigger chain
    IF TG_OP = 'DELETE' THEN
        RETURN OLD;
    END IF;
    RETURN NEW;

END;
$$ LANGUAGE plpgsql SECURITY DEFINER;