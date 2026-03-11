-- ============================================================
-- JusticeFlow — 04_triggers.sql
-- All triggers: attach functions to tables
-- Run after 03_functions.sql
-- ============================================================


-- ============================================================
-- SECTION 1: NUMBER GENERATOR TRIGGERS
-- BEFORE INSERT — populate auto-generated number columns
-- ============================================================

CREATE TRIGGER trg_generate_fir_number
    BEFORE INSERT ON Cases
    FOR EACH ROW
    WHEN (NEW.fir_number IS NULL OR NEW.fir_number = '')
    EXECUTE FUNCTION generate_fir_number();

CREATE TRIGGER trg_generate_evidence_number
    BEFORE INSERT ON Evidence
    FOR EACH ROW
    WHEN (NEW.evidence_number IS NULL OR NEW.evidence_number = '')
    EXECUTE FUNCTION generate_evidence_number();

CREATE TRIGGER trg_generate_arrest_number
    BEFORE INSERT ON Arrests
    FOR EACH ROW
    WHEN (NEW.arrest_number IS NULL OR NEW.arrest_number = '')
    EXECUTE FUNCTION generate_arrest_number();

CREATE TRIGGER trg_generate_warrant_number
    BEFORE INSERT ON Warrants
    FOR EACH ROW
    WHEN (NEW.warrant_number IS NULL OR NEW.warrant_number = '')
    EXECUTE FUNCTION generate_warrant_number();

CREATE TRIGGER trg_generate_bail_number
    BEFORE INSERT ON Bail_Records
    FOR EACH ROW
    WHEN (NEW.bail_number IS NULL OR NEW.bail_number = '')
    EXECUTE FUNCTION generate_bail_number();

CREATE TRIGGER trg_generate_forensic_request_number
    BEFORE INSERT ON Forensic_Lab_Requests
    FOR EACH ROW
    WHEN (NEW.request_number IS NULL OR NEW.request_number = '')
    EXECUTE FUNCTION generate_forensic_request_number();

CREATE TRIGGER trg_generate_charge_sheet_number
    BEFORE INSERT ON charge_sheets
    FOR EACH ROW
    WHEN (NEW.charge_sheet_number IS NULL OR NEW.charge_sheet_number = '')
    EXECUTE FUNCTION generate_charge_sheet_number();

CREATE TRIGGER trg_generate_duty_number
    BEFORE INSERT ON Duty_Roster
    FOR EACH ROW
    WHEN (NEW.duty_number IS NULL OR NEW.duty_number = '')
    EXECUTE FUNCTION generate_duty_number();


-- ============================================================
-- SECTION 2: VALIDATION TRIGGERS
-- BEFORE INSERT / UPDATE — enforce business rules
-- ============================================================

-- FIR filing rank: only ASI, SI, INSPECTOR
CREATE TRIGGER trg_validate_fir_filing_rank
    BEFORE INSERT ON Cases
    FOR EACH ROW
    EXECUTE FUNCTION validate_fir_filing_rank();

-- Warrant request rank: only INSPECTOR and above
CREATE TRIGGER trg_validate_warrant_request_rank
    BEFORE INSERT ON Warrants
    FOR EACH ROW
    EXECUTE FUNCTION validate_warrant_request_rank();

-- Case closure approval rank: only INSPECTOR (SHO) and above
CREATE TRIGGER trg_validate_case_closure_rank
    BEFORE UPDATE ON Cases
    FOR EACH ROW
    EXECUTE FUNCTION validate_case_closure_rank();

-- Officer duty status: block assignment if ON_LEAVE/SUSPENDED/ABSENT
CREATE TRIGGER trg_validate_officer_duty_status
    BEFORE INSERT ON Case_Officers
    FOR EACH ROW
    EXECUTE FUNCTION validate_officer_duty_status();

-- Active case limit: max 10 active cases per officer
CREATE TRIGGER trg_validate_officer_case_limit
    BEFORE INSERT ON Case_Officers
    FOR EACH ROW
    EXECUTE FUNCTION validate_officer_case_limit();

-- Patrol route station: route must belong to duty station
CREATE TRIGGER trg_validate_patrol_route_station
    BEFORE INSERT OR UPDATE ON Duty_Roster
    FOR EACH ROW
    EXECUTE FUNCTION validate_patrol_route_station();


-- ============================================================
-- SECTION 3: IMMUTABILITY TRIGGERS
-- Protect legal records from modification or deletion
-- ============================================================

-- Hard delete on Evidence is forbidden
-- Soft delete only (is_deleted = TRUE)
CREATE TRIGGER trg_prevent_evidence_hard_delete
    BEFORE DELETE ON Evidence
    FOR EACH ROW
    EXECUTE FUNCTION prevent_evidence_hard_delete();

-- Audit log rows can never be deleted
CREATE TRIGGER trg_prevent_audit_log_delete
    BEFORE DELETE ON audit.Audit_Log
    FOR EACH ROW
    EXECUTE FUNCTION prevent_audit_log_delete();

-- Audit log rows can never be modified
CREATE TRIGGER trg_prevent_audit_log_update
    BEFORE UPDATE ON audit.Audit_Log
    FOR EACH ROW
    EXECUTE FUNCTION prevent_audit_log_update();

-- Locked charge sheets cannot be modified
CREATE TRIGGER trg_prevent_locked_charge_sheet_update
    BEFORE UPDATE ON charge_sheets
    FOR EACH ROW
    EXECUTE FUNCTION prevent_locked_charge_sheet_update();


-- ============================================================
-- SECTION 4: STATE SYNC TRIGGERS
-- Keep related records automatically in sync
-- ============================================================

-- Evidence → SENT_TO_LAB when added to forensic request
CREATE TRIGGER trg_sync_evidence_sent_to_lab
    AFTER INSERT ON Forensic_Request_Evidence
    FOR EACH ROW
    EXECUTE FUNCTION sync_evidence_sent_to_lab();

-- Evidence → RETURNED_FROM_LAB when forensic request delivered
CREATE TRIGGER trg_sync_evidence_returned_from_lab
    AFTER UPDATE ON Forensic_Lab_Requests
    FOR EACH ROW
    EXECUTE FUNCTION sync_evidence_returned_from_lab();

-- Auto-lock charge sheet on SUBMITTED_TO_COURT
-- runs BEFORE UPDATE so NEW values can still be modified
CREATE TRIGGER trg_auto_lock_charge_sheet
    BEFORE UPDATE ON charge_sheets
    FOR EACH ROW
    EXECUTE FUNCTION auto_lock_charge_sheet();

-- Log every case status transition automatically
CREATE TRIGGER trg_log_case_status_change
    AFTER UPDATE ON Cases
    FOR EACH ROW
    EXECUTE FUNCTION log_case_status_change();


-- ============================================================
-- SECTION 5: AUDIT TRIGGERS
-- AFTER INSERT/UPDATE/DELETE on all 8 audited tables
-- Calls audit.log_change() — SECURITY DEFINER
-- ============================================================

CREATE TRIGGER trg_audit_cases
    AFTER INSERT OR UPDATE OR DELETE ON Cases
    FOR EACH ROW
    EXECUTE FUNCTION audit.log_change();

CREATE TRIGGER trg_audit_evidence
    AFTER INSERT OR UPDATE OR DELETE ON Evidence
    FOR EACH ROW
    EXECUTE FUNCTION audit.log_change();

CREATE TRIGGER trg_audit_officers
    AFTER INSERT OR UPDATE OR DELETE ON Officers
    FOR EACH ROW
    EXECUTE FUNCTION audit.log_change();

CREATE TRIGGER trg_audit_arrests
    AFTER INSERT OR UPDATE OR DELETE ON Arrests
    FOR EACH ROW
    EXECUTE FUNCTION audit.log_change();

CREATE TRIGGER trg_audit_warrants
    AFTER INSERT OR UPDATE OR DELETE ON Warrants
    FOR EACH ROW
    EXECUTE FUNCTION audit.log_change();

CREATE TRIGGER trg_audit_charge_sheets
    AFTER INSERT OR UPDATE OR DELETE ON charge_sheets
    FOR EACH ROW
    EXECUTE FUNCTION audit.log_change();

CREATE TRIGGER trg_audit_bail_records
    AFTER INSERT OR UPDATE OR DELETE ON Bail_Records
    FOR EACH ROW
    EXECUTE FUNCTION audit.log_change();

CREATE TRIGGER trg_audit_accused
    AFTER INSERT OR UPDATE OR DELETE ON Accused
    FOR EACH ROW
    EXECUTE FUNCTION audit.log_change();


-- ============================================================
-- TRIGGER EXECUTION ORDER NOTE
-- ============================================================
-- For Cases INSERT, triggers fire in this order:
--   1. trg_generate_fir_number        (BEFORE)
--   2. trg_validate_fir_filing_rank   (BEFORE)
--   3. row is written to disk
--   4. trg_log_case_status_change     (AFTER)
--   5. trg_audit_cases                (AFTER)
--
-- PostgreSQL fires BEFORE triggers alphabetically,
-- then AFTER triggers alphabetically.
-- All trigger names are prefixed trg_ to keep ordering clean.
-- ============================================================