#include "../include/process_registery.h"

ProcessRegistry::ProcessRegistry()
{
    pthread_mutex_init(&mutex_, nullptr);
}

ProcessRegistry::~ProcessRegistry()
{
    pthread_mutex_lock(&mutex_);
    registry_.clear();
    pthread_mutex_unlock(&mutex_);
    pthread_mutex_destroy(&mutex_);
}

ProcessRegistry &ProcessRegistry::getProcessRegistry()
{
    static ProcessRegistry instance;
    return instance;
}

ResultCode ProcessRegistry::registerProcess(pid_t pid, const ProcessRecord &record)
{
    pthread_mutex_lock(&mutex_);

    if (registry_.find(pid) != registry_.end())
    {
        pthread_mutex_unlock(&mutex_);
        return ResultCode::ALREADY_EXISTS;
    }

    registry_[pid] = record;

    pthread_mutex_unlock(&mutex_);
    return ResultCode::OK;
}

ResultCode ProcessRegistry::updateState(pid_t pid, ProcessState new_state)
{
    pthread_mutex_lock(&mutex_);

    auto it = registry_.find(pid);
    if (it == registry_.end())
    {
        pthread_mutex_unlock(&mutex_);
        return ResultCode::NOT_FOUND;
    }

    it->second.state = new_state;

    pthread_mutex_unlock(&mutex_);
    return Result::Code;
}

ResultCode ProcessRegistry::getRecord(pid_t pid, ProcessRecord &out_record)
{
    pthread_mutex_lock(&mutex_);

    auto it = registry_.find(pid);
    if (it == registry_.end())
    {
        pthread_mutex_unlock(&mutex_);
        return ResultCode::NOT_FOUND;
    }
    out_record = it->second;

    pthread_mutex_unlock(&mutex_);
    return ResultCode::OK;
}

ResultCode ProcessRegistry::removeRecord(pid_t pid)
{
    pthread_mutex_lock(&mutex_);

    auto it = registry_.find(pid);
    if (it == registry_.end())
    {
        pthread_mutex_unlock(&mutex);
        return ResultCode::NOT_FOUND;
    }

    registry_.erase(it);

    pthread_mutex_unlock(&mutex_);
    return ResultCode::OK;
}

ResultCode ProcessRegistry::getAllPids(std::vector<pid_t> &out_pids)
{
    pthread_mutex_lock(&mutex_);

    out_pids.clear();
    out_pids.reserve(registry_.size());
    for (const auto &pair : registry_)
    {
        out_pids.push_back(pair.first);
    }

    pthread_mutex_unlock(&mutex_);
    return ResultCode::OK;
}