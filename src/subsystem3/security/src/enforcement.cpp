#include "security/include/enforcement.h"
#include "security/include/access_control.h"
#include "legal/include/case_validation.h"
#include "legal/include/compliance.h"
#include "integration/include/s1_bridge.h"
#include "integration/include/s2_bridge.h"
#include "integration/include/audit_bridge.h"
#include "utils/include/time_utils.h"
#include "os_layer/ipc/include/ipc_manager.h"
#include "common/logger.h"
#include <sstream>
#include <map>

using namespace JusticeFlow;

namespace security
{

    // State transition validation lookup tables
    static std::map<std::string, std::set<std::string>> warrant_transitions = {
        {"ISSUED", {"EXECUTED", "CANCELLED"}},
        {"EXECUTED", {"CANCELLED"}},
        {"CANCELLED", {}},
        {"EXPIRED", {}}};

    static std::map<std::string, std::set<std::string>> custody_transitions = {
        {"IN_CUSTODY", {"BAIL_GRANTED", "REMANDED", "RELEASED", "ESCAPED"}},
        {"BAIL_GRANTED", {"RELEASED", "REVOKED"}},
        {"REMANDED", {"RELEASED"}},
        {"RELEASED", {}},
        {"ESCAPED", {}}};

    static std::map<std::string, std::set<std::string>> bail_transitions = {
        {"ACTIVE", {"REVOKED", "EXPIRED"}},
        {"REVOKED", {}},
        {"EXPIRED", {}}};

    bool Enforcement::isValidTransition(const std::string &current_state,
                                        const std::string &new_state,
                                        const std::string &state_type)
    {
        std::map<std::string, std::set<std::string>> *transition_map = nullptr;

        if (state_type == "WarrantStatus")
        {
            transition_map = &warrant_transitions;
        }
        else if (state_type == "CustodyStatus")
        {
            transition_map = &custody_transitions;
        }
        else if (state_type == "BailStatus")
        {
            transition_map = &bail_transitions;
        }
        else
        {
            Logger::error("enforcement: Unknown state type");
            return false;
        }

        auto it = transition_map->find(current_state);
        if (it == transition_map->end())
        {
            std::string msg = "enforcement: Invalid current state: " + current_state;
            Logger::error(msg.c_str());
            return false;
        }

        if (it->second.find(new_state) == it->second.end())
        {
            std::string msg = "enforcement: Illegal transition from " + current_state +
                              " to " + new_state + " (state type: " + state_type + ")";
            Logger::debug(msg.c_str());
            return false;
        }

        return true;
    }

    // ===========================
    // WARRANT OPERATIONS (5)
    // ===========================

    bool Enforcement::requestWarrant(const SessionContext &session,
                                     int case_id,
                                     const std::string &accused_cnic,
                                     WarrantType warrant_type,
                                     const std::string &magistrate_name,
                                     const std::string &issuing_court,
                                     const std::string &valid_until,
                                     const std::string &target_address,
                                     int &out_warrant_id,
                                     ResultCode &out_code)
    {
        // Pre-flight authorization
        if (!AccessControl::checkWarrantPermission(session, case_id, warrant_type, out_code))
        {
            return false;
        }

        // State transition validation: (new) → ISSUED
        if (!isValidTransition("NEW", "ISSUED", "WarrantStatus"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Invalid state transition for new warrant");
            return false;
        }

        // Generate warrant number
        std::stringstream warrant_num_ss;
        warrant_num_ss << "WR-" << std::time(nullptr) << "-" << session.officerId;
        std::string warrant_number = warrant_num_ss.str();

        // Construct INSERT query
        std::stringstream insert_query;
        insert_query << "INSERT INTO subsystem3.warrants ("
                     << "warrant_number, case_id, accused_cnic, warrant_type, warrant_status, "
                     << "issuing_court, magistrate_name, issue_date, valid_until, target_address, "
                     << "requested_by, created_at) VALUES ("
                     << "'" << warrant_number << "', " << case_id << ", "
                     << "'" << accused_cnic << "', '" << (warrant_type == WarrantType::ARREST ? "ARREST" : warrant_type == WarrantType::SEARCH ? "SEARCH"
                                                                                                                                               : "SEIZURE")
                     << "', "
                     << "'ISSUED', '" << issuing_court << "', '" << magistrate_name << "', "
                     << "now(), '" << valid_until << "', '" << target_address << "', "
                     << session.officerId << ", now()) "
                     << "RETURNING warrant_id;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(insert_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to insert warrant");
            return false;
        }

        if (results.empty())
        {
            out_code = ResultCode::DB_ERROR;
            Logger::error("enforcement: Warrant insertion returned no ID");
            return false;
        }

        out_warrant_id = std::stoi(results[0][0]);

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            insert_query.str(),
            AuditedTable::WARRANTS,
            out_warrant_id,
            "Warrant requested for accused " + accused_cnic);

        // Notify S1 bridge of officer action
        integration::S1Bridge::notifyOfficerCaseAssignment(session.officerId, case_id);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Warrant request succeeded");
        return true;
    }

    bool Enforcement::approveWarrant(const SessionContext &session,
                                     int warrant_id,
                                     ResultCode &out_code)
    {
        // Query warrant to get current state
        std::stringstream query;
        query << "SELECT warrant_status FROM subsystem3.warrants WHERE warrant_id = " << warrant_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Warrant not found for approval");
            return false;
        }

        std::string current_status = results[0][0];

        // State transition validation: ISSUED → ISSUED (with approval flag)
        if (!isValidTransition(current_status, current_status, "WarrantStatus"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Cannot approve warrant in this state");
            return false;
        }

        // Update warrant with approval
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.warrants SET approved_flag = true, updated_at = now() "
                     << "WHERE warrant_id = " << warrant_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to approve warrant");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::WARRANTS,
            warrant_id,
            "Warrant approved");

        out_code = ResultCode::OK;
        Logger::info("enforcement: Warrant approved");
        return true;
    }

    bool Enforcement::executeWarrant(const SessionContext &session,
                                     int warrant_id,
                                     ResultCode &out_code)
    {
        // Query warrant to get current state
        std::stringstream query;
        query << "SELECT warrant_status, valid_until FROM subsystem3.warrants WHERE warrant_id = "
              << warrant_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Warrant not found for execution");
            return false;
        }

        std::string current_status = results[0][0];
        std::string valid_until = results[0][1];

        // Check if warrant is expired
        time_t expiry_time = std::stol(valid_until);
        if (time_utils::isExpired(expiry_time))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Cannot execute expired warrant");
            return false;
        }

        // State transition validation: ISSUED → EXECUTED
        if (!isValidTransition(current_status, "EXECUTED", "WarrantStatus"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Invalid state for warrant execution");
            return false;
        }

        // Update warrant status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.warrants SET warrant_status = 'EXECUTED', "
                     << "executed_by = " << session.officerId << ", "
                     << "executed_at = now(), updated_at = now() "
                     << "WHERE warrant_id = " << warrant_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to execute warrant");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::WARRANTS,
            warrant_id,
            "Warrant executed");

        out_code = ResultCode::OK;
        Logger::info("enforcement: Warrant executed");
        return true;
    }

    bool Enforcement::rejectWarrant(const SessionContext &session,
                                    int warrant_id,
                                    const std::string &rejection_reason,
                                    ResultCode &out_code)
    {
        // Query warrant to get current state
        std::stringstream query;
        query << "SELECT warrant_status FROM subsystem3.warrants WHERE warrant_id = " << warrant_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Warrant not found for rejection");
            return false;
        }

        std::string current_status = results[0][0];

        // State transition validation: ISSUED → CANCELLED
        if (!isValidTransition(current_status, "CANCELLED", "WarrantStatus"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Cannot reject warrant in this state");
            return false;
        }

        // Update warrant status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.warrants SET warrant_status = 'CANCELLED', "
                     << "cancelled_by = " << session.officerId << ", "
                     << "cancelled_at = now(), cancellation_reason = '" << rejection_reason << "', "
                     << "updated_at = now() "
                     << "WHERE warrant_id = " << warrant_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to reject warrant");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::WARRANTS,
            warrant_id,
            "Warrant rejected: " + rejection_reason);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Warrant rejected");
        return true;
    }

    bool Enforcement::cancelWarrant(const SessionContext &session,
                                    int warrant_id,
                                    const std::string &cancellation_reason,
                                    ResultCode &out_code)
    {
        // Query warrant to get current state
        std::stringstream query;
        query << "SELECT warrant_status FROM subsystem3.warrants WHERE warrant_id = " << warrant_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Warrant not found for cancellation");
            return false;
        }

        std::string current_status = results[0][0];

        // State transition validation: EXECUTED → CANCELLED
        if (!isValidTransition(current_status, "CANCELLED", "WarrantStatus"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Cannot cancel warrant in this state");
            return false;
        }

        // Update warrant status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.warrants SET warrant_status = 'CANCELLED', "
                     << "cancelled_by = " << session.officerId << ", "
                     << "cancelled_at = now(), cancellation_reason = '" << cancellation_reason << "', "
                     << "updated_at = now() "
                     << "WHERE warrant_id = " << warrant_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to cancel warrant");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::WARRANTS,
            warrant_id,
            "Warrant cancelled: " + cancellation_reason);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Warrant cancelled");
        return true;
    }

    // ===========================
    // ARREST OPERATIONS (4)
    // ===========================

    bool Enforcement::recordArrest(const SessionContext &session,
                                   int warrant_id,
                                   const std::string &arrest_location,
                                   int &out_arrest_id,
                                   ResultCode &out_code)
    {
        // Pre-flight authorization
        if (!AccessControl::checkArrestPermission(session, warrant_id, out_code))
        {
            return false;
        }

        // Get warrant details (case_id, accused_cnic)
        std::stringstream warrant_query;
        warrant_query << "SELECT case_id, accused_cnic FROM subsystem3.warrants WHERE warrant_id = "
                      << warrant_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(warrant_query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Warrant not found for arrest");
            return false;
        }

        int case_id = std::stoi(results[0][0]);
        std::string accused_cnic = results[0][1];

        // Generate arrest number
        std::stringstream arrest_num_ss;
        arrest_num_ss << "AR-" << std::time(nullptr) << "-" << session.officerId;
        std::string arrest_number = arrest_num_ss.str();

        // Insert arrest record
        std::stringstream insert_query;
        insert_query << "INSERT INTO subsystem3.arrests ("
                     << "arrest_number, accused_cnic, case_id, warrant_id, "
                     << "arresting_officer_id, arrested_at, arrest_location, custody_status, "
                     << "created_at) VALUES ("
                     << "'" << arrest_number << "', '" << accused_cnic << "', " << case_id << ", "
                     << warrant_id << ", " << session.officerId << ", now(), "
                     << "'" << arrest_location << "', 'IN_CUSTODY', now()) "
                     << "RETURNING arrest_id;";

        db_result = ipc::IpcManager::getInstance().executeQuery(insert_query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to record arrest");
            return false;
        }

        out_arrest_id = std::stoi(results[0][0]);

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            insert_query.str(),
            AuditedTable::ARRESTS,
            out_arrest_id,
            "Arrest recorded for accused " + accused_cnic);

        // Notify S1 and S2 bridges
        integration::S1Bridge::notifyOfficerCaseAssignment(session.officerId, case_id);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Arrest recorded");
        return true;
    }

    bool Enforcement::updateCustodyStatus(const SessionContext &session,
                                          int arrest_id,
                                          CustodyStatus new_status,
                                          const std::string &reason,
                                          ResultCode &out_code)
    {
        // Query arrest to get current custody status
        std::stringstream query;
        query << "SELECT custody_status FROM subsystem3.arrests WHERE arrest_id = " << arrest_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Arrest not found");
            return false;
        }

        std::string current_status_str = results[0][0];
        std::string new_status_str;

        if (new_status == CustodyStatus::IN_CUSTODY)
            new_status_str = "IN_CUSTODY";
        else if (new_status == CustodyStatus::BAIL_GRANTED)
            new_status_str = "BAIL_GRANTED";
        else if (new_status == CustodyStatus::REMANDED)
            new_status_str = "REMANDED";
        else if (new_status == CustodyStatus::RELEASED)
            new_status_str = "RELEASED";
        else if (new_status == CustodyStatus::ESCAPED)
            new_status_str = "ESCAPED";

        // State transition validation
        if (!isValidTransition(current_status_str, new_status_str, "CustodyStatus"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Invalid custody status transition");
            return false;
        }

        // Update custody status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.arrests SET custody_status = '" << new_status_str << "', "
                     << "updated_at = now() WHERE arrest_id = " << arrest_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to update custody status");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::ARRESTS,
            arrest_id,
            "Custody status updated to " + new_status_str + ": " + reason);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Custody status updated");
        return true;
    }

    bool Enforcement::disputeArrest(const SessionContext &session,
                                    int arrest_id,
                                    const std::string &dispute_reason,
                                    ResultCode &out_code)
    {
        // Query arrest to verify it exists
        std::stringstream query;
        query << "SELECT arrest_id FROM subsystem3.arrests WHERE arrest_id = " << arrest_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Arrest not found for dispute");
            return false;
        }

        // Update dispute flag
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.arrests SET is_disputed = true, "
                     << "dispute_reason = '" << dispute_reason << "', updated_at = now() "
                     << "WHERE arrest_id = " << arrest_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to dispute arrest");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::ARRESTS,
            arrest_id,
            "Arrest disputed: " + dispute_reason);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Arrest disputed");
        return true;
    }

    bool Enforcement::releaseFromCustody(const SessionContext &session,
                                         int arrest_id,
                                         const std::string &release_reason,
                                         ResultCode &out_code)
    {
        // Query arrest to get current custody status
        std::stringstream query;
        query << "SELECT custody_status FROM subsystem3.arrests WHERE arrest_id = " << arrest_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Arrest not found for release");
            return false;
        }

        std::string current_status = results[0][0];

        // State transition validation: (IN_CUSTODY/BAIL_GRANTED) → RELEASED
        if (!isValidTransition(current_status, "RELEASED", "CustodyStatus"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Invalid state for release");
            return false;
        }

        // Update custody status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.arrests SET custody_status = 'RELEASED', "
                     << "custody_released_at = now(), release_reason = '" << release_reason << "', "
                     << "updated_at = now() WHERE arrest_id = " << arrest_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to release from custody");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::ARRESTS,
            arrest_id,
            "Released from custody: " + release_reason);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Released from custody");
        return true;
    }

    // ===========================
    // BAIL OPERATIONS (3)
    // ===========================

    bool Enforcement::setBail(const SessionContext &session,
                              int arrest_id,
                              BailType bail_type,
                              uint64_t bail_amount,
                              const std::string &magistrate_name,
                              const std::string &court_name,
                              const std::string &valid_until,
                              const std::string &surety_cnic,
                              int &out_bail_id,
                              ResultCode &out_code)
    {
        // Pre-flight authorization
        if (!AccessControl::checkBailPermission(session, arrest_id, out_code))
        {
            return false;
        }

        // Validate bail amount per type
        legal::ComplianceResult compliance = legal::Compliance::validateBailAmount(bail_type, bail_amount);
        if (compliance.code != ResultCode::OK)
        {
            out_code = compliance.code;
            Logger::error("enforcement: Bail amount validation failed");
            return false;
        }

        // Generate bail number
        std::stringstream bail_num_ss;
        bail_num_ss << "BA-" << std::time(nullptr) << "-" << session.officerId;
        std::string bail_number = bail_num_ss.str();

        // Convert BailType to string
        std::string bail_type_str;
        if (bail_type == BailType::REGULAR)
            bail_type_str = "REGULAR";
        else if (bail_type == BailType::ANTICIPATORY)
            bail_type_str = "ANTICIPATORY";
        else if (bail_type == BailType::INTERIM)
            bail_type_str = "INTERIM";
        else if (bail_type == BailType::SURETY)
            bail_type_str = "SURETY";

        // Insert bail record
        std::stringstream insert_query;
        insert_query << "INSERT INTO subsystem3.bail_records ("
                     << "bail_number, arrest_id, court_name, magistrate_name, bail_date, "
                     << "bail_type, bail_status, bail_amount, surety_name, surety_cnic, "
                     << "valid_until, recorded_by, created_at) VALUES ("
                     << "'" << bail_number << "', " << arrest_id << ", "
                     << "'" << court_name << "', '" << magistrate_name << "', now(), "
                     << "'" << bail_type_str << "', 'ACTIVE', " << bail_amount << ", "
                     << "'', '" << surety_cnic << "', '" << valid_until << "', "
                     << session.officerId << ", now()) "
                     << "RETURNING bail_id;";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(insert_query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to set bail");
            return false;
        }

        out_bail_id = std::stoi(results[0][0]);

        // Update arrest custody status to BAIL_GRANTED
        std::stringstream update_arrest;
        update_arrest << "UPDATE subsystem3.arrests SET custody_status = 'BAIL_GRANTED', "
                      << "updated_at = now() WHERE arrest_id = " << arrest_id << ";";
        ipc::IpcManager::getInstance().executeQuery(update_arrest.str(), results);

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            insert_query.str(),
            AuditedTable::BAIL_RECORDS,
            out_bail_id,
            "Bail set: " + bail_type_str + " amount=" + std::to_string(bail_amount));

        out_code = ResultCode::OK;
        Logger::info("enforcement: Bail set");
        return true;
    }

    bool Enforcement::modifyBail(const SessionContext &session,
                                 int bail_id,
                                 uint64_t new_amount,
                                 const std::string &modification_reason,
                                 ResultCode &out_code)
    {
        // Query bail to get current status
        std::stringstream query;
        query << "SELECT bail_status, bail_type FROM subsystem3.bail_records WHERE bail_id = "
              << bail_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Bail not found");
            return false;
        }

        std::string bail_status = results[0][0];
        std::string bail_type_str = results[0][1];

        // Can only modify ACTIVE bail
        if (bail_status != "ACTIVE")
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Can only modify active bail");
            return false;
        }

        // Validate new amount
        BailType bail_type = BailType::REGULAR;
        if (bail_type_str == "ANTICIPATORY")
            bail_type = BailType::ANTICIPATORY;
        else if (bail_type_str == "INTERIM")
            bail_type = BailType::INTERIM;
        else if (bail_type_str == "SURETY")
            bail_type = BailType::SURETY;

        legal::ComplianceResult compliance = legal::Compliance::validateBailAmount(bail_type, new_amount);
        if (compliance.code != ResultCode::OK)
        {
            out_code = compliance.code;
            Logger::error("enforcement: New bail amount validation failed");
            return false;
        }

        // Update bail amount
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.bail_records SET bail_amount = " << new_amount << ", "
                     << "updated_at = now() WHERE bail_id = " << bail_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to modify bail");
            return false;
        }

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::BAIL_RECORDS,
            bail_id,
            "Bail modified: new amount=" + std::to_string(new_amount) + ", reason=" + modification_reason);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Bail modified");
        return true;
    }

    bool Enforcement::revokeBail(const SessionContext &session,
                                 int bail_id,
                                 const std::string &revocation_reason,
                                 ResultCode &out_code)
    {
        // Query bail to get current status
        std::stringstream query;
        query << "SELECT bail_status, arrest_id FROM subsystem3.bail_records WHERE bail_id = "
              << bail_id << ";";

        std::vector<std::vector<std::string>> results;
        ResultCode db_result = ipc::IpcManager::getInstance().executeQuery(query.str(), results);

        if (db_result != ResultCode::OK || results.empty())
        {
            out_code = ResultCode::NOT_FOUND;
            Logger::error("enforcement: Bail not found for revocation");
            return false;
        }

        std::string bail_status = results[0][0];
        int arrest_id = std::stoi(results[0][1]);

        // State transition validation: ACTIVE → REVOKED
        if (!isValidTransition(bail_status, "REVOKED", "BailStatus"))
        {
            out_code = ResultCode::INVALID_STATE;
            Logger::error("enforcement: Cannot revoke bail in this state");
            return false;
        }

        // Update bail status
        std::stringstream update_query;
        update_query << "UPDATE subsystem3.bail_records SET bail_status = 'REVOKED', "
                     << "revoked_at = now(), revocation_reason = '" << revocation_reason << "', "
                     << "revoked_by = " << session.officerId << ", updated_at = now() "
                     << "WHERE bail_id = " << bail_id << ";";

        db_result = ipc::IpcManager::getInstance().executeQuery(update_query.str(), results);

        if (db_result != ResultCode::OK)
        {
            out_code = db_result;
            Logger::error("enforcement: Failed to revoke bail");
            return false;
        }

        // Update arrest custody status back to IN_CUSTODY
        std::stringstream update_arrest;
        update_arrest << "UPDATE subsystem3.arrests SET custody_status = 'IN_CUSTODY', "
                      << "updated_at = now() WHERE arrest_id = " << arrest_id << ";";
        ipc::IpcManager::getInstance().executeQuery(update_arrest.str(), results);

        // Notify audit bridge
        integration::AuditBridge::getInstance().log(
            update_query.str(),
            AuditedTable::BAIL_RECORDS,
            bail_id,
            "Bail revoked: " + revocation_reason);

        out_code = ResultCode::OK;
        Logger::info("enforcement: Bail revoked");
        return true;
    }

} // namespace security