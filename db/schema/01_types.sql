-- =============================
-- ENUM TYPES
-- =============================
CREATE TYPE station_type_enum AS ENUM (
    'ZONE_HQ',
    'DISTRICT_HQ',
    'DIVISION_HQ',
    'POLICE_STATION',
    'SUB_STATION'
);

CREATE TYPE officer_rank_enum AS ENUM (
    'CONSTABLE',
    'HEAD_CONSTABLE',
    'ASI',
    'SI',
    'INSPECTOR',
    'DSP',
    'SP',
    'SSP',
    'DIG',
    'ADDL_IG',
    'IGP'
);

CREATE TYPE officer_status_enum AS ENUM (
    'ACTIVE',
    'SUSPENDED',
    'ON_LEAVE',
    'RETIRED',
    'TERMINATED'
);

CREATE TYPE case_officer_role_enum AS ENUM (
    'DUTY_INCHARGE',
    'SIO',
    'IO',
    'LEAD_INVESTIGATOR',
    'SUPPORTING',
    'EVIDENCE_CUSTODIAN'
);

CREATE TYPE gender_enum AS ENUM ('MALE', 'FEMALE', 'OTHER');

CREATE TYPE relationship_to_victim_enum AS ENUM (
    'SELF',
    'PARENT',
    'SPOUSE',
    'SIBLING',
    'CHILD',
    'RELATIVE',
    'WITNESS',
    'THIRD_PARTY',
    'OTHER'
);

CREATE TYPE complainant_status_enum AS ENUM (
    'ACTIVE',
    'WITHDRAWN',
    'DECEASED',
    'UNREACHABLE'
);

CREATE TYPE injury_severity_enum AS ENUM (
    'NONE',
    'MINOR',
    'MODERATE',
    'SEVERE',
    'FATAL'
);

CREATE TYPE vulnerability_category_enum AS ENUM (
    'NONE',
    'MINOR',
    'ELDERLY',
    'DIFFERENTLY_ABLED',
    'FEMALE_ALONE'
);

CREATE TYPE witness_protection_enum AS ENUM (
    'NONE',
    'MONITORED',
    'PROTECTED',
    'RELOCATED'
);

CREATE TYPE involvement_type_enum AS ENUM (
    'SUSPECT',
    'ACCUSED',
    'CONVICTED',
    'ACQUITTED'
);

CREATE TYPE association_type_enum AS ENUM (
    'CO_ACCUSED',
    'GANG_MEMBER',
    'ACCOMPLICE',
    'FAMILY',
    'KNOWN_ASSOCIATE'
);

CREATE TYPE case_status_enum AS ENUM (
    'REGISTERED',
    'UNDER_INVESTIGATION',
    'EVIDENCE_COLLECTED',
    'PENDING_TRIAL',
    'CLOSED',
    'REOPENED'
);

CREATE TYPE approval_status_enum AS ENUM (
    'NOT_REQUIRED',
    'PENDING_APPROVAL',
    'APPROVED',
    'REJECTED'
);

CREATE TYPE evidence_type_enum AS ENUM (
    -- weapon, clothing, objects
    'PHYSICAL',
    -- phone, hard drive, CCTV footage
    'DIGITAL',
    -- recorded statement as evidence
    'TESTIMONIAL',
    -- lab samples, DNA, fingerprints
    'FORENSIC',
    -- contracts, letters, photographs
    'DOCUMENTARY'
);

CREATE TYPE evidence_status_enum AS ENUM (
    'RECEIVED',
    'SEALED',
    'SENT_TO_LAB',
    'RETURNED_FROM_LAB',
    'PRODUCED_IN_COURT',
    'DISPOSED'
);

CREATE TYPE custody_status_enum AS ENUM (
    'IN_CUSTODY',
    'BAIL_GRANTED',
    'REMANDED',
    'RELEASED',
    'ESCAPED'
);

CREATE TYPE warrant_type_enum AS ENUM ('ARREST', 'SEARCH', 'SEIZURE');

CREATE TYPE warrant_status_enum AS ENUM (
    'ISSUED',
    'EXECUTED',
    'CANCELLED',
    'EXPIRED'
);

CREATE TYPE bail_type_enum AS ENUM (
    'REGULAR',
    'ANTICIPTORY',
    'INTERIM',
    'SURETY'
);

CREATE TYPE bail_status_enum AS ENUM (
    'ACTIVE',
    'REVOKED',
    'EXPIRED',
    'CANCELLED'
);

CREATE TYPE forsenic_request_status_enum AS ENUM (
    'REQUESTED',
    'RECEIVED_BY_LAB',
    'UNDER_EXAMINATION',
    'REPORT_READY',
    'REPORT_DELIVERED',
    'CONTESTED'
);

CREATE TYPE examination_purpose_enum AS ENUM (
    'DNA_ANALYSIS',
    'FINGERPRINT_ANALYSIS',
    'BALLISTICS_ANALYSIS',
    'TOXICOLOGY_ANALYSIS',
    'DIGITAL_FORENSICS',
    'DOCUMENT_EXAMINATION',
    'BLOOD_ANALYSIS',
    'NARCOTICS_TESTING',
    'TRACE_EVIDENCE_ANALYSIS',  
    'OTHER'
);

CREATE TYPE examination_result_enum AS ENUM (
    'INCONCLUSIVE',
    'MATCH_FOUND',
    'NO_MATCH',
    'PARTIAL_MATCH',
    'EXCLUDED',
    'PENDING'
);

CREATE TYPE charge_sheet_status_enum AS ENUM (
    'DRAFT',
    'FILED',
    'SUBMITTED_TO_COURT',
    'ACCEPTED_BY_COURT',
    'REJECTED_BY_COURT'
);

CREATE TYPE sheet_type_enum AS ENUM (
    'ORIGINAL',
    'SUPPLEMENTARY'
);

CREATE TYPE vehicle_role_enum AS ENUM (
    'STOLEN',
    'USED_IN_CRIME',
    'ABANDONED',
    'EVIDENCE',
    'SUSPECTS_VEHICLE',
    'VICTIMS_VEHICLE',

);

CREATE TYPE seizure_status_enum AS ENUM (
    'SEIZED',
    'NOT_SEIZED',
    'RELEASED',
    'AUCTIONED',
    'DESTROYED',
    'RETAINED_FOR_EVIDENCE'
);

CREATE TYPE vehicle_type_enum AS ENUM (
    'CAR',
    'MOTORCYCLE',
    'TRUCK',
    'BUS',
    'VAN',
    'SUV',
    'RICKSHAW',
    'OTHER'
);

CREATE TYPE shift_type_enum AS ENUM (
    'MORNING',      
    'AFTERNOON',    
    'NIGHT',        
    'SPLIT',        
    'ON_CALL'       
);

CREATE TYPE duty_status_enum AS ENUM (
    'SCHEDULED',    
    'ON_DUTY',      
    'COMPLETED',    
    'ABSENT',       
    'ON_LEAVE',     
    'SUSPENDED'     
);

CREATE TYPE audit_action_enum AS ENUM (
    'INSERT',
    'UPDATE',
    'DELETE'    -- only soft deletes reach here
                -- hard deletes are blocked by triggers
);

CREATE TYPE audited_table_enum AS ENUM (
    'Cases',
    'Evidence',
    'Officers',
    'Arrests',
    'Warrants',
    'Charge_Sheets',
    'Bail_Records',
    'Accused'
);

CREATE TYPE hotspot_risk_level_enum AS ENUM (
    'LOW',
    'MEDIUM',
    'HIGH',
    'CRITICAL'
);

CREATE TYPE priority_level_enum AS ENUM (
    'LOW',
    'MEDIUM',
    'HIGH',
    'CRITICAL'
);

CREATE TYPE assignment_status_enum AS ENUM (
    'SUGGESTED',        -- AI recommendation pending human review
    'ACCEPTED',         -- SHO accepted the suggestion
    'REJECTED',         -- SHO rejected the suggestion
    'AUTO_EXPIRED'      -- suggestion expired without action
);

