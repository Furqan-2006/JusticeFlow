#include "../include/sync.h"
#include "common/logger.h"

// --- Mutex ---

Mutex::Mutex()
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    // CRITICAL FIX #1.1: Changed from PTHREAD_PROCESS_SHARED to PTHREAD_PROCESS_PRIVATE
    // These mutexes live on heap/stack of supervisor process, not in shared memory
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE);

    // ROBUST mutex for crash recovery in shared memory contexts
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

    pthread_mutex_init(&lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

Mutex::~Mutex()
{
    pthread_mutex_destroy(&lock);
}

// --- MutexGuard ---

MutexGuard::MutexGuard(Mutex &mutex) : m(mutex), lock_acquired(false)
{
    int ret = pthread_mutex_lock(&m.lock);

    // CRITICAL FIX #1.3: Check ALL error codes, not just EOWNERDEAD
    if (ret == EOWNERDEAD)
    {
        // Recover the robust mutex if the previous owner died
        int consistent_ret = pthread_mutex_consistent(&m.lock);
        if (consistent_ret == 0)
        {
            lock_acquired = true;
        }
        else
        {
            Logger::error("[Sync] pthread_mutex_consistent failed - data may be corrupted");
            lock_acquired = false;
        }
    }
    else if (ret == 0)
    {
        // Lock acquired successfully
        lock_acquired = true;
    }
    else if (ret == EDEADLK)
    {
        Logger::error("[Sync] EDEADLK: Mutex is not recursive and caller already owns it");
        lock_acquired = false;
    }
    else if (ret == EINVAL)
    {
        Logger::error("[Sync] EINVAL: Mutex is invalid");
        lock_acquired = false;
    }
    else if (ret == EAGAIN)
    {
        Logger::error("[Sync] EAGAIN: Mutex lock limit exceeded");
        lock_acquired = false;
    }
    else
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[Sync] pthread_mutex_lock failed with error code %d", ret);
        Logger::error(buf);
        lock_acquired = false;
    }
}

MutexGuard::~MutexGuard()
{
    // CRITICAL FIX #1.3: Only unlock if we actually acquired the lock
    if (lock_acquired)
    {
        int ret = pthread_mutex_unlock(&m.lock);
        if (ret != 0)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[Sync] pthread_mutex_unlock failed with error code %d", ret);
            Logger::error(buf);
        }
    }
}

// --- Semaphore ---

Semaphore::Semaphore(int initial_value)
{
    // CRITICAL FIX #1.2: Changed from pshared=1 (PROCESS_SHARED) to pshared=0 (PROCESS_PRIVATE)
    // Semaphores on heap/stack must use pshared=0. pshared=1 requires shared memory.
    // On macOS, pshared=1 silently fails with ENOSYS, eliminating throttling.
    int ret = sem_init(&sem, 0, initial_value);

    if (ret != 0)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[Sync] sem_init failed: %s", strerror(errno));
        Logger::error(buf);
    }
}

Semaphore::~Semaphore()
{
    sem_destroy(&sem);
}

int Semaphore::get_value()
{
    int val = 0;
    sem_getvalue(&sem, &val);
    return val;
}

// --- SemGuard ---

SemGuard::SemGuard(Semaphore &sem) : s(sem), sem_acquired(false)
{
    // CRITICAL FIX #1.4: Check for errors, don't silently ignore failures
    int ret = sem_wait(&s.sem);

    if (ret == 0)
    {
        sem_acquired = true;
    }
    else if (ret == -1)
    {
        int err = errno;
        if (err == EINTR)
        {
            Logger::error("[Sync] sem_wait interrupted by signal");
        }
        else if (err == EINVAL)
        {
            Logger::error("[Sync] sem_wait: Semaphore is invalid");
        }
        else
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[Sync] sem_wait failed with errno %d", err);
            Logger::error(buf);
        }
        sem_acquired = false;
    }
}

SemGuard::~SemGuard()
{
    // CRITICAL FIX #1.4: Only post if we actually acquired the semaphore
    if (sem_acquired)
    {
        int ret = sem_post(&s.sem);
        if (ret != 0)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[Sync] sem_post failed: %s", strerror(errno));
            Logger::error(buf);
        }
    }
}

// --- CondVar ---

CondVar::CondVar()
{
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);

    // CRITICAL FIX #1.1: Changed from PTHREAD_PROCESS_SHARED to PTHREAD_PROCESS_PRIVATE
    pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE);

    pthread_cond_init(&cond, &attr);
    pthread_condattr_destroy(&attr);
}

CondVar::~CondVar()
{
    pthread_cond_destroy(&cond);
}

void CondVar::wait(Mutex &m)
{
    // CRITICAL FIX #1.4: Check return value
    int ret = pthread_cond_wait(&cond, &m.lock);

    if (ret != 0)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[Sync] pthread_cond_wait failed with error code %d", ret);
        Logger::error(buf);
    }
}

void CondVar::signal()
{
    // CRITICAL FIX #1.4: Check return value
    int ret = pthread_cond_signal(&cond);

    if (ret != 0)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[Sync] pthread_cond_signal failed with error code %d", ret);
        Logger::error(buf);
    }
}

void CondVar::broadcast()
{
    // CRITICAL FIX #1.4: Check return value
    int ret = pthread_cond_broadcast(&cond);

    if (ret != 0)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[Sync] pthread_cond_broadcast failed with error code %d", ret);
        Logger::error(buf);
    }
}

// --- RWLock ---

RWLock::RWLock()
{
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);

    // CRITICAL FIX #1.1: Changed from PTHREAD_PROCESS_SHARED to PTHREAD_PROCESS_PRIVATE
    pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE);

    pthread_rwlock_init(&rwlock, &attr);
    pthread_rwlockattr_destroy(&attr);
}

RWLock::~RWLock()
{
    pthread_rwlock_destroy(&rwlock);
}

// --- RWLockReadGuard ---

RWLockReadGuard::RWLockReadGuard(RWLock &l) : lock(l), lock_acquired(false)
{
    // CRITICAL FIX #1.4: Check return value and error codes
    int ret = pthread_rwlock_rdlock(&lock.rwlock);

    if (ret == 0)
    {
        lock_acquired = true;
    }
    else if (ret == EDEADLK)
    {
        Logger::error("[Sync] EDEADLK: Read lock would cause deadlock");
        lock_acquired = false;
    }
    else if (ret == EINVAL)
    {
        Logger::error("[Sync] EINVAL: RWLock is invalid");
        lock_acquired = false;
    }
    else
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[Sync] pthread_rwlock_rdlock failed with error code %d", ret);
        Logger::error(buf);
        lock_acquired = false;
    }
}

RWLockReadGuard::~RWLockReadGuard()
{
    // CRITICAL FIX #1.4: Only unlock if we actually acquired the lock
    if (lock_acquired)
    {
        int ret = pthread_rwlock_unlock(&lock.rwlock);
        if (ret != 0)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[Sync] pthread_rwlock_unlock (read) failed with error code %d", ret);
            Logger::error(buf);
        }
    }
}

// --- RWLockWriteGuard ---

RWLockWriteGuard::RWLockWriteGuard(RWLock &l) : lock(l), lock_acquired(false)
{
    // CRITICAL FIX #1.4: Check return value and error codes
    int ret = pthread_rwlock_wrlock(&lock.rwlock);

    if (ret == 0)
    {
        lock_acquired = true;
    }
    else if (ret == EDEADLK)
    {
        Logger::error("[Sync] EDEADLK: Write lock would cause deadlock");
        lock_acquired = false;
    }
    else if (ret == EINVAL)
    {
        Logger::error("[Sync] EINVAL: RWLock is invalid");
        lock_acquired = false;
    }
    else
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[Sync] pthread_rwlock_wrlock failed with error code %d", ret);
        Logger::error(buf);
        lock_acquired = false;
    }
}

RWLockWriteGuard::~RWLockWriteGuard()
{
    // CRITICAL FIX #1.4: Only unlock if we actually acquired the lock
    if (lock_acquired)
    {
        int ret = pthread_rwlock_unlock(&lock.rwlock);
        if (ret != 0)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[Sync] pthread_rwlock_unlock (write) failed with error code %d", ret);
            Logger::error(buf);
        }
    }
}