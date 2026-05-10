# JusticeFlow — Database Design Report

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Database Architecture](#2-database-architecture)
3. [Enum Types](#3-enum-types)
4. [Schema: `public` — Core Tables](#4-schema-public--core-tables)
   - 4.1 [Infrastructure Tables](#41-infrastructure-tables)
   - 4.2 [Case & Investigation Tables](#42-case--investigation-tables)
   - 4.3 [Legal Proceedings Tables](#43-legal-proceedings-tables)
   - 4.4 [Operational Tables](#44-operational-tables)
   - 4.5 [Utility Tables](#45-utility-tables)
5. [Schema: `audit` — Audit Trail](#5-schema-audit--audit-trail)
6. [Schema: `analytics` — AI Agent Outputs](#6-schema-analytics--ai-agent-outputs)
7. [Stored Functions](#7-stored-functions)
8. [Triggers](#8-triggers)
9. [Indexes](#9-indexes)
10. [Roles & Permissions](#10-roles--permissions)
11. [Schema Patches](#11-schema-patches)
12. [Data Population](#12-data-population)
13. [Screenshots](#13-screenshots)

---

## 1. Project Overview

**JusticeFlow** is a police case management system designed to digitize and enforce the workflow of criminal case registration, investigation, evidence handling, legal proceedings, and officer duty management across a hierarchical network of police stations.

The system is built on **PostgreSQL** and is structured to guarantee:

- Legal immutability of audit trails, evidence records, and submitted charge sheets
- Rank-based access control enforced at the database layer via triggers
- Full traceability of every change to critical records
- AI-assisted decision support through a segregated analytics schema

---

## 2. Database Architecture

The database is organized into three distinct schemas, each with a clearly defined responsibility boundary.

| Schema      | Purpose                                                                       | Write Access                                |
| ----------- | ----------------------------------------------------------------------------- | ------------------------------------------- |
| `public`    | Core application tables — cases, officers, evidence, arrests, etc.            | `justice_app` (application role)            |
| `audit`     | Append-only legal audit trail — immutable change log                          | Trigger functions only (`SECURITY DEFINER`) |
| `analytics` | AI agent prediction outputs — hotspots, priority scores, workload suggestions | `justice_ai` (AI agent role)                |

This separation ensures that AI agents cannot touch operational data, and no application user can tamper with audit records directly.

---

## 3. Enum Types

All domain-constrained string columns use PostgreSQL `ENUM` types, defined in `01_types.sql`. This provides type safety at the database level without requiring application-layer validation.

### Station & Officer Enums

| Enum                  | Values                                                                                               |
| --------------------- | ---------------------------------------------------------------------------------------------------- |
| `station_type_enum`   | `ZONE_HQ`, `DISTRICT_HQ`, `DIVISION_HQ`, `POLICE_STATION`, `SUB_STATION`                             |
| `officer_rank_enum`   | `CONSTABLE`, `HEAD_CONSTABLE`, `ASI`, `SI`, `INSPECTOR`, `DSP`, `SP`, `SSP`, `DIG`, `ADDL_IG`, `IGP` |
| `officer_status_enum` | `ACTIVE`, `SUSPENDED`, `ON_LEAVE`, `RETIRED`, `TERMINATED`                                           |

### Case Enums

| Enum                     | Values                                                                                                                                                                                                                        |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `case_type_enum`         | 30 types — Criminal (20): `MURDER`, `ATTEMPTED_MURDER`, `KIDNAPPING`, `ROBBERY`, `RAPE`, `DRUG_TRAFFICKING`, `TERRORISM`, etc. Civilian (10): `THEFT`, `FRAUD`, `CYBERCRIME`, `DOMESTIC_VIOLENCE`, `PUBLIC_DISTURBANCE`, etc. |
| `case_status_enum`       | `REGISTERED`, `UNDER_INVESTIGATION`, `EVIDENCE_COLLECTED`, `PENDING_TRIAL`, `CLOSED`, `REOPENED`                                                                                                                              |
| `approval_status_enum`   | `NOT_REQUIRED`, `PENDING_APPROVAL`, `APPROVED`, `REJECTED`                                                                                                                                                                    |
| `case_officer_role_enum` | `DUTY_INCHARGE`, `SIO`, `IO`, `LEAD_INVESTIGATOR`, `SUPPORTING`, `EVIDENCE_CUSTODIAN`                                                                                                                                         |

### Person Enums

| Enum                          | Values                                                                                        |
| ----------------------------- | --------------------------------------------------------------------------------------------- |
| `gender_enum`                 | `MALE`, `FEMALE`, `OTHER`                                                                     |
| `relationship_to_victim_enum` | `SELF`, `PARENT`, `SPOUSE`, `SIBLING`, `CHILD`, `RELATIVE`, `WITNESS`, `THIRD_PARTY`, `OTHER` |
| `complainant_status_enum`     | `ACTIVE`, `WITHDRAWN`, `DECEASED`, `UNREACHABLE`                                              |
| `injury_severity_enum`        | `NONE`, `MINOR`, `MODERATE`, `SEVERE`, `FATAL`                                                |
| `vulnerability_category_enum` | `NONE`, `MINOR`, `ELDERLY`, `DIFFERENTLY_ABLED`, `FEMALE_ALONE`                               |
| `involvement_type_enum`       | `SUSPECT`, `ACCUSED`, `CONVICTED`, `ACQUITTED`                                                |

### Evidence & Legal Enums

| Enum                           | Values                                                                                               |
| ------------------------------ | ---------------------------------------------------------------------------------------------------- |
| `evidence_type_enum`           | `PHYSICAL`, `DIGITAL`, `TESTIMONIAL`, `FORENSIC`, `DOCUMENTARY`                                      |
| `evidence_status_enum`         | `RECEIVED`, `SEALED`, `SENT_TO_LAB`, `RETURNED_FROM_LAB`, `PRODUCED_IN_COURT`, `DISPOSED`            |
| `warrant_type_enum`            | `ARREST`, `SEARCH`, `SEIZURE`                                                                        |
| `warrant_status_enum`          | `ISSUED`, `EXECUTED`, `CANCELLED`, `EXPIRED`                                                         |
| `bail_type_enum`               | `REGULAR`, `ANTICIPATORY`, `INTERIM`, `SURETY`                                                       |
| `bail_status_enum`             | `ACTIVE`, `REVOKED`, `EXPIRED`, `CANCELLED`                                                          |
| `custody_status_enum`          | `IN_CUSTODY`, `BAIL_GRANTED`, `REMANDED`, `RELEASED`, `ESCAPED`                                      |
| `charge_sheet_status_enum`     | `DRAFT`, `FILED`, `SUBMITTED_TO_COURT`, `ACCEPTED_BY_COURT`, `REJECTED_BY_COURT`                     |
| `forsenic_request_status_enum` | `REQUESTED`, `RECEIVED_BY_LAB`, `UNDER_EXAMINATION`, `REPORT_READY`, `REPORT_DELIVERED`, `CONTESTED` |
| `examination_result_enum`      | `INCONCLUSIVE`, `MATCH_FOUND`, `NO_MATCH`, `PARTIAL_MATCH`, `EXCLUDED`, `PENDING`                    |

### Analytics Enums

| Enum                      | Values                                              |
| ------------------------- | --------------------------------------------------- |
| `hotspot_risk_level_enum` | `LOW`, `MEDIUM`, `HIGH`, `CRITICAL`                 |
| `priority_level_enum`     | `LOW`, `MEDIUM`, `HIGH`, `CRITICAL`                 |
| `assignment_status_enum`  | `SUGGESTED`, `ACCEPTED`, `REJECTED`, `AUTO_EXPIRED` |

### Audit Enums

| Enum                 | Values                                                                                             |
| -------------------- | -------------------------------------------------------------------------------------------------- |
| `audit_action_enum`  | `INSERT`, `UPDATE`, `DELETE`                                                                       |
| `audited_table_enum` | `cases`, `evidence`, `officers`, `arrests`, `warrants`, `charge_sheets`, `bail_records`, `accused` |

---

## 4. Schema: `public` — Core Tables

### 4.1 Infrastructure Tables

#### `Stations`

Represents the hierarchical structure of police stations. Self-referencing via `parent_station_id` to model the zone → district → division → station → sub-station hierarchy.

| Column                     | Type                   | Description                                                  |
| -------------------------- | ---------------------- | ------------------------------------------------------------ |
| `station_id`               | `BIGSERIAL PK`         | Surrogate primary key                                        |
| `station_code`             | `VARCHAR(20) UNIQUE`   | Short identifier used in all generated numbers (e.g., `KHD`) |
| `station_name`             | `VARCHAR(100)`         | Full station name                                            |
| `station_type`             | `station_type_enum`    | Hierarchy level                                              |
| `parent_station_id`        | `BIGINT FK → Stations` | Parent station (nullable for top-level zones)                |
| `district`, `zone`, `city` | `VARCHAR`              | Geographic identifiers                                       |
| `is_active`                | `BOOLEAN`              | Soft deactivation flag                                       |

#### `Persons`

Central identity table for all human entities in the system (officers, complainants, victims, witnesses, accused). CNIC is the universal identifier.

| Column                                 | Type             | Description                                                      |
| -------------------------------------- | ---------------- | ---------------------------------------------------------------- |
| `cnic`                                 | `VARCHAR(15) PK` | Pakistani CNIC — format enforced: `^[0-9]{5}-[0-9]{7}-[0-9]{1}$` |
| `full_name`                            | `VARCHAR(100)`   | Legal name                                                       |
| `gender`                               | `gender_enum`    | —                                                                |
| `dob`                                  | `DATE`           | Date of birth                                                    |
| `mobile`, `email`                      | `VARCHAR`        | Contact information                                              |
| `permanent_address`, `current_address` | `TEXT`           | Addresses                                                        |

#### `Officers`

Extends `Persons` with law enforcement-specific fields. An officer must have a matching `Persons` record (via `cnic` FK).

| Column                        | Type                  | Description                                        |
| ----------------------------- | --------------------- | -------------------------------------------------- |
| `officer_id`                  | `BIGSERIAL PK`        | Surrogate key                                      |
| `belt_number`                 | `VARCHAR(20) UNIQUE`  | Format: `PC-NNNN`, `HC-NNNN`, or `K-NNNN`          |
| `cnic`                        | `FK → Persons`        | Identity link                                      |
| `current_rank`                | `officer_rank_enum`   | Active rank                                        |
| `bps_scale`                   | `SMALLINT`            | Pay scale — constrained to valid BPS levels (7–22) |
| `station_id`                  | `FK → Stations`       | Home station                                       |
| `status`                      | `officer_status_enum` | Active/suspended/retired etc.                      |
| `password_hash`, `last_login` | —                     | Authentication fields                              |

**Constraint:** `chk_retirement_status` — `retirement_date` is required if and only if `status = 'RETIRED'`.

#### `Officer_Rank_History`

Immutable log of rank promotions/changes. Every rank change is recorded here with the old and new values.

#### `Officer_Deployments`

Tracks temporary cross-station assignments. Records the origin station, destination, authorizing officer, and active period.

---

### 4.2 Case & Investigation Tables

#### `Cases`

The central table of the system. Every FIR registered creates one row here. All other tables are either directly or indirectly linked to a case.

| Column                                        | Type                   | Description                                 |
| --------------------------------------------- | ---------------------- | ------------------------------------------- |
| `case_id`                                     | `BIGSERIAL PK`         | Surrogate key                               |
| `fir_number`                                  | `VARCHAR(30) UNIQUE`   | Auto-generated: `FIR-YYYY-STATIONCODE-NNNN` |
| `case_type`                                   | `case_type_enum`       | Crime classification                        |
| `case_status`                                 | `case_status_enum`     | Current workflow state                      |
| `incident_date`                               | `TIMESTAMPTZ`          | Must be ≤ NOW()                             |
| `incident_lat`, `incident_lon`                | `DECIMAL(9,6)`         | GPS coordinates for hotspot detection       |
| `station_id`                                  | `FK → Stations`        | Registering station                         |
| `filed_by`                                    | `FK → Officers`        | Filing officer (rank-validated by trigger)  |
| `lead_officer_id`                             | `FK → Officers`        | Nullable — assigned investigator            |
| `parent_case_id`                              | `FK → Cases`           | Self-referencing — links related cases      |
| `closed_at`, `closure_reason`                 | —                      | Closure details                             |
| `approval_status`                             | `approval_status_enum` | High-rank approval for closure              |
| `reopened_by`, `reopened_at`, `reopen_reason` | —                      | Reopen tracking                             |

**Key Constraints:**

- `chk_closed_requires_reason` — closure requires a reason
- `chk_no_self_parent` — a case cannot be its own parent

#### `Case_Officers`

Junction table linking officers to cases with specific roles. An officer can hold multiple roles on the same case simultaneously (e.g., `SIO` + `EVIDENCE_CUSTODIAN`).

**Primary Key:** `(case_id, officer_id, role)` — composite, prevents duplicate role assignment.

#### `Case_Status_Log`

Append-only log of every case status transition. Populated automatically by the `log_case_status_change` trigger.

#### `Case_Jurisdiction_History`

Records inter-station case transfers. Ensures accountability when a case moves jurisdiction.

#### `Complainants`

Links persons to cases as complainants. Supports withdrawal tracking with a mandatory `withdrawal_reason` when `withdrawn_at` is set.

#### `Victims`

Records victim details per case, including injury severity and vulnerability classification — used by the case priority AI agent.

#### `Witnesses`

Records witness statements (text or file path) and protection status. Supports identity concealment for sensitive witnesses.

#### `Accused`

Links persons to cases as accused. Supports alias tracking via `master_accused_cnic` — if an accused is known by multiple identities, their canonical CNIC is stored here.

#### `Accused_Associations`

Models gang networks and co-accused relationships. Self-referencing via `accused_id` — cannot self-associate.

---

### 4.3 Legal Proceedings Tables

#### `Evidence`

All physical, digital, forensic, testimonial, and documentary evidence linked to cases.

| Column                                        | Type                   | Description                                          |
| --------------------------------------------- | ---------------------- | ---------------------------------------------------- |
| `evidence_id`                                 | `BIGSERIAL PK`         | —                                                    |
| `evidence_number`                             | `VARCHAR(30) UNIQUE`   | Auto-generated: `EVD-FIR-YYYY-STATIONCODE-NNNN-NNNN` |
| `evidence_type`                               | `evidence_type_enum`   | Classification                                       |
| `evidence_status`                             | `evidence_status_enum` | Lifecycle state                                      |
| `file_path`                                   | `VARCHAR(255)`         | Required for DIGITAL and DOCUMENTARY types           |
| `is_deleted`                                  | `BOOLEAN`              | Soft delete flag — hard delete is trigger-blocked    |
| `deleted_at`, `deleted_by`, `deletion_reason` | —                      | Mandatory when `is_deleted = TRUE`                   |

**Key Design:** Evidence is **never physically deleted**. A `BEFORE DELETE` trigger raises an exception on any `DELETE` statement. Only soft deletion (`is_deleted = TRUE`) is permitted.

#### `Evidence_Custody_Log`

Permanent chain-of-custody record. Every transfer of evidence between officers is logged here with the reason and status at time of transfer.

#### `Warrants`

Tracks arrest, search, and seizure warrants issued by courts.

| Key Constraints                       | Description                                     |
| ------------------------------------- | ----------------------------------------------- |
| `chk_valid_until_after_issue`         | Validity window must be in the future           |
| `chk_not_both_executed_and_cancelled` | A warrant cannot be both executed and cancelled |
| `chk_search_warrant_needs_address`    | Non-arrest warrants require a target address    |
| `chk_cancellation_requires_reason`    | Cancellation requires officer + reason          |

#### `Arrests`

Records each arrest event with links to the accused person, case, and optional triggering warrant.

#### `Bail_Records`

Records bail granted following an arrest. Supports all bail types with surety details and automatic expiry via scheduled function.

#### `Forensic_Lab_Requests`

Tracks evidence sent to forensic laboratories. Supports amendment and contestation workflows.

#### `Forensic_Request_Evidence`

Junction table linking specific evidence items to a forensic request.

#### `charge_sheets`

Formal charge documents filed against accused persons.

| Column                | Description                                                       |
| --------------------- | ----------------------------------------------------------------- |
| `charge_sheet_number` | Auto-generated: `CS-YYYY-STATIONCODE-NNNN`                        |
| `sheet_type`          | `ORIGINAL` or `SUPPLEMENTARY`                                     |
| `laws_invoked`        | `TEXT[]` — must be non-empty unless `DRAFT`                       |
| `is_locked`           | Set automatically on `SUBMITTED_TO_COURT` — immutable once locked |

#### `charge_sheet_accused`

Links accused persons to charge sheets with their specific charges as a `TEXT[]` array.

---

### 4.4 Operational Tables

#### `Vehicles`

Registry of vehicles involved in cases. Tracks seizure status, owner details, and release.

#### `Vehicle_Cases`

Junction table linking vehicles to cases with a specific role (stolen, used in crime, etc.).

#### `Patrol_Routes`

Defines named patrol beats anchored to a station. Each route has an area description, landmarks array, and a unique beat code.

#### `Duty_Roster`

Schedules officer duties with shift type, patrol route assignment, and actual vs. scheduled time tracking.

| Key Constraints                 | Description                     |
| ------------------------------- | ------------------------------- |
| `chk_scheduled_end_after_start` | Shift must end after it starts  |
| `chk_absence_requires_reason`   | Absent status requires a reason |

---

### 4.5 Utility Tables

#### `Sequence_Registry`

Central sequence counter used by all number generators. A `(entity, scope_key)` pair maps to an atomically-incremented integer, ensuring unique, gap-free numbers per station per year.

**Example scopes:**

| Entity | Scope Key           | Generated Number             |
| ------ | ------------------- | ---------------------------- |
| `FIR`  | `KHD-2025`          | `FIR-2025-KHD-0001`          |
| `ARR`  | `KHD-2025`          | `ARR-2025-KHD-0042`          |
| `EVD`  | `FIR-2025-KHD-0001` | `EVD-FIR-2025-KHD-0001-0003` |

---

## 5. Schema: `audit` — Audit Trail

#### `audit.Audit_Log`

Immutable, append-only log of all changes to 8 critical tables: `cases`, `evidence`, `officers`, `arrests`, `warrants`, `charge_sheets`, `bail_records`, `accused`.

| Column                  | Description                                                                        |
| ----------------------- | ---------------------------------------------------------------------------------- |
| `table_name`            | `audited_table_enum` — which table changed                                         |
| `record_id`             | PK of the changed record                                                           |
| `action`                | `INSERT`, `UPDATE`, or `DELETE`                                                    |
| `old_value`             | Full row snapshot as JSONB (NULL on INSERT)                                        |
| `new_value`             | Full row snapshot as JSONB (NULL on DELETE)                                        |
| `changed_by_user`       | PostgreSQL session user (e.g., `justice_app`)                                      |
| `changed_by_officer_id` | Application-layer officer, resolved from `app.current_officer_id` session variable |
| `changed_by_belt`       | Denormalized belt number — no JOIN needed for reading                              |
| `client_process_id`     | OS-level PID of the connecting process                                             |
| `client_ip`             | Client IP address via `inet_client_addr()`                                         |

**Immutability guarantees:**

- `trg_prevent_audit_log_delete` — raises exception on any `DELETE`
- `trg_prevent_audit_log_update` — raises exception on any `UPDATE`

No application role has direct write access. Only the `audit.log_change()` `SECURITY DEFINER` trigger function can insert rows.

---

## 6. Schema: `analytics` — AI Agent Outputs

Three AI agents produce predictions stored in this schema. The `justice_ai` role has `INSERT`-only access — it cannot update or delete its own predictions.

#### `analytics.Crime_Hotspots`

Output of **AI Agent 1: DBSCAN Crime Hotspot Analyzer**. Populated on a scheduled basis by a Python ML process.

| Key Column                 | Description                                         |
| -------------------------- | --------------------------------------------------- |
| `center_lat`, `center_lon` | Geographic center of the crime cluster              |
| `radius_meters`            | Approximate coverage radius                         |
| `case_count`               | Number of cases forming this cluster                |
| `dominant_case_type`       | Most frequent crime type                            |
| `case_type_breakdown`      | JSONB — e.g., `{"THEFT": 12, "ROBBERY": 8}`         |
| `risk_score`               | `DECIMAL(5,4)` — 0.0 to 1.0                         |
| `patrol_increase_pct`      | Recommended patrol increase percentage              |
| `epsilon`, `min_samples`   | DBSCAN hyperparameters recorded for reproducibility |

#### `analytics.Case_Priority_Scores`

Output of **AI Agent 2: Random Forest Case Priority Recommender**. A case can be re-analyzed multiple times; each run generates a new row.

| Key Column              | Description                                                                                            |
| ----------------------- | ------------------------------------------------------------------------------------------------------ |
| `priority_score`        | `DECIMAL(5,4)` — 0.0 to 1.0                                                                            |
| `feature_contributions` | JSONB SHAP values — explains which features drove the score                                            |
| `top_reason`            | Human-readable explanation (e.g., "Filed 45 days ago with only 1 evidence item")                       |
| `input_features`        | JSONB snapshot of features at analysis time — preserves explainability even if case data later changes |
| `suggested_action`      | Recommended next step                                                                                  |

#### `analytics.Officer_Workload_Assignments`

Output of **AI Agent 3: Hungarian Algorithm Workload Balancer**. Suggestions only — the SHO must accept or reject; no automatic assignment occurs.

| Key Column             | Description                                                                               |
| ---------------------- | ----------------------------------------------------------------------------------------- |
| `cost_score`           | Lower = better fit (from Hungarian algorithm cost matrix)                                 |
| `cost_breakdown`       | JSONB — e.g., `{"workload_penalty": 0.3, "skill_match": 0.5, "geographic_distance": 0.2}` |
| `officer_active_cases` | Snapshot of officer's workload at analysis time                                           |
| `assignment_status`    | `SUGGESTED` → `ACCEPTED` / `REJECTED` / `AUTO_EXPIRED`                                    |
| `expires_at`           | Suggestion auto-expires if not acted upon (set by OS job scheduler)                       |

#### `analytics.Model_Performance_Log`

Tracks model accuracy over time for all three AI agents. Used to trigger retraining when accuracy drops below defined thresholds.

---

## 7. Stored Functions

All functions are defined in `03_functions.sql`.

### Section 1 — Number Generators

Every document type in the system has a unique, human-readable number generated at insert time. A single helper function `get_next_seq(entity, scope_key)` provides the atomic counter using a `SELECT FOR UPDATE` to prevent race conditions in concurrent inserts.

| Function                             | Format                       | Scope                |
| ------------------------------------ | ---------------------------- | -------------------- |
| `generate_fir_number()`              | `FIR-YYYY-STATIONCODE-NNNN`  | Per station per year |
| `generate_evidence_number()`         | `EVD-{FIR_NUMBER}-NNNN`      | Per FIR              |
| `generate_arrest_number()`           | `ARR-YYYY-STATIONCODE-NNNN`  | Per station per year |
| `generate_warrant_number()`          | `WRT-YYYY-STATIONCODE-NNNN`  | Per station per year |
| `generate_bail_number()`             | `BAIL-YYYY-STATIONCODE-NNNN` | Per station per year |
| `generate_forensic_request_number()` | `FLR-YYYY-STATIONCODE-NNNN`  | Per station per year |
| `generate_charge_sheet_number()`     | `CS-YYYY-STATIONCODE-NNNN`   | Per station per year |
| `generate_duty_number()`             | `DUTY-YYYY-STATIONCODE-NNNN` | Per station per year |

### Section 2 — Expiry Functions

Called by the OS layer job scheduler — not by triggers. These run on a cron-like schedule.

| Function                        | Schedule       | Action                                                            |
| ------------------------------- | -------------- | ----------------------------------------------------------------- |
| `expire_warrants()`             | Daily at 00:01 | Marks `ISSUED` warrants past `valid_until` as `EXPIRED`           |
| `expire_bail_records()`         | Daily at 00:01 | Marks `ACTIVE` bail records past `valid_until` as `EXPIRED`       |
| `expire_workload_assignments()` | Hourly         | Marks `SUGGESTED` assignments past `expires_at` as `AUTO_EXPIRED` |

### Section 3 — Validation Functions

These run as `BEFORE INSERT/UPDATE` trigger functions and raise an exception if the business rule is violated, rolling back the operation.

| Function                          | Rule Enforced                                                                                |
| --------------------------------- | -------------------------------------------------------------------------------------------- |
| `validate_fir_filing_rank()`      | Only `ASI`, `SI`, or `INSPECTOR` can file an FIR                                             |
| `validate_warrant_request_rank()` | Only `INSPECTOR` and above can request a warrant                                             |
| `validate_case_closure_rank()`    | Only `INSPECTOR` (SHO) and above can approve case closure                                    |
| `validate_officer_duty_status()`  | Officers who are `SUSPENDED`, `RETIRED`, `ON_LEAVE`, or `ABSENT` cannot be assigned to cases |
| `validate_officer_case_limit()`   | An officer may not hold more than 10 active case assignments simultaneously                  |
| `validate_patrol_route_station()` | A patrol route must belong to the same station as the duty roster entry                      |

### Section 4 — Immutability Functions

| Function                               | Protection                                                         |
| -------------------------------------- | ------------------------------------------------------------------ |
| `prevent_evidence_hard_delete()`       | Hard `DELETE` on `Evidence` raises an exception — soft delete only |
| `prevent_audit_log_delete()`           | `DELETE` on `audit.Audit_Log` raises an exception                  |
| `prevent_audit_log_update()`           | `UPDATE` on `audit.Audit_Log` raises an exception                  |
| `prevent_locked_charge_sheet_update()` | `UPDATE` on a locked charge sheet raises an exception              |

### Section 5 — State Sync Functions

| Function                            | Event                                        | Action                                       |
| ----------------------------------- | -------------------------------------------- | -------------------------------------------- |
| `sync_evidence_sent_to_lab()`       | Evidence added to forensic request           | Sets `evidence_status = 'SENT_TO_LAB'`       |
| `sync_evidence_returned_from_lab()` | Forensic request status → `REPORT_DELIVERED` | Sets linked evidence to `RETURNED_FROM_LAB`  |
| `auto_lock_charge_sheet()`          | Charge sheet status → `SUBMITTED_TO_COURT`   | Sets `is_locked = TRUE`, `locked_at = NOW()` |
| `log_case_status_change()`          | Any `case_status` change                     | Inserts row into `Case_Status_Log`           |

### Section 6 — Audit Function

`audit.log_change()` is a single `SECURITY DEFINER` function attached to all 8 audited tables. It captures the full old and new row as JSONB, resolves the officer from the `app.current_officer_id` session variable (set at login by the application), and records the OS-level process ID and client IP for forensic traceability.

---

## 8. Triggers

All triggers are defined in `04_triggers.sql` with a consistent `trg_` prefix to control PostgreSQL's alphabetical firing order.

### Section 1 — Number Generator Triggers (BEFORE INSERT)

Eight triggers fire `BEFORE INSERT` on their respective tables to populate auto-generated number columns if not already set.

`trg_generate_fir_number`, `trg_generate_evidence_number`, `trg_generate_arrest_number`, `trg_generate_warrant_number`, `trg_generate_bail_number`, `trg_generate_forensic_request_number`, `trg_generate_charge_sheet_number`, `trg_generate_duty_number`

### Section 2 — Validation Triggers (BEFORE INSERT / UPDATE)

Six triggers enforce rank-based and status-based business rules before writes are committed.

`trg_validate_fir_filing_rank`, `trg_validate_warrant_request_rank`, `trg_validate_case_closure_rank`, `trg_validate_officer_duty_status`, `trg_validate_officer_case_limit`, `trg_validate_patrol_route_station`

### Section 3 — Immutability Triggers

Four triggers protect legal records from unauthorized modification.

`trg_prevent_evidence_hard_delete`, `trg_prevent_audit_log_delete`, `trg_prevent_audit_log_update`, `trg_prevent_locked_charge_sheet_update`

### Section 4 — State Sync Triggers (AFTER INSERT / UPDATE)

Four triggers keep related records automatically synchronized.

`trg_sync_evidence_sent_to_lab`, `trg_sync_evidence_returned_from_lab`, `trg_auto_lock_charge_sheet`, `trg_log_case_status_change`

### Section 5 — Audit Triggers (AFTER INSERT / UPDATE / DELETE)

Eight triggers — one per audited table — all call the same `audit.log_change()` function.

`trg_audit_cases`, `trg_audit_evidence`, `trg_audit_officers`, `trg_audit_arrests`, `trg_audit_warrants`, `trg_audit_charge_sheets`, `trg_audit_bail_records`, `trg_audit_accused`

### Trigger Execution Order — Cases INSERT

For a `Cases INSERT`, triggers fire in this sequence:

```
1. trg_generate_fir_number        (BEFORE — alphabetical)
2. trg_validate_fir_filing_rank   (BEFORE — alphabetical)
   ↓ row is written to disk
3. trg_log_case_status_change     (AFTER — alphabetical)
4. trg_audit_cases                (AFTER — alphabetical)
```

---

## 9. Indexes

Defined in `05_indexes.sql` — 60+ indexes across all three schemas, organized by table domain.

### Cases (Most Queried Table)

| Index                      | Columns                                       | Purpose                          |
| -------------------------- | --------------------------------------------- | -------------------------------- |
| `idx_cases_fir_number`     | `fir_number`                                  | Direct FIR lookup                |
| `idx_cases_station_status` | `(station_id, case_status)`                   | Dashboard filter                 |
| `idx_cases_status_filed`   | `(case_status, filed_at DESC)`                | Priority sorting                 |
| `idx_cases_lat_lon`        | `(incident_lat, incident_lon)` WHERE not null | Hotspot detection                |
| `idx_cases_lead_officer`   | `lead_officer_id` WHERE not null              | Partial — skips unassigned cases |

### Officers

| Index                       | Columns                      | Purpose                   |
| --------------------------- | ---------------------------- | ------------------------- |
| `idx_officers_station_rank` | `(station_id, current_rank)` | Workload balancer queries |
| `idx_officers_cnic`         | `cnic`                       | Person lookup             |
| `idx_officers_status`       | `status`                     | Active officer filter     |

### Case_Officers (Active Assignments)

| Index                      | Columns                                             | Purpose          |
| -------------------------- | --------------------------------------------------- | ---------------- |
| `idx_case_officers_active` | `(officer_id, case_id)` WHERE `relieved_at IS NULL` | Case limit check |

### Warrants & Bail (Expiry)

| Index                           | Columns                                                   | Purpose            |
| ------------------------------- | --------------------------------------------------------- | ------------------ |
| `idx_warrants_expiry_check`     | `(valid_until, warrant_status)` WHERE `status = 'ISSUED'` | Nightly expiry job |
| `idx_bail_records_expiry_check` | `(valid_until, bail_status)` WHERE `status = 'ACTIVE'`    | Nightly expiry job |

### Analytics

| Index                       | Columns                                                        | Purpose                |
| --------------------------- | -------------------------------------------------------------- | ---------------------- |
| `idx_hotspots_risk_level`   | `(risk_level, risk_score DESC)`                                | Dashboard risk ranking |
| `idx_priority_score_desc`   | `priority_score DESC`                                          | Highest priority cases |
| `idx_workload_expiry_check` | `(expires_at, assignment_status)` WHERE `status = 'SUGGESTED'` | Hourly expiry job      |

### Audit

| Index                        | Columns                   | Purpose                   |
| ---------------------------- | ------------------------- | ------------------------- |
| `idx_audit_log_table_record` | `(table_name, record_id)` | Record history lookup     |
| `idx_audit_log_changed_at`   | `changed_at DESC`         | Chronological audit reads |

---

## 10. Roles & Permissions

Two application roles are defined, each with a strictly scoped permission set.

### `justice_app` — Application Role

| Schema      | Permission                               |
| ----------- | ---------------------------------------- |
| `public`    | Full read/write (via application)        |
| `audit`     | `SELECT` only on `Audit_Log`             |
| `analytics` | `SELECT` on all tables (dashboard reads) |

### `justice_ai` — AI Agent Role

| Schema      | Permission                                                     |
| ----------- | -------------------------------------------------------------- |
| `public`    | `SELECT` only — AI reads case data for analysis                |
| `audit`     | No access at all                                               |
| `analytics` | `INSERT` only — AI writes predictions; cannot update or delete |

### Audit Schema Protection

The `audit` schema is revoked from `PUBLIC`. No application role can `INSERT`, `UPDATE`, or `DELETE` directly. Only the `audit.log_change()` `SECURITY DEFINER` function can write to it — and this function runs with the elevated privileges of its definer, not the calling session user. This is the core legal guarantee: audit records cannot be suppressed or tampered with by any application path.

---

## 11. Schema Patches

`schema_patch.sql` contains six idempotent fixes applied to the live database. Each fix is wrapped in a `DO $$` block that checks current state before applying, making it safe to run repeatedly.

| Fix       | Issue                                                                                                        | Resolution                                                                       |
| --------- | ------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------- |
| **FIX 1** | `audited_table_enum` had mixed-case values; `TG_TABLE_NAME` always returns lowercase — cast failed           | Recreated enum with all-lowercase values; migrated `Audit_Log.table_name` column |
| **FIX 2** | `persons_cnic_check` used `\d` shorthand — unreliable across PostgreSQL versions                             | Replaced with explicit `[0-9]` character class                                   |
| **FIX 3** | `Accused.master_accused_cnic` was `NOT NULL` — incorrect since it is an optional alias link                  | Changed column to nullable                                                       |
| **FIX 4** | `chk_laws_invoked_not_empty` used `AND` — blocked all non-DRAFT inserts regardless of `laws_invoked` content | Corrected to `OR` — DRAFT sheets are exempt from the non-empty requirement       |
| **FIX 5** | Table was mistakenly named `Bails_Records`                                                                   | Renamed to `Bail_Records`                                                        |
| **FIX 6** | `chk_valid_until_after_bail_date` had no NULL guard — blocked valid records with no expiry date              | Added `valid_until IS NULL OR ...` guard                                         |

A verification block at the end of the patch file prints the current state of all fixed objects to confirm successful application.

---

## 12. Data Population

The database was populated with realistic dummy data using a Python script leveraging two libraries:

- **Faker** — for generating realistic Pakistani names, CNICs, addresses, and contact details
- **NumPy** — for probabilistic distributions (case type frequencies, injury severity distributions, officer rank pyramids, etc.)

### Population Strategy

The data was seeded in dependency order to satisfy all foreign key constraints:

1. **Stations** — hierarchical tree (zones → districts → sub-stations)
2. **Persons** — base identity records for all human entities
3. **Officers** — linked to persons; rank distribution follows a realistic pyramid (more constables than inspectors)
4. **Cases** — distributed across stations with weighted case type probabilities
5. **Complainants, Victims, Witnesses, Accused** — linked per case
6. **Evidence** — multiple items per case with appropriate type distribution
7. **Forensic Lab Requests** — for evidence items marked for lab analysis
8. **Warrants & Arrests** — for cases with accused
9. **Bail Records** — for arrested accused
10. **Charge Sheets** — for closed/pending-trial cases
11. **Duty Roster** — officer schedules across stations
12. **Analytics Tables** — simulated AI agent output (hotspots, priority scores, workload suggestions)

The `app.current_officer_id` session variable was set before each insert batch so that audit triggers could resolve the responsible officer, resulting in a populated `audit.Audit_Log` with realistic attribution data.

---

## 13. Screenshots

### 13.1 Database Schema Overview

![alt text](image.png)

### 13.2 Sample Cases Data

![alt text](image-1.png)

### 13.3 Audit Log in Action

![alt text](image-2.png)

### 13.4 Generated FIR Numbers

![alt text](image-3.png)

### 13.5 Analytics — Crime Hotspots

![alt text](image-4.png)

### 13.6 Analytics — Case Priority Scores

![alt text](image-5.png)

### 13.7 Analytics — Workload Assignments

![alt text](image-6.png)

### 13.8 Trigger Validation in Action

![alt text](image-7.png)

### 13.9 Evidence Immutability

![alt text](image-8.png)

### 13.10 Sequence Registry

![alt text](image-9.png)

---

_End of Report_
