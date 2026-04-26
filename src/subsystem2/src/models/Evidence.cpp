#include "../../include/models/Evidence.h"
#include <algorithm> // for std::remove

namespace subsystem2
{

    Evidence::Evidence(const EvidenceDTO &db_data) : data(db_data) {}

    // --- Data Accessors ---

    int64_t Evidence::getEvidenceId() const
    {
        return data.evidence_id;
    }

    std::string Evidence::getEvidenceNumber() const
    {
        return data.evidence_number;
    }

    std::string Evidence::getFilePath() const
    {
        return data.file_path;
    }

    JusticeFlow::EvidenceStatus Evidence::getStatus() const
    {
        return data.evidence_status;
    }

    bool Evidence::isDeleted() const
    {
        return data.is_deleted;
    }

    EvidenceDTO Evidence::getDTO() const
    {
        return data;
    }

    // --- Domain Logic ---

    void Evidence::markDeleted(int64_t officer_id, const std::string &reason)
    {
        // Contract Rule: Never physically delete. Soft delete only.
        data.is_deleted = true;
        data.deleted_by = officer_id;
        data.deletion_reason = reason;
        data.deleted_at = std::time(nullptr); // Current timestamp
    }

    void Evidence::updateStatus(JusticeFlow::EvidenceStatus new_status)
    {
        data.evidence_status = new_status;
        data.updated_at = std::time(nullptr);
    }

    // --- Observer Pattern ---

    void Evidence::addObserver(IEvidenceObserver *observer)
    {
        if (observer)
        {
            observers.push_back(observer);
        }
    }

    void Evidence::removeObserver(IEvidenceObserver *observer)
    {
        // Removes the observer from the vector if it exists
        observers.erase(std::remove(observers.begin(), observers.end(), observer), observers.end());
    }

    void Evidence::notifyObservers()
    {
        // Loop through all listeners (e.g., OSFileLocker) and notify them
        for (IEvidenceObserver *obs : observers)
        {
            obs->onEvidenceSecured(this);
        }
    }

} // namespace subsystem2
