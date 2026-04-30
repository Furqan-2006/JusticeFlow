#pragma once

namespace JusticeFlow
{
    /**
     * =============================
     *    * SYSTEM RETURN CODES *
     * =============================
     */

    enum class ResultCode
    {
        OK = 0,

        // Identity & Access
        AUTH_FAILED = 1,
        RANK_INSUFFICIENT = 2,
        SESSION_EXPIRED = 3,
        JURISDICTION_DENIED = 4,

        // Basic CRUD
        NOT_FOUND = 10,
        ALREADY_EXISTS = 11,
        INVALID_INPUT = 12,
        INVALID_ARGUMENT = 13,

        // System & DB
        DB_ERROR = 20,
        FOREIGN_KEY_VIOLATION = 21,
        FILE_SYSTEM_ERROR = 22,

        // Workflow Logic
        INVALID_STATE = 30,
        DUTY_INACTIVE = 31,
        RECORD_LOCKED = 32,

        // Analytics & AI
        ANALYSIS_FAILED = 40,
        THRESHOLD_NOT_MET = 41
    };

    /**
     * =======================
     *    * DB ENUM TYPES *
     * =======================
     */

    enum class StationType
    {
        ZONE_HQ,
        DISTRICT_HQ,
        DIVISION_HQ,
        POLICE_STATION,
        SUB_STATION
    };

    enum class OfficerRank
    {
        CONSTABLE,
        HEAD_CONSTABLE,
        ASI,
        SI,
        INSPECTOR,
        DSP,
        SP,
        SSP,
        DIG,
        ADDL_IG,
        IGP
    };
    enum class OfficerStatus
    {
        ACTIVE,
        SUSPENDED,
        ON_LEAVE,
        RETIRED,
        TERMINATED
    };
    enum class CaseOfficerRole
    {
        DUTY_INCHARGE,
        SIO,
        IO,
        LEAD_INVESTIGATOR,
        SUPPORTING,
        EVIDENCE_CUSTODIAN
    };
    enum class Gender
    {
        MALE,
        FEMALE,
        OTHER
    };
    enum class RelationshipToVictim
    {
        SELF,
        PARENT,
        SPOUSE,
        SIBLING,
        CHILD,
        RELATIVE,
        WITNESS,
        THIRD_PARTY,
        OTHER
    };
    enum class ComplainantStatus
    {
        ACTIVE,
        WITHDRAWN,
        DECEASED,
        UNREACHABLE
    };
    enum class InjurySeverity
    {
        NONE,
        MINOR,
        MODERATE,
        SEVERE,
        FATAL
    };
    enum class VulnerabilityCategory
    {
        NONE,
        MINOR,
        ELDERLY,
        DIFFERENTLY_ABLED,
        FEMALE_ALONE
    };
    enum class WitnessProtection
    {
        NONE,
        MONITORED,
        PROTECTED,
        RELOCATED
    };
    enum class InvolvementType
    {
        SUSPECT,
        ACCUSED,
        CONVICTED,
        ACQUITTED
    };
    enum class AssociationType
    {
        CO_ACCUSED,
        GANG_MEMBER,
        ACCOMPLICE,
        FAMILY,
        KNOWN_ASSOCIATE
    };
    enum class CaseType
    {
        // Criminal
        MURDER,
        ATTEMPTED_MURDER,
        MANSLAUGHTER,
        KIDNAPPING,
        HUMAN_TRAFFICKING,
        ROBBERY,
        ARMED_ROBBERY,
        ASSAULT,
        AGGRAVATED_ASSAULT,
        RAPE,
        SEXUAL_ASSAULT,
        BURGLARY,
        HOME_INVASION,
        ARSON,
        VANDALISM,
        DRUG_TRAFFICKING,
        DRUG_POSSESSION,
        TERRORISM,
        EXTORTION,
        GANG_ACTIVITY,
        // Civilian
        THEFT,
        FRAUD,
        CYBERCRIME,
        HIT_AND_RUN,
        VEHICLE_THEFT,
        DOMESTIC_VIOLENCE,
        HARASSMENT,
        BRIBERY,
        FORGERY,
        PUBLIC_DISTURBANCE
    };
    enum class CaseStatus
    {
        REGISTERED,
        UNDER_INVESTIGATION,
        EVIDENCE_COLLECTED,
        PENDING_TRIAL,
        CLOSED,
        REOPENED
    };
    enum class ApprovalStatus
    {
        NOT_REQUIRED,
        PENDING_APPROVAL,
        APPROVED,
        REJECTED
    };
    enum class EvidenceType
    {
        PHYSICAL,
        DIGITAL,
        TESTIMONIAL,
        FORENSIC,
        DOCUMENTARY
    };
    enum class EvidenceStatus
    {
        RECEIVED,
        SEALED,
        SENT_TO_LAB,
        RETURNED_FROM_LAB,
        PRODUCED_IN_COURT,
        DISPOSED
    };
    enum class CustodyStatus
    {
        IN_CUSTODY,
        BAIL_GRANTED,
        REMANDED,
        RELEASED,
        ESCAPED
    };
    enum class WarrantType
    {
        ARREST,
        SEARCH,
        SEIZURE
    };
    enum class WarrantStatus
    {
        ISSUED,
        EXECUTED,
        CANCELLED,
        EXPIRED
    };
    enum class BailType
    {
        REGULAR,
        ANTICIPATORY,
        INTERIM,
        SURETY
    };
    enum class BailStatus
    {
        ACTIVE,
        REVOKED,
        EXPIRED,
        CANCELLED
    };
    enum class ForensicRequestStatus
    {
        REQUESTED,
        RECEIVED_BY_LAB,
        UNDER_EXAMINATION,
        REPORT_READY,
        REPORT_DELIVERED,
        CONTESTED
    };
    enum class ExaminationPurpose
    {
        DNA_ANALYSIS,
        FINGERPRINT_ANALYSIS,
        BALLISTICS_ANALYSIS,
        TOXICOLOGY_ANALYSIS,
        DIGITAL_FORENSICS,
        DOCUMENT_EXAMINATION,
        BLOOD_ANALYSIS,
        NARCOTICS_TESTING,
        TRACE_EVIDENCE_ANALYSIS,
        OTHER
    };
    enum class ExaminationResult
    {
        INCONCLUSIVE,
        MATCH_FOUND,
        NO_MATCH,
        PARTIAL_MATCH,
        EXCLUDED,
        PENDING
    };
    enum class ChargeSheetStatus
    {
        DRAFT,
        FILED,
        SUBMITTED_TO_COURT,
        ACCEPTED_BY_COURT,
        REJECTED_BY_COURT
    };
    enum class SheetType
    {
        ORIGINAL,
        SUPPLEMENTARY
    };
    enum class VehicleRole
    {
        STOLEN,
        USED_IN_CRIME,
        ABANDONED,
        EVIDENCE,
        SUSPECTS_VEHICLE,
        VICTIMS_VEHICLE
    };
    enum class SeizureStatus
    {
        SEIZED,
        NOT_SEIZED,
        RELEASED,
        AUCTIONED,
        DESTROYED,
        RETAINED_FOR_EVIDENCE
    };
    enum class VehicleType
    {
        CAR,
        MOTORCYCLE,
        TRUCK,
        BUS,
        VAN,
        SUV,
        RICKSHAW,
        OTHER
    };
    enum class ShiftType
    {
        MORNING,
        AFTERNOON,
        NIGHT,
        SPLIT,
        ON_CALL
    };
    enum class DutyStatus
    {
        SCHEDULED,
        ON_DUTY,
        COMPLETED,
        ABSENT,
        ON_LEAVE,
        SUSPENDED
    };
    enum class AuditAction
    {
        INSERT,
        UPDATE,
        DELETE
    };
    enum class AuditedTable
    {
        CASES,
        EVIDENCE,
        OFFICERS,
        ARRESTS,
        WARRANTS,
        CHARGE_SHEETS,
        BAIL_RECORDS,
        ACCUSED
    };
    enum class HotspotRiskLevel
    {
        LOW,
        MEDIUM,
        HIGH,
        CRITICAL
    };
    enum class PriorityLevel
    {
        LOW,
        MEDIUM,
        HIGH,
        CRITICAL
    };
    enum class AssignmentStatus
    {
        SUGGESTED,
        ACCEPTED,
        REJECTED,
        AUTO_EXPIRED
    };

}; // namespace JusticeFlow
