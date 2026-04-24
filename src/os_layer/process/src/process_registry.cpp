#include "../include/process_registry.h"

ProcessRegistry &ProcessRegistry::getInstance()
{
    static ProcessRegistry instance;
    return instance;
}

JusticeFlow::ResultCode ProcessRegistry::registerProcess(pid_t pid, const ProcessRecord &record)
{
    MutexGuard lock(mutex_); // RAII lock

    if (registry_.find(pid) != registry_.end())
    {
        return JusticeFlow::ResultCode::ALREADY_EXISTS;
    }

    registry_[pid] = record;
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode ProcessRegistry::updateState(pid_t pid, ProcessState new_state)
{
    MutexGuard lock(mutex_);

    auto it = registry_.find(pid);
    if (it == registry_.end())
    {
        return JusticeFlow::ResultCode::NOT_FOUND;
    }

    it->second.state = new_state;
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode ProcessRegistry::incrementRestartCount(pid_t pid, int &out_new_count)
{
    // CRITICAL FIX: Atomic operation under lock
    // This ensures restart_count is immediately persisted in the registry
    // and visible to all threads. No TOCTOU window.
    MutexGuard lock(mutex_);

    auto it = registry_.find(pid);
    if (it == registry_.end())
    {
        return JusticeFlow::ResultCode::NOT_FOUND;
    }

    // Increment under lock - immediately visible to all threads
    it->second.restart_count++;
    out_new_count = it->second.restart_count;

    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode ProcessRegistry::getRecord(pid_t pid, ProcessRecord &out_record)
{
    MutexGuard lock(mutex_);

    auto it = registry_.find(pid);
    if (it == registry_.end())
    {
        return JusticeFlow::ResultCode::NOT_FOUND;
    }

    out_record = it->second;
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode ProcessRegistry::removeRecord(pid_t pid)
{
    MutexGuard lock(mutex_);

    auto it = registry_.find(pid);
    if (it == registry_.end())
    {
        return JusticeFlow::ResultCode::NOT_FOUND;
    }

    registry_.erase(it);
    return JusticeFlow::ResultCode::OK;
}

JusticeFlow::ResultCode ProcessRegistry::getAllPids(std::vector<pid_t> &out_pids)
{
    MutexGuard lock(mutex_);

    out_pids.clear();
    out_pids.reserve(registry_.size());
    for (const auto &pair : registry_)
    {
        out_pids.push_back(pair.first);
    }

    return JusticeFlow::ResultCode::OK;
}
