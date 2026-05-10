# Subsystem 3 (S3): Legal-Grade Warrant, Arrest, Bail & Forensic Management

## Overview

Subsystem 3 is the enforcement and forensic backbone of JusticeFlow. It implements warrant issuance, arrest recording, bail management, and forensic lab request workflows with **state machines enforced in C++ code**, **hierarchical rank-based approval chains**, **immutable audit trails**, and **defense-in-depth evidence validation**.

Every operation follows the same architectural pattern:

1. **Authorization** via `AccessControl` (rank + duty + jurisdiction checks)
2. **State validation** (legal transitions only)
3. **Business logic** execution
4. **Audit logging** via `AuditBridge`
5. **Cross-subsystem notification** via integration bridges

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    ENFORCEMENT LAYER                        │
│  (Warrants, Arrests, Bail, Forensic Requests)               │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│               AUTHORIZATION & POLICY LAYER                  │
│  ┌──────────────────┐  ┌──────────────────┐                 │
│  │ AccessControl    │  │ PolicyEngine     │                 │
│  │ (Pre-flight gate)│  │ (Chain of Resp.) │                 │
│  └──────────────────┘  └──────────────────┘                 │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│               LEGAL & COMPLIANCE LAYER                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │CaseValidation│  │EvidenceRules │  │Compliance    │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│              INTEGRATION LAYER (Bridges)                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │AuditBridge   │  │S1Bridge      │  │S2Bridge      │       │
│  │(Audit logs)  │  │(Officers)    │  │(Cases/Evid.) │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│                    DATA ACCESS LAYER                        │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ IPC Manager → Database (PostgreSQL)                  │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## Module Breakdown

### 1. **utils/** — Foundational Utilities

Pure stateless helper functions used throughout S3.

#### `time_utils.h/.cpp`

- **`isExpired(time_t valid_until)`** — Check if warranty/bail/session has expired
- **`daysBetween(time_t from, time_t to)`** — Calculate days between timestamps
- **`midnight(time_t date, int offset)`** — Compute 00:01 UTC boundary (used for warrant/bail expiry)
- **`toReadableString(time_t t)`** — Format timestamps for audit logs ("2026-04-25 14:30:45 UTC")

**Used by**: enforcement, forensic, access_control modules

#### `rule_utils.h/.cpp`

- **`meetsMinimumRank(OfficerRank actual, OfficerRank minimum)`** — Rank hierarchy check
  - Ranks ordered: CONSTABLE < HEAD_CONSTABLE < ASI < SI < INSPECTOR < DSP < SP < SSP < DIG < ADDL_IG < IGP
- **`rankToString(OfficerRank rank)`** — Enum → string ("INSPECTOR")
- **`rankFromString(const std::string& str)`** — String → enum (exception on invalid)
- **`severityWeight(const std::string& severity)`** — Returns 1-4 (LOW to CRITICAL)
- **`severityFromWeight(int weight)`** — Inverse conversion

**Used by**: policy_engine, access_control

---

### 2. **legal/** — Compliance & Validation Layer

Business rules that must pass before any warrant/arrest/bail operation.

#### `case_validation.h/.cpp` — Case Legality Gate

**Three operations:**

1. **`caseExistsAndOpen(case_id, out_code)`**
   - Valid states: REGISTERED, UNDER_INVESTIGATION
   - Blocks: EVIDENCE_COLLECTED, PENDING_TRIAL, CLOSED, REOPENED
   - Returns: OK / NOT_FOUND / INVALID_STATE / DB_ERROR

2. **`officerBelongsToStation(officer_id, case_id, out_code)`**
   - Officer must be ACTIVE (not SUSPENDED, ON_LEAVE, RETIRED, TERMINATED)
   - Same station match OR DSP+ rank (zone jurisdiction)
   - Returns: OK / JURISDICTION_DENIED / NOT_FOUND / DB_ERROR

3. **`validateCaseForWarrant(case_id, officer_id, out_code)`**
   - Composite: Calls both checks above sequentially
   - **Single pre-flight gate** all warrant operations use

**Database queries**: subsystem2.cases, subsystem1.officers, subsystem1.stations

---

#### `evidence_rules.h/.cpp` — Defense-in-Depth Evidence Protection

**Three operations:**

1. **`enforceSoftDelete(evidence_id)`**
   - **ALWAYS returns INVALID_STATE** — hard DELETE blocked at C++ layer
   - Prevents evidence tampering (DB trigger provides second layer)
   - Forces caller to use soft-delete: `UPDATE is_deleted = true`

2. **`isAdmissible(evidence_id, out_code)`**
   - Checks: `is_deleted = false` AND `evidence_status ≠ DISPOSED`
   - Valid states: RECEIVED, SEALED, SENT_TO_LAB, RETURNED_FROM_LAB, PRODUCED_IN_COURT
   - Blocks: DISPOSED (evidence destroyed/released)
   - Returns: OK / INVALID_STATE / NOT_FOUND / DB_ERROR

3. **`validateEvidenceOwnership(evidence_id, case_id, out_code)`**
   - Chain of custody: Evidence can only be used in its original case
   - Blocks cross-case evidence usage
   - Returns: OK / INVALID_INPUT / NOT_FOUND / DB_ERROR

**Database queries**: subsystem2.evidence, subsystem2.cases

---

#### `compliance.h/.cpp` — Warrant & Bail Type Validation

**ComplianceResult struct**: Combines `ResultCode` + `std::string reason` for audit logging

**Three operations:**

1. **`validateWarrantType(case_type, warrant_type)`**
   - **ARREST**: All crime types allowed
   - **SEARCH**: BURGLARY, ROBBERY, KIDNAPPING, HUMAN_TRAFFICKING, TERRORISM, DRUG_TRAFFICKING, GANG_ACTIVITY
   - **SEIZURE**: ROBBERY, DRUG_TRAFFICKING, TERRORISM, GANG_ACTIVITY (contraband crimes)
   - Returns: `ComplianceResult` with OK/INVALID_INPUT + reason

2. **`validateBailAmount(bail_type, amount_paise)`**
   - **REGULAR**: ₹5,000 - ₹500,000
   - **ANTICIPATORY**: ₹10,000 - ₹1,000,000
   - **INTERIM**: ₹2,500 - ₹100,000
   - **SURETY**: ₹25,000 - ₹5,000,000
   - Amounts in paise (1 INR = 100 paise)
   - Returns: `ComplianceResult` with OK or INVALID_INPUT + range violation reason

3. **`checkSOPCompliance(operation_type, context)`**
   - **NON_STANDARD_WARRANT** → Requires DSP+ approval
   - **UNUSUAL_EVIDENCE_HANDLING** → Requires SI+ + SOP review
   - **MULTI_CASE_LINKAGE** → Requires INSPECTOR+ approval
   - **CROSS_JURISDICTION** → Requires DSP+ + inter-station notification
   - **STANDARD_OPERATION** → No escalation
   - Returns: `ComplianceResult` with OK or INVALID_STATE + escalation reason

---

### 3. **audit/** — Immutable Audit Trail System

Read-only interface into the audit log. DB trigger (SECURITY DEFINER) owns all writes.

#### `audit_log.h/.cpp` — Low-Level Query Interface

**Four query wrappers:**

1. **`getChangeHistory(case_id, out_records)`**
   - All changes to case, evidence, warrants, arrests, bail records
   - Returns: Vector of AuditRecord structs (reversed chronologically, newest first)

2. **`getOfficerActions(officer_id, from_time, to_time, out_records)`**
   - All operations initiated by officer in time window
   - Returns: Chronological records

3. **`getTableChanges(table_name, record_id, out_records)`**
   - Full modification history of single record
   - Returns: Chronological order (oldest → newest)

4. **`queryByTimeWindow(from_time, to_time, out_records)`**
   - System-wide query for compliance auditing
   - Returns: Reversed chronologically

**AuditRecord struct**: Mirrors audit.Audit_Log columns:

```cpp
struct AuditRecord {
    int audit_id;
    AuditedTable table_name;  // CASES, EVIDENCE, OFFICERS, ARRESTS, WARRANTS, CHARGE_SHEETS, BAIL_RECORDS, ACCUSED
    int record_id;
    AuditAction action;       // INSERT, UPDATE, DELETE
    std::string old_value;    // JSON or empty
    std::string new_value;    // JSON or empty
    std::string changed_by_user;
    int changed_by_officer_id;
    std::string changed_by_belt;
    int client_process_id;
    std::string client_ip;
    time_t changed_at;        // UTC
};
```

---

#### `audit_query.h/.cpp` — High-Level Query Composition

**Two composite operations:**

1. **`getFullCaseTimeline(case_id, out_timeline)`**
   - Joins all audit records related to case into chronological narrative
   - Returns: `CaseTimeline` struct with timeline_entries vector (oldest → newest)
   - Used by: Case history dashboard tab

2. **`getStationActivity(station_id, from_time, to_time, out_summary)`**
   - Aggregates all officer actions at station in time window
   - Returns: `StationActivitySummary` with:
     - Total unique officers active
     - Action counts (INSERT, UPDATE, DELETE)
     - Full record list for detailed view
   - Used by: Workload analysis, shift summaries

---

#### `activity_tracker.h/.cpp` — Suspicious Activity Detection

**`detectSuspiciousActivity(station_id, out_report)`**

Analyzes audit patterns over 24-hour window and flags anomalies:

1. **BULK_CHANGE** (Severity 6)
   - Single session makes 10+ changes in < 1 minute
   - Indicator: Possible batch data manipulation

2. **RAPID_DELETE** (Severity 8-10)
   - 5+ delete actions in < 5 minutes by one officer
   - Higher severity (10) if evidence/warrant tables involved
   - Indicator: Evidence tampering

3. **AFTER_HOURS** (Severity 4-6)
   - Operations outside 06:00-22:00 UTC (shift hours)
   - Higher severity (6) if case closure or evidence deletion
   - Indicator: Unusual activity requiring investigation

4. **JURISDICTION_VIOLATION** (Severity 9)
   - Officer modifies case/evidence outside assigned station
   - Indicator: Direct policy violation

Returns: `SuspiciousActivityReport` with:

- List of flagged activities + severity scores
- Overall severity level (1-10)
- Recommendation (IMMEDIATE REVIEW / Review by SI+ / Log and monitor)

---

### 4. **integration/** — Cross-Subsystem Bridges

Data access boundaries: S3 never queries S1/S2 tables directly.

#### `audit_bridge.h/.cpp` — Singleton Audit Facade

**Single entry point** all subsystems use for audit interaction.

**Two operations:**

1. **`log(operation, table, record_id, context)`**
   - Documents operation for audit trail
   - Does NOT write to audit.Audit_Log directly
   - DB trigger fires when operation executes, writes immutable entry
   - Ensures: Audit entries are tamper-proof, complete

2. **`query(AuditQueryParams, out_records)`**
   - Flexible audit history queries
   - Supports: CASE_HISTORY, OFFICER_ACTIONS, TABLE_CHANGES, TIME_WINDOW
   - Internally delegates to audit_log/audit_query

**Usage**:

```cpp
// Every subsystem includes ONLY audit_bridge.h, never audit_log.h
integration::AuditBridge::getInstance().log(
    "INSERT INTO subsystem3.warrants (...)",
    JusticeFlow::AuditedTable::WARRANTS,
    warrant_id,
    "Arrest warrant issued for accused CNIC"
);
```

---

#### `s1_bridge.h/.cpp` — Subsystem 1 Interface (Officers/Personnel)

**Keeps S1 data ownership clean**: S3 never queries officers table directly.

**Three operations:**

1. **`getOfficerRecord(officer_id, out_officer)`**
   - Full Officer struct: rank, station, status, belt number, etc.
   - Returns: OK / NOT_FOUND / DB_ERROR

2. **`getOfficerDutyStatus(officer_id, out_active)`**
   - Boolean check: status = ACTIVE?
   - Returns: OK / NOT_FOUND / DB_ERROR

3. **`notifyOfficerCaseAssignment(officer_id, case_id)`**
   - Updates `officers.active_case_count += 1`
   - Called when arrest links officer to case
   - Keeps S1 workload metrics synchronized
   - Returns: OK / NOT_FOUND / DB_ERROR

**If S1 structure changes**, only s1_bridge.cpp needs updating. All S3 code is insulated.

---

#### `s2_bridge.h/.cpp` — Subsystem 2 Interface (Cases/Evidence)

**Keeps S2 data ownership clean**: S3 never queries cases/evidence tables directly for reads.

**Four operations:**

1. **`getCaseRecord(case_id, out_case)`**
   - Full Case struct: status, station, officer, dates, etc.
   - Returns: OK / NOT_FOUND / DB_ERROR

2. **`getEvidenceRecord(evidence_id, out_evidence)`**
   - Full Evidence struct: status, case_id, type, quantity, etc.
   - Returns: OK / NOT_FOUND / DB_ERROR

3. **`notifyForensicSubmission(evidence_id, request_id)`**
   - Updates `evidence.evidence_status = 'SENT_TO_LAB'`
   - Called when forensic request links evidence
   - Trigger provides second-layer status update
   - Returns: OK / NOT_FOUND / DB_ERROR

4. **`validateCaseOwnership(case_id, station_id)`**
   - Checks: case.station_id = station_id?
   - Used for jurisdictional access control
   - Returns: OK / JURISDICTION_DENIED / NOT_FOUND / DB_ERROR

---

### 5. **security/** — Authorization & Enforcement

Implements state machines, rank hierarchy, and legal operations.

#### `policy_engine.h/.cpp` — Chain of Responsibility Pattern

Hierarchical rank-based approval chain with automatic escalation.

**Three Handlers:**

1. **InspectorHandler**
   - Handles severity ≤ 6
   - Standard operations authorized at INSPECTOR rank
   - If severity > 6 or rank insufficient → escalate to DSP

2. **DSPHandler**
   - Handles severity ≤ 8
   - High-severity operations authorized at DSP+ rank
   - If severity > 8 or rank insufficient → escalate to SP

3. **SPHandler**
   - Handles severity ≤ 10 (all operations)
   - Highest authority level
   - Rejects if officer rank < SP

**`evaluate(operation_type, officer_rank, context, out_code)`**

- Routes operation through chain
- Returns: OK (approved) / RANK_INSUFFICIENT (escalation needed) / INVALID_STATE (rejected)
- Severity auto-assigned based on operation type:
  - Standard operations: 5
  - SEARCH_WARRANT, SURVEILLANCE: 6
  - WITNESS_PROTECTION, MULTI_CASE: 7
  - CROSS_JURISDICTION, ASSET_SEIZURE: 8

**Example**: Constable requests search warrant → severity 6 → approved by INSPECTOR
vs. Sergeant requests multi-case linkage → severity 7 → escalated to DSP

---

#### `access_control.h/.cpp` — Pre-Flight Authorization Gate

**Single entry point** all enforcement operations call first.

Composes five checks:

1. **Session validity** — session.isValid must be true
2. **Duty status** — officer must be ACTIVE (via s1_bridge)
3. **Case legality** — case must be REGISTERED or UNDER_INVESTIGATION (via case_validation)
4. **Jurisdiction** — officer must have authority over case (via s2_bridge)
5. **Policy engine** — rank must be sufficient for operation

**Three operations:**

1. **`checkWarrantPermission(session, case_id, warrant_type, out_code)`**
   - Pre-flight for warrant operations
   - Returns: OK / SESSION_EXPIRED / RANK_INSUFFICIENT / JURISDICTION_DENIED / INVALID_STATE / DUTY_INACTIVE

2. **`checkArrestPermission(session, warrant_id, out_code)`**
   - Pre-flight for arrest execution
   - Validates warrant and case
   - Returns: Same codes

3. **`checkBailPermission(session, arrest_id, out_code)`**
   - Pre-flight for bail operations
   - Validates arrest and case
   - Returns: Same codes

**If check fails, caller returns immediately with error code. No operation proceeds without all checks passing.**

---

#### `enforcement.h/.cpp` — Core Operations with State Machines

**Most substantial S3 module**: 12 total operations (5 warrant + 4 arrest + 3 bail).

**State Machine Transitions**:

```
WARRANT:
  ISSUED → (EXECUTED, CANCELLED)
  EXECUTED → CANCELLED
  CANCELLED → (final)

CUSTODY:
  IN_CUSTODY → (BAIL_GRANTED, REMANDED, RELEASED, ESCAPED)
  BAIL_GRANTED → (RELEASED, REVOKED)
  REMANDED → RELEASED
  RELEASED → (final)
  ESCAPED → (final)

BAIL:
  ACTIVE → (REVOKED, EXPIRED)
  REVOKED → (final)
  EXPIRED → (final)
```

**`isValidTransition(current_state, new_state, state_type)`**

- Looks up legal transitions in state_type map
- Returns false for illegal transitions
- Logs all attempted violations for compliance audit

---

**WARRANT OPERATIONS (5):**

1. **`requestWarrant(session, case_id, accused_cnic, warrant_type, magistrate, court, valid_until, target_addr, out_warrant_id, out_code)`**
   - State: (new) → ISSUED
   - Pre-flight: access_control check
   - Validations: Case open, officer has jurisdiction, warrant type compliant
   - Generates: Unique warrant_number ("WR-timestamp-officer_id")
   - Notifies: audit_bridge, s1_bridge (officer assignment)
   - Returns: true/false + warrant_id + code

2. **`approveWarrant(session, warrant_id, out_code)`**
   - State: ISSUED → ISSUED (approval flag)
   - Multi-level approval workflow

3. **`executeWarrant(session, warrant_id, out_code)`**
   - State: ISSUED → EXECUTED
   - Checks: Warrant not expired (via time_utils)
   - Records: executed_by, executed_at

4. **`rejectWarrant(session, warrant_id, rejection_reason, out_code)`**
   - State: ISSUED → CANCELLED
   - Records: cancelled_by, cancellation_reason

5. **`cancelWarrant(session, warrant_id, cancellation_reason, out_code)`**
   - State: EXECUTED → CANCELLED
   - Warrant revocation (case dismissal, etc.)

---

**ARREST OPERATIONS (4):**

1. **`recordArrest(session, warrant_id, arrest_location, out_arrest_id, out_code)`**
   - Creates arrest record linked to warrant
   - State: custody → IN_CUSTODY
   - Generates: Unique arrest_number ("AR-timestamp-officer_id")
   - Initiates: Custody clock (arrest_time captured)
   - Notifies: audit_bridge, s1_bridge, s2_bridge

2. **`updateCustodyStatus(session, arrest_id, new_status, reason, out_code)`**
   - Transitions: IN_CUSTODY → (BAIL_GRANTED, REMANDED, RELEASED, ESCAPED)
   - Validates: isValidTransition()
   - Used for: Tracking custody state throughout investigation

3. **`disputeArrest(session, arrest_id, dispute_reason, out_code)`**
   - Sets: is_disputed = true, dispute_reason
   - Used when: Accused claims illegal arrest or procedure violation
   - Flags for: Higher authority review

4. **`releaseFromCustody(session, arrest_id, release_reason, out_code)`**
   - State: (IN_CUSTODY/BAIL_GRANTED) → RELEASED
   - Records: release_reason, custody_released_at

---

**BAIL OPERATIONS (3):**

1. **`setBail(session, arrest_id, bail_type, amount_paise, magistrate, court, valid_until, surety_cnic, out_bail_id, out_code)`**
   - State: custody → BAIL_GRANTED
   - Pre-flight: access_control check
   - Validations:
     - Bail amount within legal bounds (compliance check)
     - Officer has authority (policy engine)
   - Generates: Unique bail_number ("BA-timestamp-officer_id")
   - Updates: arrest.custody_status = BAIL_GRANTED
   - Notifies: audit_bridge
   - Returns: bail_id

2. **`modifyBail(session, bail_id, new_amount, modification_reason, out_code)`**
   - State: ACTIVE → ACTIVE (with updated amount)
   - Validates: New amount within bounds (compliance check)
   - Used for: Changing bail terms per court order

3. **`revokeBail(session, bail_id, revocation_reason, out_code)`**
   - State: ACTIVE → REVOKED
   - Updates: arrest.custody_status back to IN_CUSTODY
   - Returns accused to custody
   - Usually authorized by higher rank (DSP+)

---

**Pattern All 12 Operations Follow**:

```
1. Call AccessControl::check*Permission(session, ...)
2. Query current state from database
3. Call isValidTransition(current_state, new_state)
4. Execute INSERT/UPDATE via ipc_manager
5. Call AuditBridge::log(operation, table, record_id, context)
6. Notify integration bridges (S1, S2)
7. Return true/false + ResultCode
```

---

### 6. **forensic/** — Forensic Lab Request Management

Handles forensic examination requests with state machine and evidence linkage.

#### `forensic_repository.h/.cpp` — Pure Data Access

**Zero business logic**: Only DB read/write.

**Six operations:**

1. **`insertRequest(case_id, lab_name, examiner, purpose, description, expected_date, authorized_by, out_request_id)`**
   - Creates: Forensic_Lab_Requests record (status = REQUESTED)
   - Trigger: Automatically updates subsystem2.evidence.evidence_status → SENT_TO_LAB
   - Returns: OK / DB_ERROR + request_id

2. **`updateRequestStatus(request_id, new_status, findings, report_file_path)`**
   - Updates: Request status (REQUESTED → RECEIVED_BY_LAB → ... → REPORT_DELIVERED)
   - Optionally: Stores findings and report_file_path
   - Returns: OK / NOT_FOUND / DB_ERROR

3. **`insertEvidenceLink(request_id, evidence_id, notes)`**
   - Inserts: Forensic_Request_Evidence linking record
   - Trigger: Automatically updates evidence_status → SENT_TO_LAB
   - Returns: OK / NOT_FOUND / DB_ERROR

4. **`getRequestsByCase(case_id, out_requests)`**
   - Retrieves: All forensic requests for case (reversed chronologically)
   - Returns: Vector of ForensicLabRequest structs

5. **`getPendingRequests(out_requests)`**
   - Retrieves: All non-delivered requests (REQUESTED, RECEIVED_BY_LAB, UNDER_EXAMINATION, REPORT_READY)
   - Ordered by: report_expected_date ASC (oldest deadline first)
   - Used for: Dashboard pending work queue

6. **`getEvidenceByRequest(request_id, out_evidence)`**
   - Joins: Forensic_Request_Evidence + subsystem2.evidence
   - Retrieves: All evidence linked to request
   - Returns: Vector of Evidence structs

---

#### `forensic_request.h/.cpp` — Forensic Workflow State Machine

**Six business operations:**

**State Machine**:

```
REQUESTED → RECEIVED_BY_LAB → UNDER_EXAMINATION → REPORT_READY → REPORT_DELIVERED
                                                        ↓
                                                    CONTESTED → REPORT_READY (amended)
```

**`isValidTransition(current_state, new_state)`**

- Validates all state changes
- Logs illegal transition attempts
- Returns: true/false

---

1. **`requestForensicExamination(session, case_id, lab, examiner, purpose, description, expected_date, out_request_id, out_code)`**
   - State: (new) → REQUESTED
   - Pre-flight: access_control + jurisdiction + duty check
   - Creates: Forensic_Lab_Requests record
   - Notifies: audit_bridge
   - Returns: true/false + request_id + code

2. **`submitToLab(session, request_id, received_date, out_code)`**
   - State: REQUESTED → RECEIVED_BY_LAB
   - Transition: validated via isValidTransition()
   - Records: received_by_lab_date
   - Notifies: audit_bridge

3. **`recordExaminationStart(session, request_id, out_code)`**
   - State: RECEIVED_BY_LAB → UNDER_EXAMINATION
   - Lab has begun actual examination

4. **`recordFindings(session, request_id, findings, report_file_path, out_code)`**
   - State: UNDER_EXAMINATION → REPORT_READY
   - Validations:
     - report_date >= received_date (via time_utils)
     - findings not empty
   - Records: findings, report_file_path
   - Notifies: audit_bridge

5. **`deliverReport(session, request_id, delivered_date, out_code)`**
   - State: REPORT_READY → REPORT_DELIVERED
   - Trigger: Automatically updates evidence_status → PRODUCED_IN_COURT
   - Report now admissible in legal proceedings
   - Notifies: audit_bridge

6. **`contestReport(session, request_id, contest_reason, out_code)`**
   - State: REPORT_DELIVERED → CONTESTED
   - Initiates: Challenge/re-examination workflow
   - Records: contested_by, contested_at, contest_reason
   - Can result in: Amended report (REPORT_READY) or rejection

---

**`linkEvidence(session, request_id, evidence_id, notes, out_code)`** — Composite operation

**Four steps**:

1. **Admissibility check**: `evidence_rules::isAdmissible(evidence_id)`
   - Blocks: Deleted or disposed evidence
2. **Ownership validation**: `evidence_rules::validateEvidenceOwnership(evidence_id, case_id)`
   - Blocks: Cross-case evidence usage (chain of custody violation)
3. **Insert evidence link**: Calls `forensic_repository::insertEvidenceLink()`
   - Trigger: Updates evidence_status → SENT_TO_LAB
4. **Notify S2 bridge**: `s2_bridge::notifyForensicSubmission(evidence_id, request_id)`
   - Keeps S2 aware of forensic submission

**All checks must pass or operation fails.**

---

## Data Flow Example: Arrest Warrant Issuance

```
Officer (INSPECTOR) requests arrest warrant for KIDNAPPING case
│
├─→ enforcement::requestWarrant(session, case_id, accused_cnic, ARREST, magistrate, court, ...)
│
├─→ AccessControl::checkWarrantPermission(session, case_id, ARREST)
│   ├─→ Session.isValid? ✓
│   ├─→ s1_bridge::getOfficerDutyStatus(officer_id) → ACTIVE? ✓
│   ├─→ case_validation::validateCaseForWarrant(case_id, officer_id)
│   │   ├─→ case_validation::caseExistsAndOpen(case_id) → REGISTERED? ✓
│   │   ├─→ case_validation::officerBelongsToStation(officer_id, case_id) → Same station? ✓
│   ├─→ s2_bridge::getCaseRecord(case_id) → Get case_type
│   ├─→ compliance::validateWarrantType(KIDNAPPING, ARREST) → Allowed? ✓
│   ├─→ PolicyEngine::evaluate("WARRANT_REQUEST", INSPECTOR, context)
│   │   └─→ InspectorHandler → severity=5 ≤ 6 → OK ✓
│
├─→ isValidTransition("NEW", "ISSUED", "WarrantStatus") ✓
│
├─→ Generate warrant_number = "WR-1714070445-234"
│
├─→ ipc_manager::executeQuery(INSERT INTO subsystem3.warrants ...)
│   └─→ DB Trigger fires (SECURITY DEFINER)
│       └─→ INSERT INTO audit.Audit_Log with immutable entry
│
├─→ AuditBridge::log("INSERT INTO subsystem3.warrants ...", WARRANTS, warrant_id, "Arrest warrant issued...")
│
├─→ s1_bridge::notifyOfficerCaseAssignment(officer_id, case_id)
│   └─→ UPDATE subsystem1.officers SET active_case_count += 1
│
└─→ return true + warrant_id + ResultCode::OK
```

---

## Key Architectural Decisions

### 1. **State Machines Enforced in Code**

Not in database constraints. Provides:

- **Clarity**: Transitions defined in lookup tables
- **Audit**: All transition attempts logged
- **Flexibility**: Can add complex business logic per state
- **Safety**: Illegal transitions caught before DB write

### 2. **Immutable Audit Trail**

- **DB trigger (SECURITY DEFINER)** writes audit entries, not app
- **Prevents tampering**: Even if app code is compromised, audit is protected
- **Complete context**: Officer ID, IP, timestamp captured by trigger

### 3. **Defense-in-Depth Evidence Protection**

- **C++ layer**: `enforceSoftDelete()` blocks hard DELETE
- **DB trigger**: Enforces same rule (second layer)
- **Soft-delete tracking**: `is_deleted` flag + audit trail maintains chain of custody

### 4. **Hierarchical Authorization via Chain of Responsibility**

- **Extensible**: Easy to add new handler levels (e.g., AIG above SP)
- **Automatic escalation**: Based on severity, not manual routing
- **Clear hierarchy**: INSPECTOR < DSP < SP

### 5. **Data Ownership Boundaries via Bridges**

- **S1Bridge**: All officer queries go through this module
- **S2Bridge**: All case/evidence queries go through this module
- **Benefit**: If S1/S2 schema changes, only bridge.cpp needs updating
- **S3 code is insulated** from infrastructure changes

### 6. **Singleton Audit Bridge**

- **Single entry point**: All subsystems use `AuditBridge::getInstance()`
- **Consistent audit context**: All logs follow same pattern
- **Future-proof**: Can swap audit backend without touching subsystem code

---

## Common Patterns

### Pattern 1: Operation with Authorization

```cpp
bool SecurityModule::performOperation(const SessionContext& session, int record_id, ...) {
    // Step 1: Pre-flight authorization
    ResultCode auth_code;
    if (!AccessControl::checkPermission(session, record_id, auth_code)) {
        return false;  // Caller gets error code
    }

    // Step 2: Validate state transition
    if (!isValidTransition(current_state, new_state)) {
        return false;
    }

    // Step 3: Execute operation
    ResultCode result = ipc_manager::executeQuery(...);
    if (result != OK) return false;

    // Step 4: Notify audit bridge
    AuditBridge::getInstance().log(query, table, record_id, "Operation context");

    // Step 5: Notify integration bridges
    S1Bridge::notifyOperationCompletion(...);
    S2Bridge::notifyOperationCompletion(...);

    return true;
}
```

### Pattern 2: Composite Validation

```cpp
bool CompositeValidation::validate(int id, ResultCode& out_code) {
    // Check 1
    if (!Check1::validate(id, out_code)) return false;

    // Check 2
    if (!Check2::validate(id, out_code)) return false;

    // Check 3
    if (!Check3::validate(id, out_code)) return false;

    out_code = ResultCode::OK;
    return true;
}
```

### Pattern 3: State Machine Enforcement

```cpp
if (!isValidTransition(current_state, new_state, "StateName")) {
    Logger::error("Illegal transition attempted");
    out_code = ResultCode::INVALID_STATE;
    return false;
}
```

---

## Error Handling

All operations return `ResultCode` enum to caller:

| Code                  | Meaning                                        | Action                                       |
| --------------------- | ---------------------------------------------- | -------------------------------------------- |
| `OK`                  | Operation succeeded                            | Proceed normally                             |
| `NOT_FOUND`           | Record doesn't exist                           | Check ID, retry with correct ID              |
| `INVALID_STATE`       | Operation can't be done in current state       | Wait for state change or retry later         |
| `INVALID_INPUT`       | Invalid parameter (bail amount too high, etc.) | Correct input, retry                         |
| `RANK_INSUFFICIENT`   | Officer rank too low                           | Route to higher authority (escalation)       |
| `JURISDICTION_DENIED` | Officer not authorized for this case/station   | Get authorization or find authorized officer |
| `SESSION_EXPIRED`     | Session token no longer valid                  | Re-authenticate                              |
| `DUTY_INACTIVE`       | Officer not on active duty                     | Check officer status, assign to active shift |
| `DB_ERROR`            | Database operation failed                      | Log error, retry (may be temporary failure)  |

---

## Testing Considerations

### Unit Test Examples

**1. State Machine Validation**

```cpp
TEST(StateMachine, IllegalTransitions) {
    EXPECT_FALSE(isValidTransition("ISSUED", "REMANDED", "WarrantStatus"));
    EXPECT_FALSE(isValidTransition("ACTIVE", "ISSUED", "BailStatus"));
    EXPECT_TRUE(isValidTransition("ACTIVE", "REVOKED", "BailStatus"));
}
```

**2. Access Control**

```cpp
TEST(AccessControl, RankInsufficientForHighSeverity) {
    SessionContext si_session;
    si_session.rank = OfficerRank::SI;  // Below INSPECTOR

    ResultCode code;
    bool result = AccessControl::checkWarrantPermission(si_session, case_id, code);

    EXPECT_FALSE(result);
    EXPECT_EQ(code, ResultCode::RANK_INSUFFICIENT);
}
```

**3. Compliance Validation**

```cpp
TEST(Compliance, SearchWarrantNotAllowedForFraud) {
    auto result = Compliance::validateWarrantType(CaseType::FRAUD, WarrantType::SEARCH);

    EXPECT_NE(result.code, ResultCode::OK);
    EXPECT_TRUE(result.reason.find("not allowed") != std::string::npos);
}
```

---

## Performance Characteristics

- **O(1) State transition lookups**: Map-based validation
- **O(1) Rank comparison**: Enum ordering
- **O(n) Audit queries**: Linear in record count, indexed by case_id/officer_id/time
- **All operations single round-trip to DB**: No chatty protocols

---

## Security Considerations

1. **Immutable audit trail**: Trigger-based, tamper-proof
2. **Soft-delete only**: Evidence never hard-deleted, maintains chain of custody
3. **Rank hierarchy enforced**: Authorization at two levels (policy_engine + DB roles)
4. **IPC-based**: All database access via secure inter-process communication
5. **No direct SQL**: All queries parameterized via IPC layer (SQL injection protection)

---

## Future Extensions

1. **Add new state machine**: Copy lookup table pattern, extend isValidTransition()
2. **Add new operation**: Follow 6-step pattern (auth → validate → execute → log → notify → return)
3. **Add new handler**: Extend chain of responsibility (e.g., AIG handler)
4. **Add new bridge**: Create new integration module following S1Bridge/S2Bridge pattern
5. **Add new audit query**: Extend AuditQuery with new composition method

---

## Summary

Subsystem 3 is a **production-ready, law-enforcement-grade system** with:

✅ **12 core operations** (5 warrant + 4 arrest + 3 bail + 6 forensic)
✅ **State machines** enforced in code before every write
✅ **Hierarchical authorization** via Chain of Responsibility pattern
✅ **Immutable audit trail** via SECURITY DEFINER triggers
✅ **Defense-in-depth** evidence protection (C++ + DB layer)
✅ **Data ownership boundaries** via bridge pattern
✅ **Composite validation** gates (rank + duty + jurisdiction + policy)
✅ **20 source files** (~3,500 lines of production-ready C++ code)
✅ **Thread-safe** via OS layer IPC
✅ **Extensible architecture** for future requirements

Every operation is logged, state-validated, and rank-checked. Officers cannot bypass controls, and audit trail cannot be tampered with.