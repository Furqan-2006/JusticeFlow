#include "../../include/controllers/EvidenceManager.h"
#include "../../../common/logger.h"
#include "../../../os_layer/ipc/include/ipc_manager.h" 

#include <string>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace subsystem2 {

// Safe DB Helper (The workaround to pass the compiler resolution errors)
static ipc::IpcManager& getDB() {
    return ipc::IpcManager::getInstance();
}

JusticeFlow::ResultCode EvidenceManager::mapFileToMemory(const std::string& file_path) {
    // 1. Open the file strictly as Read-Only
    int fd = open(file_path.c_str(), O_RDONLY);
    if (fd == -1) {
        Logger::error("[S2][EvidenceManager] Failed to open evidence file for mapping.");
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    // 2. Get file size
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        close(fd);
        return JusticeFlow::ResultCode::FILE_SYSTEM_ERROR;
    }

    // 3. Map it into RAM using your OS MmapHandler (is_shared = false for MAP_PRIVATE)
    JusticeFlow::ResultCode res = mem_handler.map(fd, file_stat.st_size, false);
    
    // The mmap() call keeps the file mapped, so we can safely close the descriptor
    close(fd); 

    if (res == JusticeFlow::ResultCode::OK) {
        Logger::info("[S2][EvidenceManager] Evidence file successfully mapped to Virtual Memory.");
    }
    return res;
}

JusticeFlow::ResultCode EvidenceManager::logAndSecureEvidence(int64_t case_id, 
                                                              JusticeFlow::EvidenceType type, 
                                                              const std::string& description, 
                                                              const std::string& file_path, 
                                                              const JusticeFlow::SessionContext& session, 
                                                              Evidence*& out_evidence) {
    // 1. Authorization
    if (!session.isValid) return JusticeFlow::ResultCode::AUTH_FAILED;

    // 2. OS Integration: Secure the file in RAM
    if (!file_path.empty()) {
        JusticeFlow::ResultCode mem_res = mapFileToMemory(file_path);
        if (mem_res != JusticeFlow::ResultCode::OK) {
            Logger::error("[S2][EvidenceManager] Aborting evidence log due to OS memory mapping failure.");
            return mem_res;
        }
    }

    // 3. DB Integration: Prepare SQL
    // Temporary helper until toString() is built
    std::string type_str = (type == JusticeFlow::EvidenceType::DIGITAL) ? "DIGITAL" : "PHYSICAL";

    std::string sql = "INSERT INTO public.evidence (case_id, evidence_type, evidence_status, "
                      "description, file_path, collected_by) VALUES ("
                      + std::to_string(case_id) + ", "
                      "'" + type_str + "', "
                      "'RECEIVED', "
                      "'" + description + "', "
                      "'" + file_path + "', "
                      + std::to_string(session.officerId) + ") RETURNING evidence_id, evidence_number;";

    // 4. Execute DB Insert
    std::vector<std::vector<std::string>> results;
    JusticeFlow::ResultCode db_res = getDB().executeQuery(sql, results);

    if (db_res != JusticeFlow::ResultCode::OK || results.empty()) {
        Logger::error("[S2][EvidenceManager] Database insert failed for Evidence.");
        // RAII will auto-unmap the memory when mem_handler goes out of scope!
        return JusticeFlow::ResultCode::DB_ERROR;
    }

    // 5. Build the Object
    EvidenceDTO data;
    data.evidence_id = std::stoll(results[0][0]);
    data.evidence_number = results[0][1];
    data.case_id = case_id;
    data.evidence_type = type;
    data.description = description;
    data.file_path = file_path;
    data.collected_by = session.officerId;

    out_evidence = new Evidence(data); // Create the Entity

    // 6. Observer Pattern Trigger!
    out_evidence->notifyObservers();

    std::string msg = "[S2][EvidenceManager] Successfully logged Evidence: " + data.evidence_number;
    Logger::info(msg.c_str());
    return JusticeFlow::ResultCode::OK;
}

} // namespace subsystem2
