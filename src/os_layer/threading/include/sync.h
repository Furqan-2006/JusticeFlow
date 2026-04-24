#pragma once

#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

// ============================================================================
// CRITICAL FIX #1.1: Mutex, CondVar, RWLock initialization
// ============================================================================
// ISSUE: These were ALL initialized with PTHREAD_PROCESS_SHARED, but they
// live on heap/stack of supervisor (not shared memory). This is undefined
// behavior per POSIX. Only objects in actual shared memory should use this flag.
//
// FIX: Changed to PTHREAD_PROCESS_PRIVATE (default) since they're single-process.
// Only the raw pthread_mutex_t inside SharedStatusTable uses PROCESS_SHARED.
// ============================================================================

// 1. Mutex & MutexGuard

class Mutex
{
private:
    pthread_mutex_t lock;
    friend class MutexGuard;
    friend class CondVar;

public:
    Mutex();
    ~Mutex();

    // Prevent copying raw resources
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;
};

class MutexGuard
{
private:
    Mutex &m;
    bool lock_acquired; // CRITICAL FIX #1.3: Track if lock was actually acquired

public:
    MutexGuard(Mutex &mutex);
    ~MutexGuard();
};

// 2. Semaphore & SemGuard

class Semaphore
{
private:
    sem_t sem;
    friend class SemGuard;

public:
    Semaphore(int initial_value);
    ~Semaphore();

    int get_value();

    Semaphore(const Semaphore &) = delete;
    Semaphore &operator=(const Semaphore &) = delete;
};

class SemGuard
{
private:
    Semaphore &s;
    bool sem_acquired; // CRITICAL FIX #1.4: Track if semaphore was acquired

public:
    SemGuard(Semaphore &sem);
    ~SemGuard();
};

// 3. Condition Variable

class CondVar
{
private:
    pthread_cond_t cond;

public:
    CondVar();
    ~CondVar();

    void wait(Mutex &m);
    void signal();
    void broadcast();

    CondVar(const CondVar &) = delete;
    CondVar &operator=(const CondVar &) = delete;
};

// 4. Read/Write Locks

class RWLock
{
private:
    pthread_rwlock_t rwlock;
    friend class RWLockReadGuard;
    friend class RWLockWriteGuard;

public:
    RWLock();
    ~RWLock();

    RWLock(const RWLock &) = delete;
    RWLock &operator=(const RWLock &) = delete;
};

class RWLockReadGuard
{
private:
    RWLock &lock;
    bool lock_acquired; // CRITICAL FIX #1.4: Track if lock was acquired

public:
    RWLockReadGuard(RWLock &l);
    ~RWLockReadGuard();
};

class RWLockWriteGuard
{
private:
    RWLock &lock;
    bool lock_acquired; // CRITICAL FIX #1.4: Track if lock was acquired

public:
    RWLockWriteGuard(RWLock &l);
    ~RWLockWriteGuard();
};