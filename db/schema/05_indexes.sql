-- ============================================================
-- JusticeFlow — 05_indexes.sql
-- Performance indexes for public, audit, and analytics schemas
-- Run after: 04_triggers.sql
-- ============================================================

-- already covered by the UNIQUE constraint, but explicit for clarity
CREATE INDEX idx_sequence_registry_lookup
    ON Sequence_Registry (entity, scope_key);

-- ============================================================
-- SECTION 1: STATIONS
-- ============================================================

CREATE INDEX idx_stations_station_code
    ON Stations (station_code);

CREATE INDEX idx_stations_district
    ON Stations (district);

CREATE INDEX idx_stations_parent
    ON Stations (parent_station_id)
    WHERE parent_station_id IS NOT NULL;


-- ============================================================
-- SECTION 2: OFFICERS
-- ============================================================

CREATE INDEX idx_officers_cnic
    ON Officers (cnic);

CREATE INDEX idx_officers_station_id
    ON Officers (station_id);

CREATE INDEX idx_officers_current_rank
    ON Officers (current_rank);

CREATE INDEX idx_officers_status
    ON Officers (status);

-- composite: station + rank — used by workload balancer
CREATE INDEX idx_officers_station_rank
    ON Officers (station_id, current_rank);


-- ============================================================
-- SECTION 3: CASES
-- Central table — most queried in the system
-- ============================================================

CREATE INDEX idx_cases_fir_number
    ON Cases (fir_number);

CREATE INDEX idx_cases_station_id
    ON Cases (station_id);

CREATE INDEX idx_cases_case_status
    ON Cases (case_status);

CREATE INDEX idx_cases_case_type
    ON Cases (case_type);

CREATE INDEX idx_cases_filed_at
    ON Cases (filed_at DESC);

CREATE INDEX idx_cases_incident_date
    ON Cases (incident_date DESC);

CREATE INDEX idx_cases_primary_complainant
    ON Cases (primary_complainant_cnic);

CREATE INDEX idx_cases_filed_by
    ON Cases (filed_by);

CREATE INDEX idx_cases_lead_officer
    ON Cases (lead_officer_id)
    WHERE lead_officer_id IS NOT NULL;

-- composite: station + status — dashboard filter
CREATE INDEX idx_cases_station_status
    ON Cases (station_id, case_status);

-- composite: status + filed_at — priority sorting
CREATE INDEX idx_cases_status_filed
    ON Cases (case_status, filed_at DESC);

-- spatial: hotspot detection reads these two columns together
CREATE INDEX idx_cases_lat_lon
    ON Cases (incident_lat, incident_lon)
    WHERE incident_lat IS NOT NULL
    AND   incident_lon IS NOT NULL;


-- ============================================================
-- SECTION 4: CASE ROLE TABLES
-- ============================================================

CREATE INDEX idx_case_officers_officer_id
    ON Case_Officers (officer_id);

CREATE INDEX idx_case_officers_case_id
    ON Case_Officers (case_id);

-- active assignments only — used by case limit check
CREATE INDEX idx_case_officers_active
    ON Case_Officers (officer_id, case_id)
    WHERE relieved_at IS NULL;

CREATE INDEX idx_complainants_case_id
    ON Complainants (case_id);

CREATE INDEX idx_complainants_person_cnic
    ON Complainants (person_cnic);

CREATE INDEX idx_victims_case_id
    ON Victims (case_id);

CREATE INDEX idx_victims_person_cnic
    ON Victims (person_cnic);

CREATE INDEX idx_witnesses_case_id
    ON Witnesses (case_id);

CREATE INDEX idx_witnesses_person_cnic
    ON Witnesses (person_cnic);

CREATE INDEX idx_accused_case_id
    ON Accused (case_id);

CREATE INDEX idx_accused_person_cnic
    ON Accused (person_cnic);

CREATE INDEX idx_accused_involvement_type
    ON Accused (involvement_type);


-- ============================================================
-- SECTION 5: EVIDENCE
-- ============================================================

CREATE INDEX idx_evidence_case_id
    ON Evidence (case_id);

CREATE INDEX idx_evidence_evidence_number
    ON Evidence (evidence_number);

CREATE INDEX idx_evidence_evidence_type
    ON Evidence (evidence_type);

CREATE INDEX idx_evidence_evidence_status
    ON Evidence (evidence_status);

CREATE INDEX idx_evidence_collected_by
    ON Evidence (collected_by);

-- active evidence only — excludes soft-deleted rows
CREATE INDEX idx_evidence_active
    ON Evidence (case_id, evidence_status)
    WHERE is_deleted = FALSE;

CREATE INDEX idx_evidence_custody_log_evidence_id
    ON Evidence_Custody_Log (evidence_id);

CREATE INDEX idx_evidence_custody_log_transferred_to
    ON Evidence_Custody_Log (transferred_to);


-- ============================================================
-- SECTION 6: ARRESTS & WARRANTS
-- ============================================================

CREATE INDEX idx_arrests_accused_cnic
    ON Arrests (accused_cnic);

CREATE INDEX idx_arrests_case_id
    ON Arrests (case_id);

CREATE INDEX idx_arrests_custody_status
    ON Arrests (custody_status);

CREATE INDEX idx_arrests_arrested_at
    ON Arrests (arrested_at DESC);

CREATE INDEX idx_warrants_case_id
    ON Warrants (case_id);

CREATE INDEX idx_warrants_accused_cnic
    ON Warrants (accused_cnic)
    WHERE accused_cnic IS NOT NULL;

CREATE INDEX idx_warrants_warrant_status
    ON Warrants (warrant_status);

-- expiry function reads this every night
CREATE INDEX idx_warrants_expiry_check
    ON Warrants (valid_until, warrant_status)
    WHERE warrant_status = 'ISSUED';


-- ============================================================
-- SECTION 7: BAIL RECORDS
-- ============================================================

CREATE INDEX idx_bail_records_arrest_id
    ON Bail_Records (arrest_id);

CREATE INDEX idx_bail_records_bail_status
    ON Bail_Records (bail_status);

-- expiry function reads this every night
CREATE INDEX idx_bail_records_expiry_check
    ON Bail_Records (valid_until, bail_status)
    WHERE bail_status = 'ACTIVE'
    AND   valid_until IS NOT NULL;


-- ============================================================
-- SECTION 8: FORENSIC LAB
-- ============================================================

CREATE INDEX idx_forensic_requests_case_id
    ON Forensic_Lab_Requests (case_id);

CREATE INDEX idx_forensic_requests_status
    ON Forensic_Lab_Requests (request_status);

CREATE INDEX idx_forensic_request_evidence_evidence_id
    ON Forensic_Request_Evidence (evidence_id);


-- ============================================================
-- SECTION 9: CHARGE SHEETS
-- ============================================================

CREATE INDEX idx_charge_sheets_case_id
    ON charge_sheets (case_id);

CREATE INDEX idx_charge_sheets_status
    ON charge_sheets (charge_sheet_status);

CREATE INDEX idx_charge_sheets_filed_by
    ON charge_sheets (filed_by);

CREATE INDEX idx_charge_sheet_accused_cnic
    ON charge_sheet_accused (accused_cnic);


-- ============================================================
-- SECTION 10: VEHICLES
-- ============================================================

CREATE INDEX idx_vehicles_registration_number
    ON vehicles (registration_number);

CREATE INDEX idx_vehicles_seizure_status
    ON vehicles (seizure_status);

CREATE INDEX idx_vehicles_registered_owner_cnic
    ON vehicles (registered_owner_cnic)
    WHERE registered_owner_cnic IS NOT NULL;

CREATE INDEX idx_vehicle_cases_case_id
    ON Vehicle_cases (case_id);

CREATE INDEX idx_vehicle_cases_vehicle_id
    ON Vehicle_cases (vehicle_id);


-- ============================================================
-- SECTION 11: DUTY ROSTER & PATROL
-- ============================================================

CREATE INDEX idx_duty_roster_officer_id
    ON Duty_Roster (officer_id);

CREATE INDEX idx_duty_roster_station_id
    ON Duty_Roster (station_id);

CREATE INDEX idx_duty_roster_duty_date
    ON Duty_Roster (duty_date DESC);

CREATE INDEX idx_duty_roster_duty_status
    ON Duty_Roster (duty_status);

-- today's duty lookup — used by officer duty status validation
CREATE INDEX idx_duty_roster_today
    ON Duty_Roster (officer_id, duty_date, duty_status);

CREATE INDEX idx_patrol_routes_station_id
    ON Patrol_Routes (station_id);

CREATE INDEX idx_patrol_routes_beat_code
    ON Patrol_Routes (beat_code);


-- ============================================================
-- SECTION 12: AUDIT SCHEMA
-- ============================================================

CREATE INDEX idx_audit_log_table_record
    ON audit.Audit_Log (table_name, record_id);

CREATE INDEX idx_audit_log_officer_id
    ON audit.Audit_Log (changed_by_officer_id)
    WHERE changed_by_officer_id IS NOT NULL;

CREATE INDEX idx_audit_log_changed_at
    ON audit.Audit_Log (changed_at DESC);

CREATE INDEX idx_audit_log_action
    ON audit.Audit_Log (action);


-- ============================================================
-- SECTION 13: ANALYTICS SCHEMA
-- ============================================================

-- Crime hotspots — dashboard reads latest by risk
CREATE INDEX idx_hotspots_analyzed_at
    ON analytics.Crime_Hotspots (analyzed_at DESC);

CREATE INDEX idx_hotspots_risk_level
    ON analytics.Crime_Hotspots (risk_level, risk_score DESC);

-- Case priority — dashboard reads latest score per case
CREATE INDEX idx_priority_case_id
    ON analytics.Case_Priority_Scores (case_id);

CREATE INDEX idx_priority_score_desc
    ON analytics.Case_Priority_Scores (priority_score DESC);

CREATE INDEX idx_priority_analyzed_at
    ON analytics.Case_Priority_Scores (analyzed_at DESC);

-- Workload assignments — SHO reads pending suggestions
CREATE INDEX idx_workload_officer_id
    ON analytics.Officer_Workload_Assignments (officer_id);

CREATE INDEX idx_workload_case_id
    ON analytics.Officer_Workload_Assignments (case_id);

CREATE INDEX idx_workload_status
    ON analytics.Officer_Workload_Assignments (assignment_status);

-- expiry function reads this hourly
CREATE INDEX idx_workload_expiry_check
    ON analytics.Officer_Workload_Assignments (expires_at, assignment_status)
    WHERE assignment_status = 'SUGGESTED';