#ifndef EVIDENCE_H
#define EVIDENCE_H

#include <string>
#include <vector>
#include "../s2_types.h"
#include "../patterns/IEvidenceObserver.h"

namespace subsystem2 {

class Evidence {
private:
    EvidenceDTO data; // The raw data mapping to the PostgreSQL table
    std::vector<IEvidenceObserver*> observers; // List of listeners

public:
    // Constructor initializes the entity with database data
    explicit Evidence(const EvidenceDTO& db_data);
    ~Evidence() = default;

    // --- Data Accessors (Getters) ---
    int64_t getEvidenceId() const;
    std::string getEvidenceNumber() const;
    std::string getFilePath() const;
    JusticeFlow::EvidenceStatus getStatus() const;
    bool isDeleted() const;

    // --- Domain Logic ---
    void markDeleted(int64_t officer_id, const std::string& reason);
    void updateStatus(JusticeFlow::EvidenceStatus new_status);

    // --- Observer Pattern Implementation ---
    void addObserver(IEvidenceObserver* observer);
    void removeObserver(IEvidenceObserver* observer);
    
    // Triggers all observers (called when the file is successfully saved)
    void notifyObservers();

    // Returns a copy of the DTO for database saving
    EvidenceDTO getDTO() const;
};

} // namespace subsystem2

#endif // EVIDENCE_H
