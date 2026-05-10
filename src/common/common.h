#pragma once

#include <string>
#include <ctime>
#include <vector>
#include "constants.h"

namespace JusticeFlow
{

    /**
     * ================================
     *    * SESSION CONTEXT STRUCT *
     * ================================
     */
    struct SessionContext
    {
        int officerId;
        std::string cnic;
        OfficerRank rank;
        int stationId;
        std::string belt_number;
        time_t createdAt;
        time_t expiresAt;
        bool isValid;
        std::string sessionToken; // UUID string
    };

    /**
     * ======================
     *    * CORE STRUCTS *
     * ======================
     */
    struct Station
    {
        int stationId;
        std::string stationCode;
        std::string stationName;
        StationType stationType;
        std::string phone;
        std::string email;
        std::string address;
        std::string city;
        std::string district;
        std::string zone;
        int parentStationId;
        bool isActive;
        time_t createdAt;
        time_t updatedAt;
    };

    struct Person
    {
        std::string cnic;
        std::string full_name;
        Gender gender;
        std::string dob;
        std::string mobile;
        std::string email;
        std::string permanentAddress;
        std::string currentAddress;
        time_t createdAt;
        time_t updatedAt;
    };

    struct Officer
    {
        int officerId;
        std::string beltNumber;
        std::string cnic;
        std::string qualification;
        std::string joiningDate;
        OfficerRank joiningRank;
        OfficerRank currentRank;
        std::string retirementDate;
        int bpsScale;
        int stationId;
        OfficerStatus status;
        std::string password_hash;
        time_t last_login;
        
    };

    struct Case
    {
        int case_id;
        std::string fir_number;
        CaseType case_type;
        CaseStatus case_status;
        time_t incident_date;
        std::string incident_address;
        std::string incident_description;
        double incident_lat;
        double incident_lon;
        int station_id;
        std::string primary_complainant_cnic;
        int filed_by;
        time_t filed_at;
        time_t updated_at;
        int lead_officer_id;
        int parent_case_id;
        time_t closed_at;
        std::string closure_reason;
        ApprovalStatus approval_status;
        int approved_by;
        time_t approved_at;
        int reopened_by;
        time_t reopened_at;
        std::string reopen_reason;
    };
    struct CaseOfficer
    {
        int case_id;
        int officer_id;
        CaseOfficerRole role;
        int assigned_by;
        time_t assigned_at;
        time_t relieved_at;
        time_t created_at;
    };
    struct OfficerRankHistory
    {
        int history_id;
        int officer_id;
        OfficerRank old_rank;
        OfficerRank new_rank;
        std::string old_belt_number;
        std::string new_belt_number;
        std::string promotion_type;
        std::string effective_date;
        std::string order_date;
        std::string promoted_by;
        time_t created_at;
    };
    struct OfficerDeployment
    {
        int deployment_id;
        int officer_id;
        int from_station_id;
        int to_station_id;
        std::string deployment_reason;
        std::string order_number;
        std::string deployed_from;
        std::string deployed_until;
        int deployed_by;
        bool is_active;
        time_t created_at;
        time_t updated_at;
    };

    /** =========================
     *   * CASE ROLE STRUCTS *
     *  =========================
     */

    struct Complainant
    {
        int complainant_id;
        int case_id;
        std::string person_cnic;
        RelationshipToVictim relation_to_victim;
        ComplainantStatus status;
        int added_by;
        bool notify_on_update;
        time_t withdrawn_at;
        std::string withdrawal_reason;
        time_t created_at;
        time_t updated_at;
    };
    struct Victim
    {
        int victim_id;
        int case_id;
        std::string person_cnic;
        std::string injury_type;
        InjurySeverity injury_severity;
        VulnerabilityCategory vulnerability_category;
        std::string medical_report_ref;
        int added_by;
        time_t created_at;
        time_t updated_at;
    };
    struct Witness
    {
        int witness_id;
        int case_id;
        std::string person_cnic;
        std::string statement_text;
        std::string statement_file_path;
        time_t statement_recorded_at;
        int recorded_by;
        WitnessProtection protection_status;
        bool is_identity_concealed;
        int added_by;
        time_t created_at;
        time_t updated_at;
    };
    struct Accused
    {
        int accused_id;
        std::string master_accused_cnic;
        int case_id;
        std::string person_cnic;
        InvolvementType involvement_type;
        int added_by;
        time_t created_at;
        time_t updated_at;
    };
    struct AccusedAssociation
    {
        int accused_id;
        int associated_accused_id;
        AssociationType association_type;
        time_t created_at;
    };

    /** ==========================
     *     * CASE LOG STRUCTS *
     *  ==========================
     */

    struct CaseStatusLog
    {
        int log_id;
        int case_id;
        CaseStatus old_status;
        CaseStatus new_status;
        int changed_by;
        std::string change_reason;
        time_t changed_at;
    };
    struct CaseJurisdictionHistory
    {
        int history_id;
        int case_id;
        int from_station_id;
        int to_station_id;
        std::string transfer_reason;
        int transferred_by;
        time_t transferred_at;
    };

    /** ====================================
     *     * EVIDENCE & CUSTODY STRUCTS *
     *  ====================================
     */

    struct Evidence
    {
        int evidence_id;
        std::string evidence_number;
        int case_id;
        EvidenceType evidence_type;
        EvidenceStatus evidence_status;
        std::string description;
        int quantity;
        std::string file_path;
        int collected_by;
        time_t collected_at;
        std::string collection_location;
        bool is_deleted;
        time_t deleted_at;
        int deleted_by;
        std::string deletion_reason;
        time_t created_at;
        time_t updated_at;
    };
    struct EvidenceCustodyLog
    {
        int custody_id;
        int evidence_id;
        int transferred_from;
        int transferred_to;
        std::string transfer_reason;
        time_t transferred_at;
        EvidenceStatus status_at_transfer;
        std::string notes;
    };

    /** =========================================
     *     * WARRANT, ARRESTS & BAIL STRUCTS *
     *  =========================================
     */
    struct Warrant
    {
        int warrant_id;
        std::string warrant_number;
        int case_id;
        std::string accused_cnic;
        WarrantType warrant_type;
        WarrantStatus warrant_status;
        std::string issuing_court;
        std::string magistrate_name;
        std::string issue_date;
        std::string valid_until;
        std::string target_address;
        int requested_by;
        int executed_by;
        time_t executed_at;
        int cancelled_by;
        time_t cancelled_at;
        std::string cancellation_reason;
        time_t created_at;
        time_t updated_at;
    };
    struct Arrest
    {
        int arrest_id;
        std::string arrest_number;
        std::string accused_cnic;
        int case_id;
        int warrant_id;
        int arresting_officer_id;
        time_t arrested_at;
        std::string arrest_location;
        CustodyStatus custody_status;
        time_t custody_released_at;
        std::string release_reason;
        bool is_disputed;
        std::string dispute_reason;
        time_t created_at;
        time_t updated_at;
    };
    struct BailRecord
    {
        int bail_id;
        std::string bail_number;
        int arrest_id;
        std::string court_name;
        std::string magistrate_name;
        std::string bail_date;
        BailType bail_type;
        BailStatus bail_status;
        double bail_amount;
        std::string surety_name;
        std::string surety_cnic;
        std::string surety_address;
        std::string valid_until;
        time_t revoked_at;
        std::string revocation_reason;
        int revoked_by;
        int recorded_by;
        time_t created_at;
        time_t updated_at;
    };

    /** =========================================
     *     * FORENSIC & CHARGE SHEET STRUCTS *
     *  =========================================
     */
    struct ForensicLabRequest
    {
        int request_id;
        std::string request_number;
        int case_id;
        std::string lab_name;
        std::string examiner_name;
        ExaminationPurpose examination_purpose;
        std::string purpose_description;
        std::string sent_date;
        std::string received_by_lab_date;
        std::string report_expected_date;
        ForensicRequestStatus request_status;
        int authorized_by;
        std::string findings;
        std::string report_file_path;
        std::string report_delivered_date;
        bool is_amended;
        std::string amendment_notes;
        time_t amended_at;
        int amended_by;
        bool is_contested;
        std::string contest_reason;
        int contested_by;
        time_t contested_at;
        time_t created_at;
        time_t updated_at;
    };
    struct ChargeSheet
    {
        int charge_sheet_id;
        std::string charge_sheet_number;
        int case_id;
        int parent_charge_sheet_id;
        SheetType sheet_type;
        ChargeSheetStatus charge_sheet_status;
        std::string court_name;
        std::string magistrate_name;
        std::vector<std::string> laws_invoked;
        int filed_by;
        std::string filing_date;
        time_t submitted_to_court_at;
        int submitted_by;
        std::string court_response_date;
        std::string court_remarks;
        bool is_locked;
        time_t locked_at;
        int locked_by;
        time_t created_at;
        time_t updated_at;
    };
    struct ForensicRequestEvidence
    {
        int request_id;
        int evidence_id;
        std::string notes;
        time_t added_at;
    };

    struct ChargeSheetAccused
    {
        int charge_sheet_id;
        std::string accused_cnic;
        std::vector<std::string> specific_charges;
        std::string remarks;
        int added_by;
        time_t added_at;
    };

    /** ========================================
     *     * VEHICLE, PATROL & DUTY STRUCTS *
     *  ========================================
     */
    struct Vehicle
    {
        int vehicle_id;
        std::string registration_number;
        std::string chassis_number;
        std::string engine_number;
        VehicleType vehicle_type;
        std::string make;
        std::string model;
        int model_year;
        std::string color;
        std::string registered_state;
        std::string registered_owner_cnic;
        std::string registered_owner_name;
        SeizureStatus seizure_status;
        time_t seized_at;
        int seized_by;
        std::string seizure_location;
        time_t released_at;
        int released_by;
        std::string release_reason;
        time_t created_at;
        time_t updated_at;
    };
    struct VehicleCase
    {
        int vehicle_case_id;
        int vehicle_id;
        int case_id;
        VehicleRole vehicle_role;
        std::string condition_notes;
        int added_by;
        time_t added_at;
    };
    struct PatrolRoute
    {
        int route_id;
        std::string beat_code;
        std::string route_name;
        std::string area_description;
        std::vector<std::string> landmarks;
        int station_id;
        bool is_active;
        time_t created_at;
        time_t updated_at;
    };
    struct DutyRoster
    {
        int duty_id;
        std::string duty_number;
        int officer_id;
        int station_id;
        int patrol_route_id;
        ShiftType shift_type;
        std::string duty_date;
        time_t scheduled_start;
        time_t scheduled_end;
        time_t actual_start;
        time_t actual_end;
        DutyStatus duty_status;
        std::string absence_reason;
        int assigned_by;
        time_t created_at;
        time_t updated_at;
    };

    /** ===========================================
     *     * AUDIT & ANALYTICS (AI/ML) STRUCTS *
     *  ===========================================
     */
    struct AuditLog
    {
        int audit_id;
        AuditedTable table_name;
        int record_id;
        AuditAction action;
        std::string old_value; // JSONB mapped to serialized string
        std::string new_value; // JSONB mapped to serialized string
        std::string changed_by_user;
        int changed_by_officer_id;
        std::string changed_by_belt;
        int client_process_id;
        std::string client_ip;
        time_t changed_at;
    };
    struct CrimeHotspot
    {
        int hotspot_id;
        std::string zone_label;
        double center_lat;
        double center_lon;
        int radius_meters;
        std::string area_description;
        int case_count;
        CaseType dominant_case_type;
        std::string case_type_breakdown; // JSONB string
        HotspotRiskLevel risk_level;
        double risk_score;
        int patrol_increase_pct;
        std::string recommendation_text;
        std::string analysis_from;
        std::string analysis_to;
        time_t analyzed_at;
        std::string model_version;
        std::string algorithm;
        double epsilon;
        int min_samples;
    };
    struct CasePriorityScore
    {
        int score_id;
        int case_id;
        PriorityLevel priority_level;
        double priority_score;
        std::string feature_contributions; // JSONB string
        std::string top_reason;
        std::string suggested_action;
        std::string input_features; // JSONB string
        time_t analyzed_at;
        std::string model_version;
        std::string algorithm;
        double model_accuracy;
    };
    struct OfficerWorkloadAssignment
    {
        int assignment_id;
        int case_id;
        int officer_id;
        AssignmentStatus assignment_status;
        double cost_score;
        std::string recommendation_reason;
        std::string cost_breakdown; // JSONB string
        int officer_active_cases;
        double officer_workload_score;
        int decided_by;
        time_t decided_at;
        std::string decision_notes;
        time_t expires_at;
        time_t analyzed_at;
        std::string model_version;
        std::string algorithm;
    };
    struct ModelPerformanceLog
    {
        int log_id;
        std::string model_name;
        std::string model_version;
        std::string algorithm;
        double accuracy;
        double precision_score;
        double recall_score;
        double f1_score;
        double hotspot_precision;
        int training_sample_size;
        std::string training_from;
        std::string training_to;
        bool meets_threshold;
        std::string threshold_notes;
        time_t evaluated_at;
        std::string evaluated_by;
    };
}