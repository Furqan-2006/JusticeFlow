-- =============================
-- CORE TABLES
-- =============================
CREATE TABLE Stations (
    station_id BIGSERIAL PRIMARY KEY,
    station_code VARCHAR(20) UNIQUE NOT NULL,
    station_name VARCHAR(100) NOT NULL,
    station_type station_type_enum NOT NULL,
    phone VARCHAR(15),
    email VARCHAR(100) UNIQUE,
    address TEXT NOT NULL,
    city VARCHAR(50) NOT NULL,
    district VARCHAR(100) NOT NULL,
    zone VARCHAR(50),
    parent_station_id BIGINT REFERENCES Stations(station_id),
    is_active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE Persons (
    cnic VARCHAR(15) PRIMARY KEY CHECK (cnic ~ '^\\d{5}-\\d{7}-\\d{1}$'),
    full_name VARCHAR(100) NOT NULL,
    gender gender_enum NOT NULL,
    dob DATE,
    mobile VARCHAR(15),
    email VARCHAR(100),
    permanent_address TEXT,
    current_address TEXT,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE Officers(
    officer_id BIGSERIAL PRIMARY KEY,
    belt_number VARCHAR(20) UNIQUE NOT NULL CHECK (belt_number ~ '^(PC|HC|K)-[0-9]+$'),
    cnic VARCHAR(15) UNIQUE NOT NULL REFERENCES Persons(cnic),
    qualification VARCHAR(100),
    joining_date DATE NOT NULL,
    joining_rank officer_rank_enum NOT NULL,
    current_rank officer_rank_enum NOT NULL,
    retirement_date DATE,
    bps_scale SMALLINT NOT NULL CHECK (
        bps_scale IN (7, 9, 11, 14, 16, 17, 18, 19, 20, 21, 22)
    ),
    station_id BIGINT NOT NULL REFERENCES Stations(station_id),
    status officer_status_enum NOT NULL DEFAULT 'ACTIVE',
    CONSTRAINT chk_retirement_status CHECK (
        (
            status = 'RETIRED'
            AND retirement_date IS NOT NULL
        )
        OR (
            status <> 'RETIRED'
            AND retirement_date IS NULL
        )
    )
) ;

CREATE TABLE Cases (
    case_id BIGSERIAL PRIMARY KEY,
    fir_number VARCHAR(30) UNIQUE NOT NULL,
    case_type case_type_enum NOT NULL,
    case_status case_status_enum NOT NULL DEFAULT 'REGISTERED',
    incident_date TIMESTAMPTZ NOT NULL CHECK (incident_date <= NOW()),
    incident_address TEXT NOT NULL,
    incident_description TEXT NOT NULL,
    incident_lat  DECIMAL(9,6),
    incident_lon  DECIMAL(9,6),
    station_id BIGINT NOT NULL REFERENCES Stations(station_id),
    primary_complainant_cnic VARCHAR(15) NOT NULL REFERENCES Persons(cnic),
    filed_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    filed_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    lead_officer_id BIGINT REFERENCES Officers(officer_id),
    parent_case_id BIGINT REFERENCES Cases(case_id),
    closed_at TIMESTAMPTZ,
    closure_reason TEXT,
    approval_status approval_status_enum NOT NULL DEFAULT 'NOT_REQUIRED',
    approved_by BIGINT REFERENCES Officers(officer_id),
    approved_at TIMESTAMPTZ,
    reopened_by BIGINT REFERENCES Officers(officer_id),
    reopened_at TIMESTAMPTZ,
    reopen_reason TEXT,
    CONSTRAINT chk_closed_requires_reason CHECK (
        closed_at IS NULL
        OR closure_reason IS NOT NULL
    ),
    CONSTRAINT chk_no_self_parent CHECK (
        parent_case_id IS NULL
        OR parent_case_id <> case_id
    )
);

CREATE TABLE Case_Officers (
    case_id             BIGINT NOT NULL REFERENCES Cases(case_id),
    officer_id          BIGINT NOT NULL REFERENCES Officers(officer_id),
    role                case_officer_role_enum NOT NULL,
    assigned_by         BIGINT NOT NULL REFERENCES Officers(officer_id),
    assigned_at         TIMESTAMPTZ DEFAULT NOW(),
    relieved_at         TIMESTAMPTZ,
    -- NULL means still actively assigned

    created_at          TIMESTAMPTZ DEFAULT NOW(),

    -- same officer can hold multiple roles on same case
    -- e.g. SIO and EVIDENCE_CUSTODIAN simultaneously
    CONSTRAINT pk_case_officers
        PRIMARY KEY (case_id, officer_id, role),

    CONSTRAINT chk_reliever_after_assignment
        CHECK (
            relieved_at IS NULL
            OR relieved_at > assigned_at
        )
);

CREATE TABLE Officer_Rank_History (
    history_id          BIGSERIAL PRIMARY KEY,
    officer_id          BIGINT NOT NULL REFERENCES Officers(officer_id),

    old_rank            officer_rank_enum NOT NULL,
    new_rank            officer_rank_enum NOT NULL,
    old_belt_number     VARCHAR(20),
    new_belt_number     VARCHAR(20),

    promotion_type      VARCHAR(20),
    -- REGULAR, ACTING

    effective_date      DATE NOT NULL,
    order_date          DATE NOT NULL,
    promoted_by         VARCHAR(100),
    
    created_at          TIMESTAMPTZ DEFAULT NOW(),

    CONSTRAINT chk_rank_actually_changed
        CHECK (old_rank <> new_rank),

    CONSTRAINT chk_effective_date_not_future
        CHECK (effective_date <= CURRENT_DATE)
);

CREATE TABLE Officer_Deployments (
    deployment_id       BIGSERIAL PRIMARY KEY,
    officer_id          BIGINT NOT NULL REFERENCES Officers(officer_id),

    -- home station stays in Officers.station_id
    -- this tracks temporary assignments elsewhere
    from_station_id     BIGINT NOT NULL REFERENCES Stations(station_id),
    to_station_id       BIGINT NOT NULL REFERENCES Stations(station_id),

    deployment_reason   TEXT,
    order_number        VARCHAR(100),

    deployed_from       DATE NOT NULL,
    deployed_until      DATE,
    -- NULL means indefinite / still deployed

    deployed_by         BIGINT NOT NULL REFERENCES Officers(officer_id),
    -- must be DSP or above

    is_active           BOOLEAN DEFAULT TRUE,

    created_at          TIMESTAMPTZ DEFAULT NOW(),
    updated_at          TIMESTAMPTZ DEFAULT NOW(),

    CONSTRAINT chk_different_stations
        CHECK (from_station_id <> to_station_id),

    CONSTRAINT chk_deployed_until_after_from
        CHECK (
            deployed_until IS NULL
            OR deployed_until > deployed_from
        )
);

-- =============================
-- CASE ROLE TABLES
-- =============================
CREATE TABLE Complainants (
    complainant_id BIGSERIAL PRIMARY KEY,
    case_id BIGINT NOT NULL REFERENCES Cases(case_id) ON DELETE CASCADE,
    person_cnic VARCHAR(15) NOT NULL REFERENCES Persons(cnic),
    relation_to_victim relationship_to_victim_enum NOT NULL,
    status complainant_status_enum NOT NULL DEFAULT 'ACTIVE',
    added_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    notify_on_update BOOLEAN DEFAULT TRUE,
    withdrawn_at TIMESTAMPTZ,
    withdrawal_reason TEXT,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    CONSTRAINT chk_withdrawal_requires_reason CHECK (
        withdrawn_at IS NULL OR withdrawal_reason IS NOT NULL
    )

);

CREATE TABLE Victims (
    victim_id BIGSERIAL PRIMARY KEY,
    case_id BIGINT NOT NULL REFERENCES Cases(case_id) ON DELETE CASCADE,
    person_cnic VARCHAR(15) NOT NULL REFERENCES Persons(cnic),
    injury_type VARCHAR(100),
    injury_severity injury_severity_enum NOT NULL DEFAULT 'NONE',
    vulnerability_category vulnerability_category_enum NOT NULL DEFAULT 'NONE',
    medical_report_ref VARCHAR(100),
    added_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE Witnesses (
    witness_id BIGSERIAL PRIMARY KEY,
    case_id BIGINT NOT NULL REFERENCES Cases(case_id) ON DELETE CASCADE,
    person_cnic VARCHAR(15) NOT NULL REFERENCES Persons(cnic),
    statement_text TEXT,
    statement_file_path VARCHAR(255),
    statement_recorded_at TIMESTAMPTZ,
    recorded_by BIGINT REFERENCES Officers(officer_id),
    protection_status witness_protection_enum NOT NULL DEFAULT 'NONE',
    is_identity_concealed BOOLEAN DEFAULT FALSE,
    added_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    CONSTRAINT chk_statement_exists CHECK (
        statement_text IS NOT NULL 
        OR statement_file_path IS NOT NULL
        )
);

CREATE TABLE Accused (
    accused_id BIGSERIAL PRIMARY KEY,
    master_accused_cnic VARCHAR(50) REFERENCES persons(cnic), -- in case of accused having alias
    case_id BIGINT NOT NULL REFERENCES Cases(case_id) ON DELETE CASCADE,
    person_cnic VARCHAR(15) NOT NULL REFERENCES Persons(cnic),
    involvement_type involvement_type_enum NOT NULL DEFAULT 'SUSPECT',
    added_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    CONSTRAINT chk_no_self_alias CHECK (
        master_accused_cnic IS NULL OR master_accused_cnic <> person_cnic
    )

);

CREATE TABLE Accused_Associations (
    accused_id BIGINT NOT NULL REFERENCES Accused(accused_id) ON DELETE CASCADE,
    associated_accused_id BIGINT NOT NULL REFERENCES Accused(accused_id) ON DELETE CASCADE,
    association_type association_type_enum NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    PRIMARY KEY (accused_id, associated_accused_id),
    CONSTRAINT chk_no_self_association CHECK (accused_id <> associated_accused_id)
);

-- =============================
-- LOG TABLES
-- =============================
CREATE TABLE Case_Status_Log (
    log_id BIGSERIAL PRIMARY KEY,
    case_id BIGINT NOT NULL REFERENCES Cases(case_id) ON DELETE CASCADE,
    old_status case_status_enum,
    new_status case_status_enum NOT NULL,
    changed_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    change_reason TEXT,
    changed_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE Case_Jurisdiction_History (
    history_id BIGSERIAL PRIMARY KEY,
    case_id BIGINT NOT NULL REFERENCES Cases(case_id) ON DELETE CASCADE,
    from_station_id BIGINT NOT NULL REFERENCES Stations(station_id),
    to_station_id BIGINT NOT NULL REFERENCES Stations(station_id),
    transfer_reason TEXT,
    transferred_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    transferred_at TIMESTAMPTZ DEFAULT NOW(),
    CONSTRAINT chk_different_stations CHECK (from_station_id <> to_station_id)
);

CREATE TABLE Evidence (
    -- IDENTITY
    evidence_id BIGSERIAL PRIMARY KEY,
    evidence_number VARCHAR(30) UNIQUE NOT NULL,
    -- format: EVD-YYYY-CASENUMBER-NNNN
    -- generated via trigger on INSERT
    -- LINKAGE
    case_id BIGINT NOT NULL REFERENCES Cases(case_id),
    -- CLASSIFICATION
    evidence_type evidence_type_enum NOT NULL,
    evidence_status evidence_status_enum NOT NULL DEFAULT 'RECEIVED',
    -- DESCRIPTION
    description TEXT NOT NULL,
    quantity SMALLINT DEFAULT 1,
    -- FILE REFERENCE (filesystem path, no BLOBs)
    file_path VARCHAR(255),
    -- only for DIGITAL and DOCUMENTARY types
    -- COLLECTION
    collected_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    collected_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    collection_location TEXT,
    -- SOFT DELETE (evidence is NEVER physically removed)
    is_deleted BOOLEAN DEFAULT FALSE,
    deleted_at TIMESTAMPTZ,
    deleted_by BIGINT REFERENCES Officers(officer_id),
    deletion_reason TEXT,
    -- META
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    -- CONSTRAINTS
    CONSTRAINT chk_deletion_requires_reason CHECK (
        is_deleted = FALSE
        OR (
            deleted_at IS NOT NULL
            AND deletion_reason IS NOT NULL
        )
    ),
    CONSTRAINT chk_file_path_for_digital CHECK (
        evidence_type NOT IN ('DIGITAL', 'DOCUMENTARY')
        OR file_path IS NOT NULL
    ),
    CONSTRAINT chk_quantity_positive CHECK (quantity > 0)
);

-- Every custody transfer is permanently recorded
-- Answers: who had it, when, and why it moved
CREATE TABLE Evidence_Custody_Log (
    custody_id BIGSERIAL PRIMARY KEY,
    evidence_id BIGINT NOT NULL REFERENCES Evidence(evidence_id),
    -- TRANSFER
    transferred_from BIGINT REFERENCES Officers(officer_id),
    -- NULL on first entry = initial collection
    transferred_to BIGINT NOT NULL REFERENCES Officers(officer_id),
    transfer_reason TEXT NOT NULL,
    transferred_at TIMESTAMPTZ DEFAULT NOW(),
    -- STATUS AT TIME OF TRANSFER
    status_at_transfer evidence_status_enum NOT NULL,
    -- META
    notes TEXT,
    CONSTRAINT chk_different_custodians CHECK (
        transferred_from IS NULL
        OR transferred_from <> transferred_to
    )
);

CREATE TABLE Warrants (
    -- IDENTITY
    warrant_id BIGSERIAL PRIMARY KEY,
    warrant_number VARCHAR(30) UNIQUE NOT NULL,
    -- format: WRT-YYYY-STATIONCODE-NNNN
    -- generated via trigger on INSERT
    -- LINKAGE
    case_id BIGINT NOT NULL REFERENCES Cases(case_id),
    -- warrant may target an accused person
    accused_cnic VARCHAR(15) REFERENCES Persons(cnic),
    -- NULL for search/seizure warrants targeting a location
    -- WARRANT DETAILS
    warrant_type warrant_type_enum NOT NULL,
    warrant_status warrant_status_enum NOT NULL DEFAULT 'ISSUED',
    issuing_court VARCHAR(150) NOT NULL,
    magistrate_name VARCHAR(100) NOT NULL,
    issue_date DATE NOT NULL,
    valid_until DATE NOT NULL,
    target_address TEXT,
    -- location for SEARCH and SEIZURE warrants
    -- REQUESTED BY
    requested_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    -- must be INSPECTOR or above (enforced via trigger)
    -- EXECUTION
    executed_by BIGINT REFERENCES Officers(officer_id),
    executed_at TIMESTAMPTZ,
    -- CANCELLATION
    cancelled_by BIGINT REFERENCES Officers(officer_id),
    -- must be SHO or above (enforced via trigger)
    cancelled_at TIMESTAMPTZ,
    cancellation_reason TEXT,
    -- META
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    -- CONSTRAINTS
    CONSTRAINT chk_valid_until_after_issue CHECK (
        valid_until IS NULL OR valid_until > issue_date
        ),
    CONSTRAINT chk_execution_requires_officer CHECK (
        executed_at IS NULL
        OR executed_by IS NOT NULL
    ),
    CONSTRAINT chk_cancellation_requires_reason CHECK (
        cancelled_at IS NULL
        OR (
            cancelled_by IS NOT NULL
            AND cancellation_reason IS NOT NULL
        )
    ),
    CONSTRAINT chk_not_both_executed_and_cancelled CHECK (
        executed_at IS NULL
        OR cancelled_at IS NULL
    ),
    CONSTRAINT chk_search_warrant_needs_address CHECK (
        warrant_type = 'ARREST'
        OR target_address IS NOT NULL
    )
);

CREATE TABLE Arrests (
    -- IDENTITY
    arrest_id BIGSERIAL PRIMARY KEY,
    arrest_number VARCHAR(30) UNIQUE NOT NULL,
    -- format: ARR-YYYY-STATIONCODE-NNNN
    -- generated via trigger on INSERT
    -- LINKAGE
    -- who was arrested
    accused_cnic VARCHAR(15) NOT NULL REFERENCES Persons(cnic),
    -- which case this arrest is under
    case_id BIGINT NOT NULL REFERENCES Cases(case_id),
    -- warrant used for this arrest if any
    warrant_id BIGINT REFERENCES Warrants(warrant_id),
    -- ARREST DETAILS
    arresting_officer_id BIGINT NOT NULL REFERENCES Officers(officer_id),
    arrested_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    arrest_location TEXT NOT NULL,
    -- CUSTODY
    custody_status custody_status_enum NOT NULL DEFAULT 'IN_CUSTODY',
    custody_released_at TIMESTAMPTZ,
    release_reason TEXT,
    -- DISPUTE
    is_disputed BOOLEAN DEFAULT FALSE,
    dispute_reason TEXT,
    -- META
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),
    -- CONSTRAINTS
    CONSTRAINT chk_release_requires_reason CHECK (
        custody_released_at IS NULL
        OR release_reason IS NOT NULL
    ),
    CONSTRAINT chk_dispute_requires_reason CHECK (
        is_disputed = FALSE
        OR dispute_reason IS NOT NULL
    ),
    CONSTRAINT chk_release_after_arrest CHECK (
        custody_released_at IS NULL
        OR custody_released_at > arrested_at
    )
);

CREATE TABLE IF NOT EXISTS Bail_Records (
    bail_id BIGSERIAL PRIMARY KEY,
    bail_number VARCHAR(30) UNIQUE NOT NULL,

    arrest_id BIGINT NOT NULL REFERENCES arrests(arrest_id),

    court_name VARCHAR(150),
    magistrate_name VARCHAR(150),
    bail_date DATE NOT NULL,

    bail_type bail_type_enum NOT NULL,
    bail_status bail_status_enum NOT NULL DEFAULT 'ACTIVE',
    bail_amount DECIMAL(12,2),

    surety_name VARCHAR(100),
    surety_cnic VARCHAR(15) REFERENCES Persons(cnic),
    surety_address TEXT,

    valid_until DATE,

    revoked_at TIMESTAMPTZ,
    revocation_reason TEXT,
    revoked_by BIGINT REFERENCES Officers(officer_id),

    recorded_by BIGINT NOT NULL REFERENCES Officers(officer_id),

    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),


    CONSTRAINT chk_bail_amount_positive CHECK (bail_amount IS NULL OR bail_amount > 0),
    CONSTRAINT chk_valid_until_after_bail_date CHECK (
        valid_until IS NULL OR valid_until > bail_date
        ),
    CONSTRAINT chk_revocation_requires_reason CHECK (
        revoked_at IS NULL
        OR (
            revocation_reason IS NOT NULL
            AND revoked_by IS NOT NULL
        )
    ),
    CONSTRAINT chk_surety_info CHECK (
        surety_name IS NULL
        OR (
            surety_cnic IS NOT NULL
            AND surety_address IS NOT NULL
        )
    )       
);

CREATE TABLE Forensic_Lab_Requests (
    request_id BIGSERIAL PRIMARY KEY,
    request_number VARCHAR(30) UNIQUE NOT NULL,
    case_id BIGINT NOT NULL REFERENCES Cases(case_id),
    lab_name VARCHAR(150) NOT NULL,
    examiner_name VARCHAR(100),
    examination_purpose examination_purpose_enum NOT NULL,
    purpose_description TEXT,
    sent_date DATE NOT NULL DEFAULT CURRENT_DATE,
    received_by_lab_date DATE,
    report_expected_date DATE,
    request_status forsenic_request_status_enum NOT NULL DEFAULT 'REQUESTED',
    authorized_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    findings TEXT,
    report_file_path VARCHAR(255),
    report_delivered_date DATE,
    is_amended BOOLEAN DEFAULT FALSE,
    amendment_notes TEXT,
    amended_at TIMESTAMPTZ,
    amended_by BIGINT REFERENCES officers(officer_id),
    is_contested BOOLEAN DEFAULT FALSE,
    contest_reason TEXT,
    contested_by BIGINT REFERENCES officers(officer_id),
    contested_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),

    CONSTRAINT chk_report_requires_findings
        CHECK(
            report_delivered_date IS NULL
            OR findings IS NOT NULL
            OR report_file_path IS NOT NULL
        ),
    CONSTRAINT chk_amendment_requires_notes
        CHECK(
            is_amended = FALSE
            OR ( amendment_notes IS NOT NULL
            AND amended_at IS NOT NULL
            AND amended_by IS NOT NULL
            )
        ),
    CONSTRAINT chk_contest_requires_reason
        CHECK(
            is_contested = FALSE
            OR ( contest_reason IS NOT NULL
            AND contested_at IS NOT NULL
            AND contested_by IS NOT NULL
            )
        ),
    CONSTRAINT chk_report_received_after_sent
        CHECK (
            received_by_lab_date IS NULL
            OR received_by_lab_date >= sent_date
        ),
    CONSTRAINT chk_report_delivered_after_sent
        CHECK (
            report_delivered_date IS NULL
            OR received_by_lab_date IS NULL
            OR report_delivered_date >= received_by_lab_date
        )
);

CREATE TABLE Forensic_Request_Evidence (
    request_id BIGINT NOT NULL REFERENCES Forensic_Lab_Requests(request_id) ON DELETE CASCADE,
    evidence_id BIGINT NOT NULL REFERENCES Evidence(evidence_id) ON DELETE CASCADE,
    notes TEXT,
    added_at TIMESTAMPTZ DEFAULT NOW(),
    CONSTRAINT pk_forsenic_request_evidence
        PRIMARY KEY (request_id, evidence_id)
);

CREATE TABLE charge_sheets (
    charge_sheet_id BIGSERIAL PRIMARY KEY,
    charge_sheet_number VARCHAR(30) UNIQUE NOT NULL,

    case_id BIGINT NOT NULL REFERENCES Cases(case_id),
    parent_charge_sheet_id BIGINT REFERENCES charge_sheets(charge_sheet_id),
    sheet_type sheet_type_enum NOT NULL DEFAULT 'ORIGINAL',
    charge_sheet_status charge_sheet_status_enum NOT NULL DEFAULT 'DRAFT',
    court_name VARCHAR(150),
    magistrate_name VARCHAR(100),
    laws_invoked TEXT[] NOT NULL DEFAULT '{}',
    filed_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    filing_date DATE,
    submitted_to_court_at TIMESTAMPTZ,
    submitted_by BIGINT REFERENCES OFFICERS(officer_id),
    court_response_date DATE,
    court_remarks TEXT,
    is_locked BOOLEAN DEFAULT FALSE,
    locked_at TIMESTAMPTZ,
    locked_by BIGINT REFERENCES officers(officer_id),
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),

    CONSTRAINT chk_supplementary_requires_parent CHECK (
        sheet_type = 'ORIGINAL'
        OR parent_charge_sheet_id IS NOT NULL
    ),
    CONSTRAINT chk_no_self_parent CHECK (
        parent_charge_sheet_id IS NULL
        OR parent_charge_sheet_id <> charge_sheet_id
    ),
    CONSTRAINT chk_filed_date_required CHECK (
        charge_sheet_status = 'DRAFT'
        OR filing_date IS NOT NULL
    ),
    CONSTRAINT chk_lock_requires_officer CHECK (
        is_locked = FALSE 
        OR (locked_at IS NOT NULL
        AND locked_by IS NOT NULL)
    ),
    CONSTRAINT chk_laws_invoked_not_empty CHECK (
        charge_sheet_status = 'DRAFT'
        OR array_length(laws_invoked, 1) > 0
    )
);

CREATE TABLE charge_sheet_accused (
    charge_sheet_id BIGINT NOT NULL REFERENCES charge_sheets(charge_sheet_id) ON DELETE CASCADE,
    accused_cnic VARCHAR(15) NOT NULL REFERENCES persons(cnic) ON DELETE CASCADE,
    specific_charges TEXT[] NOT NULL DEFAULT '{}',
    remarks TEXT,
    added_by BIGINT NOT NULL REFERENCES officers(officer_id),
    added_at TIMESTAMPTZ DEFAULT NOW(),
    CONSTRAINT pk_charge_sheet_accused
        PRIMARY KEY (charge_sheet_id, accused_cnic),
    CONSTRAINT chk_specific_charges_not_empty
        CHECK (array_length(specific_charges, 1) > 0)
);

CREATE TABLE vehicles (
    vehicle_id BIGSERIAL PRIMARY KEY,
    registration_number VARCHAR(20) UNIQUE NOT NULL,  --plate_number
    chassis_number VARCHAR(50) UNIQUE,
    engine_number VARCHAR(50) UNIQUE,
    vehicle_type vehicle_type_enum NOT NULL,
    make VARCHAR(50),
    model VARCHAR(50),
    model_year SMALLINT,
    color VARCHAR(30) NOT NULL,
    registered_state VARCHAR(50),
    registered_owner_cnic VARCHAR(15) REFERENCES Persons(cnic),
    registered_owner_name VARCHAR(100),
    seizure_status seizure_status_enum NOT NULL DEFAULT 'NOT_SEIZED',
    seized_at TIMESTAMPTZ,
    seized_by BIGINT REFERENCES Officers(officer_id),
    seizure_location TEXT,
    released_at TIMESTAMPTZ,
    released_by BIGINT REFERENCES Officers(officer_id),
    release_reason TEXT, 
    created_at TIMESTAMPTZ DEFAULT NOW(),
    updated_at TIMESTAMPTZ DEFAULT NOW(),

    CONSTRAINT chk_model_year_valid
        CHECK (
            model_year IS NULL
            OR (model_year >= 1980
            AND model_year <= EXTRACT(YEAR FROM CURRENT_DATE) + 1)
        ),
    CONSTRAINT chk_seizure_requires_officer
        CHECK (
            seized_at IS NULL
            OR seized_by IS NOT NULL
        ),
    CONSTRAINT chk_release_after_seizure
        CHECK (
            released_at IS NULL
            OR seized_at IS NULL
            OR released_at > seized_at
        ),
    CONSTRAINT chk_release_requires_reason
        CHECK (
            released_at IS NULL
            OR release_reason IS NOT NULL
        )
);

CREATE TABLE Vehicle_cases (
    vehicle_case_id BIGSERIAL PRIMARY KEY,
    vehicle_id BIGINT NOT NULL REFERENCES vehicles(vehicle_id) ON DELETE CASCADE,
    case_id BIGINT NOT NULL REFERENCES Cases(case_id) ON DELETE CASCADE,
    vehicle_role vehicle_role_enum NOT NULL,
    condition_notes TEXT,
    added_by BIGINT NOT NULL REFERENCES Officers(officer_id),
    added_at TIMESTAMPTZ DEFAULT NOW(),
    CONSTRAINT uq_vehicle_case_role
        UNIQUE (vehicle_id, case_id, vehicle_role)
);

CREATE TABLE Patrol_Routes (
    route_id                BIGSERIAL PRIMARY KEY,
    beat_code               VARCHAR(20) UNIQUE NOT NULL,
    route_name              VARCHAR(100) NOT NULL,
    area_description        TEXT NOT NULL,
    landmarks               TEXT[],
    station_id              BIGINT NOT NULL REFERENCES Stations(station_id),
    is_active               BOOLEAN DEFAULT TRUE,
    created_at              TIMESTAMPTZ DEFAULT NOW(),
    updated_at              TIMESTAMPTZ DEFAULT NOW()
);

CREATE TABLE Duty_Roster (
    duty_id                 BIGSERIAL PRIMARY KEY,
    duty_number             VARCHAR(30) UNIQUE NOT NULL,
    -- format: DUTY-YYYY-STATIONCODE-NNNN
    -- generated via trigger on INSERT
    officer_id              BIGINT NOT NULL REFERENCES Officers(officer_id),
    station_id              BIGINT NOT NULL REFERENCES Stations(station_id),
    patrol_route_id         BIGINT REFERENCES Patrol_Routes(route_id),
    shift_type              shift_type_enum NOT NULL,
    duty_date               DATE NOT NULL,
    scheduled_start         TIMESTAMPTZ NOT NULL,
    scheduled_end           TIMESTAMPTZ NOT NULL,
    actual_start            TIMESTAMPTZ,
    actual_end              TIMESTAMPTZ,
    duty_status             duty_status_enum NOT NULL DEFAULT 'SCHEDULED',
    absence_reason          TEXT,
    assigned_by             BIGINT NOT NULL REFERENCES Officers(officer_id),
    -- META
    created_at              TIMESTAMPTZ DEFAULT NOW(),
    updated_at              TIMESTAMPTZ DEFAULT NOW(),
    -- CONSTRAINTS
    CONSTRAINT chk_scheduled_end_after_start
        CHECK (scheduled_end > scheduled_start),
    CONSTRAINT chk_actual_end_after_start
        CHECK (
            actual_end IS NULL
            OR actual_start IS NULL
            OR actual_end > actual_start
        ),
    CONSTRAINT chk_absence_requires_reason
        CHECK (
            duty_status <> 'ABSENT'
            OR absence_reason IS NOT NULL
        )
);

CREATE TABLE audit.Audit_Log (
    audit_id                BIGSERIAL PRIMARY KEY,

    -- WHAT CHANGED
    table_name              audited_table_enum NOT NULL,
    record_id               BIGINT NOT NULL,
    -- PK value of the changed record
    action                  audit_action_enum NOT NULL,

    -- CHANGE DETAILS
    -- full row snapshots as JSONB
    old_value               JSONB,
    -- NULL on INSERT
    new_value               JSONB,
    -- NULL on DELETE

    -- both DB session user and application officer
    changed_by_user         VARCHAR(100) NOT NULL,
    -- pg session user e.g. 'justice_app'
    changed_by_officer_id   BIGINT,
    -- FK to Officers — nullable if system triggered
    changed_by_belt         VARCHAR(20),
    -- denormalized for quick reading without JOIN

    -- PROCESS CONTEXT (OS layer integration)
    client_process_id       INT,
    -- PID of the process that made the change
    client_ip               INET,
    -- IP address of the client

    changed_at              TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- CONSTRAINTS
    CONSTRAINT chk_insert_has_new_value
        CHECK (
            action <> 'INSERT'
            OR new_value IS NOT NULL
        ),
    CONSTRAINT chk_delete_has_old_value
        CHECK (
            action <> 'DELETE'
            OR old_value IS NOT NULL
        ),
    CONSTRAINT chk_update_has_both_values
        CHECK (
            action <> 'UPDATE'
            OR (old_value IS NOT NULL
                AND new_value IS NOT NULL)
        )
);

-- Output of AI Agent 1: DBSCAN Crime Hotspot Analyzer
-- Populated by Python ML process on schedule
CREATE TABLE analytics.Crime_Hotspots (
    -- IDENTITY
    hotspot_id              BIGSERIAL PRIMARY KEY,

    -- LOCATION
    zone_label              VARCHAR(50) NOT NULL,
    -- e.g. 'Zone-A', 'Zone-B'
    center_lat              DECIMAL(9,6) NOT NULL,
    center_lon              DECIMAL(9,6) NOT NULL,
    radius_meters           INT NOT NULL,
    -- approximate coverage radius of the cluster
    area_description        TEXT,
    -- human readable e.g. 'Kharadar market area'

    -- CLUSTER STATS
    case_count              INT NOT NULL,
    -- number of cases that formed this cluster
    dominant_case_type      case_type_enum NOT NULL,
    -- most frequent crime type in this cluster
    case_type_breakdown     JSONB,
    -- e.g. {"THEFT": 12, "ROBBERY": 8, "ASSAULT": 3}

    -- RISK
    risk_level              hotspot_risk_level_enum NOT NULL,
    risk_score              DECIMAL(5,4) NOT NULL,
    -- 0.0000 to 1.0000

    -- PATROL RECOMMENDATION
    patrol_increase_pct     SMALLINT,
    -- e.g. 40 means increase patrol by 40%
    recommendation_text     TEXT,
    -- e.g. 'Increase patrol frequency by 40% in this zone'

    -- ANALYSIS WINDOW
    analysis_from           DATE NOT NULL,
    -- start of data window used
    analysis_to             DATE NOT NULL,
    -- end of data window used
    analyzed_at             TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    -- MODEL METADATA
    model_version           VARCHAR(20) NOT NULL,
    -- e.g. 'DBSCAN-v1.2'
    algorithm               VARCHAR(50) NOT NULL DEFAULT 'DBSCAN',
    epsilon                 DECIMAL(6,4),
    -- DBSCAN epsilon parameter
    min_samples             INT,
    -- DBSCAN min_samples parameter

    -- CONSTRAINTS
    CONSTRAINT chk_risk_score_range
        CHECK (risk_score BETWEEN 0.0 AND 1.0),
    CONSTRAINT chk_case_count_positive
        CHECK (case_count > 0),
    CONSTRAINT chk_analysis_window
        CHECK (analysis_to > analysis_from),
    CONSTRAINT chk_patrol_pct_positive
        CHECK (
            patrol_increase_pct IS NULL
            OR patrol_increase_pct > 0
        )
);

-- Output of AI Agent 2: Random Forest Case Priority Recommender
-- One row per analysis run per case
-- Case can be re-analyzed multiple times
CREATE TABLE analytics.Case_Priority_Scores (
    -- IDENTITY
    score_id                BIGSERIAL PRIMARY KEY,

    -- LINKAGE
    case_id                 BIGINT NOT NULL,
    -- intentionally no FK — AI schema is read-only
    -- referential integrity maintained by application

    -- PRIORITY
    priority_level          priority_level_enum NOT NULL,
    priority_score          DECIMAL(5,4) NOT NULL,
    -- 0.0000 to 1.0000

    -- SHAP EXPLAINABILITY
    -- feature contributions that produced this score
    -- e.g. {
    --   "days_since_filing": 0.40,
    --   "evidence_count": 0.30,
    --   "prior_convictions": 0.30
    -- }
    feature_contributions   JSONB NOT NULL,

    -- TOP REASON
    top_reason              TEXT NOT NULL,
    -- human readable explanation
    -- e.g. 'Filed 45 days ago with only 1 evidence item'

    -- SUGGESTED ACTION
    suggested_action        TEXT,
    -- e.g. 'Assign additional investigator immediately'

    -- INPUT FEATURES SNAPSHOT
    -- snapshot of features used at time of analysis
    -- preserves explainability even if case data changes
    input_features          JSONB NOT NULL,

    -- MODEL METADATA
    analyzed_at             TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    model_version           VARCHAR(20) NOT NULL,
    -- e.g. 'RF-v2.1'
    algorithm               VARCHAR(50) NOT NULL
                            DEFAULT 'RandomForest',
    model_accuracy          DECIMAL(5,4),
    -- accuracy of model at time of this prediction

    -- CONSTRAINTS
    CONSTRAINT chk_priority_score_range
        CHECK (priority_score BETWEEN 0.0 AND 1.0),
    CONSTRAINT chk_model_accuracy_range
        CHECK (
            model_accuracy IS NULL
            OR model_accuracy BETWEEN 0.0 AND 1.0
        )
);

-- Output of AI Agent 3: Hungarian Algorithm Workload Balancer
-- Suggestions only — SHO must accept or reject
CREATE TABLE analytics.Officer_Workload_Assignments (
    -- IDENTITY
    assignment_id           BIGSERIAL PRIMARY KEY,

    -- LINKAGE
    case_id                 BIGINT NOT NULL,
    officer_id              BIGINT NOT NULL,
    -- both intentionally no FK
    -- AI schema is isolated

    -- RECOMMENDATION
    assignment_status       assignment_status_enum
                            NOT NULL DEFAULT 'SUGGESTED',
    cost_score              DECIMAL(8,4) NOT NULL,
    -- lower = better fit
    -- from Hungarian algorithm cost matrix

    -- EXPLANATION
    recommendation_reason   TEXT NOT NULL,
    -- e.g. 'Best fit: fraud expertise,
    --        3 active cases, nearest station'

    -- CONTRIBUTING FACTORS
    -- e.g. {
    --   "workload_penalty": 0.3,
    --   "skill_match": 0.5,
    --   "geographic_distance": 0.2
    -- }
    cost_breakdown          JSONB NOT NULL,

    -- WORKLOAD SNAPSHOT AT TIME OF SUGGESTION
    officer_active_cases    SMALLINT NOT NULL,
    -- active cases officer had at analysis time
    officer_workload_score  DECIMAL(5,4) NOT NULL,
    -- normalized workload 0.0 to 1.0

    -- HUMAN DECISION
    decided_by              BIGINT,
    -- officer_id of SHO who accepted/rejected
    decided_at              TIMESTAMPTZ,
    decision_notes          TEXT,

    -- AUTO EXPIRY
    expires_at              TIMESTAMPTZ NOT NULL,
    -- suggestion expires if not acted upon
    -- set by OS job scheduler

    -- MODEL METADATA
    analyzed_at             TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    model_version           VARCHAR(20) NOT NULL,
    algorithm               VARCHAR(50) NOT NULL
                            DEFAULT 'HungarianAlgorithm',

    -- CONSTRAINTS
    CONSTRAINT chk_cost_score_positive
        CHECK (cost_score >= 0),
    CONSTRAINT chk_workload_score_range
        CHECK (officer_workload_score BETWEEN 0.0 AND 1.0),
    CONSTRAINT chk_decision_requires_officer
        CHECK (
            assignment_status
                NOT IN ('ACCEPTED', 'REJECTED')
            OR decided_by IS NOT NULL
        ),
    CONSTRAINT chk_expires_after_analysis
        CHECK (expires_at > analyzed_at)
);

-- Tracks model accuracy over time
-- Used to decide when retraining is needed
CREATE TABLE analytics.Model_Performance_Log (
    -- IDENTITY
    log_id                  BIGSERIAL PRIMARY KEY,

    -- MODEL IDENTITY
    model_name              VARCHAR(50) NOT NULL,
    -- e.g. 'CrimeHotspotDetector',
    --       'CasePriorityRecommender',
    --       'WorkloadBalancer'
    model_version           VARCHAR(20) NOT NULL,
    algorithm               VARCHAR(50) NOT NULL,

    -- PERFORMANCE METRICS
    accuracy                DECIMAL(5,4),
    precision_score         DECIMAL(5,4),
    recall_score            DECIMAL(5,4),
    f1_score                DECIMAL(5,4),
    -- for hotspot detector
    hotspot_precision       DECIMAL(5,4),

    -- TRAINING DATA
    training_sample_size    INT NOT NULL,
    training_from           DATE NOT NULL,
    training_to             DATE NOT NULL,

    -- THRESHOLDS
    -- did this version meet minimum requirements?
    meets_threshold         BOOLEAN NOT NULL,
    -- hotspot precision > 0.70
    -- priority accuracy > 0.75
    threshold_notes         TEXT,

    -- META
    evaluated_at            TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    evaluated_by            VARCHAR(50) NOT NULL,
    -- e.g. 'OS_JobScheduler', 'Manual'

    -- CONSTRAINTS
    CONSTRAINT chk_accuracy_range
        CHECK (accuracy IS NULL
               OR accuracy BETWEEN 0.0 AND 1.0),
    CONSTRAINT chk_training_window
        CHECK (training_to > training_from)
);

-- Sequence registry — guarantees unique NNNN per entity/station/year
-- No application code ever touches this directly
CREATE TABLE Sequence_Registry (
    seq_id      BIGSERIAL PRIMARY KEY,
    entity      VARCHAR(20)  NOT NULL,
    -- e.g. 'FIR', 'ARR', 'WRT', 'BAIL', 'FLR', 'CS', 'DUTY', 'EVD'
    scope_key   VARCHAR(50)  NOT NULL,
    -- e.g. 'KHD-2024' for station-year scoped
    --       'FIR-2024-KHD-0001' for evidence scoped to a FIR
    last_value  INT          NOT NULL DEFAULT 0,

    CONSTRAINT uq_sequence_entity_scope
        UNIQUE (entity, scope_key)
);