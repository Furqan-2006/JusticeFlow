#include "../include/sync.h"

// --- Mutex ---

Mutex::Mutex()
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);

    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&lock, &attr);
    pthread_mutexattr_destroy(&attr);
}

Mutex::~Mutex()
{
    pthread_mutex_destroy(&lock);
}

// --- MutexGuard ---

MutexGuard::MutexGuard(Mutex &mutex) : m(mutex)
{
    int ret = pthread_mutex_lock(&m.lock);
    if (ret == EOWNERDEAD)
    {
        // Recover the robust mutex if the previous owner died
        pthread_mutex_consistent(&m.lock);
    }
}

MutexGuard::~MutexGuard()
{
    pthread_mutex_unlock(&m.lock);
}

// --- Semaphore ---

Semaphore::Semaphore(int initial_value)
{
    // 1 specifies it is process-shared
    sem_init(&sem, 1, initial_value);
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

SemGuard::SemGuard(Semaphore &sem) : s(sem)
{
    sem_wait(&s.sem);
}

SemGuard::~SemGuard()
{
    sem_post(&s.sem);
}

// --- CondVar ---

CondVar::CondVar()
{
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_cond_init(&cond, &attr);
    pthread_condattr_destroy(&attr);
}

CondVar::~CondVar()
{
    pthread_cond_destroy(&cond);
}

void CondVar::wait(Mutex &m)
{
    pthread_cond_wait(&cond, &m.lock);
}

void CondVar::signal()
{
    pthread_cond_signal(&cond);
}

void CondVar::broadcast()
{
    pthread_cond_broadcast(&cond);
}

// --- RWLock ---

RWLock::RWLock()
{
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_rwlock_init(&rwlock, &attr);
    pthread_rwlockattr_destroy(&attr);
}

RWLock::~RWLock()
{
    pthread_rwlock_destroy(&rwlock);
}

// --- RWLockReadGuard ---

RWLockReadGuard::RWLockReadGuard(RWLock &l) : lock(l)
{
    pthread_rwlock_rdlock(&lock.rwlock);
}

RWLockReadGuard::~RWLockReadGuard()
{
    pthread_rwlock_unlock(&lock.rwlock);
}

// --- RWLockWriteGuard ---

RWLockWriteGuard::RWLockWriteGuard(RWLock &l) : lock(l)
{
    pthread_rwlock_wrlock(&lock.rwlock);
}

RWLockWriteGuard::~RWLockWriteGuard()
{
    pthread_rwlock_unlock(&lock.rwlock);
}