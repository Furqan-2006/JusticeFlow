#pragma once

#include <string>
#include <vector>
#include <ctime>
#include "common/constants.h"
#include "common/common.h"

namespace forensic
{

    /**
     * @file forensic_repository.h
     * @brief Repository pattern for forensic database access
     *
     * Pure data access layer — no business logic.
     * Owns all DB read/write for:
     *   - subsystem3.Forensic_Lab_Requests
     *   - subsystem3.Forensic_Request_Evidence
     *
     * Trigger-driven evidence status sync (SENT_TO_LAB, RETURNED_FROM_LAB) fires
     * automatically on INSERT/UPDATE — repository doesn't manage this.
     *
     * Thread Safety: All functions are stateless. Thread-safe via OS layer IPC.
     *
     * Dependencies: common/constants.h, common/common.h only
     */

    class ForensicRepository
    {
    public:
        /**
         * Inserts a new forensic lab request.
         *
         * Creates a record in subsystem3.Forensic_Lab_Requests with status REQUESTED.
         * Trigger automatically fires to update subsystem2.evidence status.
         *
         * @param case_id Case ID for which request is made
         * @param lab_name Name of forensic lab
         * @param examiner_name Name of assigned examiner
         * @param examination_purpose Purpose of examination (DNA_ANALYSIS, FINGERPRINT_ANALYSIS, etc.)
         * @param purpose_description Free-form description of examination goals
         * @param report_expected_date Expected report delivery date
         * @param authorized_by Officer ID authorizing the request
         * @param out_request_id Output parameter set to created request ID
         * @return ResultCode::OK on success
         *         ResultCode::DB_ERROR on database failure
         *
         * @note Database operation: INSERT INTO subsystem3.Forensic_Lab_Requests (...)
         *       Trigger: Updates subsystem2.evidence.evidence_status → SENT_TO_LAB
         */
        static JusticeFlow::ResultCode insertRequest(int case_id,
                                                     const std::string &lab_name,
                                                     const std::string &examiner_name,
                                                     JusticeFlow::ExaminationPurpose examination_purpose,
                                                     const std::string &purpose_description,
                                                     const std::string &report_expected_date,
                                                     int authorized_by,
                                                     int &out_request_id);

        /**
         * Updates the status of a forensic lab request.
         *
         * Transitions request through states: REQUESTED → RECEIVED_BY_LAB → UNDER_EXAMINATION →
         * REPORT_READY → REPORT_DELIVERED (or CONTESTED).
         *
         * @param request_id Request ID to update
         * @param new_status New status value
         * @param findings Lab findings/report content (optional, for REPORT_READY/REPORT_DELIVERED)
         * @param report_file_path Path to report file in storage (optional)
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if request doesn't exist
         *         ResultCode::DB_ERROR on database failure
         *
         * @note Database operation: UPDATE subsystem3.Forensic_Lab_Requests SET status = ...
         *       No automatic trigger on status update — evidence status handled separately.
         */
        static JusticeFlow::ResultCode updateRequestStatus(int request_id,
                                                           JusticeFlow::ForensicRequestStatus new_status,
                                                           const std::string &findings = "",
                                                           const std::string &report_file_path = "");

        /**
         * Links an evidence item to a forensic request.
         *
         * Inserts record into subsystem3.Forensic_Request_Evidence.
         * Trigger automatically updates subsystem2.evidence.evidence_status to SENT_TO_LAB.
         *
         * @param request_id Forensic request ID
         * @param evidence_id Evidence item to link
         * @param notes Optional notes about why this evidence is submitted
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if request or evidence doesn't exist
         *         ResultCode::DB_ERROR on database failure
         *
         * @note Database operation: INSERT INTO subsystem3.Forensic_Request_Evidence (...)
         *       Trigger: Updates subsystem2.evidence.evidence_status → SENT_TO_LAB
         */
        static JusticeFlow::ResultCode insertEvidenceLink(int request_id,
                                                          int evidence_id,
                                                          const std::string &notes = "");

        /**
         * Retrieves all forensic requests related to a case.
         *
         * Queries subsystem3.Forensic_Lab_Requests filtered by case_id.
         *
         * @param case_id Case ID to query
         * @param out_requests Output vector populated with ForensicLabRequest structs
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if no requests found
         *         ResultCode::DB_ERROR on database failure
         *
         * @note Database query: SELECT * FROM subsystem3.Forensic_Lab_Requests WHERE case_id = ?
         */
        static JusticeFlow::ResultCode getRequestsByCase(int case_id,
                                                         std::vector<JusticeFlow::ForensicLabRequest> &out_requests);

        /**
         * Retrieves all pending forensic requests (not yet delivered or contested).
         *
         * Queries subsystem3.Forensic_Lab_Requests filtered by status.
         * Pending states: REQUESTED, RECEIVED_BY_LAB, UNDER_EXAMINATION, REPORT_READY.
         *
         * @param out_requests Output vector populated with ForensicLabRequest structs
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if no pending requests
         *         ResultCode::DB_ERROR on database failure
         *
         * @note Database query: SELECT * FROM subsystem3.Forensic_Lab_Requests
         *       WHERE request_status IN ('REQUESTED', 'RECEIVED_BY_LAB', 'UNDER_EXAMINATION', 'REPORT_READY')
         */
        static JusticeFlow::ResultCode getPendingRequests(std::vector<JusticeFlow::ForensicLabRequest> &out_requests);

        /**
         * Retrieves all evidence items linked to a forensic request.
         *
         * Queries subsystem3.Forensic_Request_Evidence to get evidence IDs, then joins
         * subsystem2.evidence to get full evidence records.
         *
         * @param request_id Forensic request ID
         * @param out_evidence Output vector populated with Evidence structs
         * @return ResultCode::OK on success
         *         ResultCode::NOT_FOUND if request not found or no evidence linked
         *         ResultCode::DB_ERROR on database failure
         *
         * @note Database query: SELECT e.* FROM subsystem2.evidence e
         *       INNER JOIN subsystem3.Forensic_Request_Evidence fre ON e.evidence_id = fre.evidence_id
         *       WHERE fre.request_id = ?
         */
        static JusticeFlow::ResultCode getEvidenceByRequest(int request_id,
                                                            std::vector<JusticeFlow::Evidence> &out_evidence);
    };

} // namespace forensic