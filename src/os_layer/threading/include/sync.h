#pragma once

#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

// 1. Mutex & MutexGuard

class Mutex {
private:
    pthread_mutex_t lock;
    friend class MutexGuard;
    friend class CondVar;

public: 
    Mutex();
    ~Mutex();

    // Prevent copying raw resources
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
};

class MutexGuard {
private:
    Mutex& m;
public:
    MutexGuard(Mutex& mutex);
    ~MutexGuard();
};

// 2. Semaphore & SemGuard

class Semaphore {
private:
    sem_t sem;
    friend class SemGuard;
    
public: 
    Semaphore(int initial_value);
    ~Semaphore();
    
    int get_value();

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
};

class SemGuard {
private:
    Semaphore& s;
public:
    SemGuard(Semaphore& sem);
    ~SemGuard();
};

// 3. Condition Variable

class CondVar {
private:
    pthread_cond_t cond;
public:
    CondVar();
    ~CondVar();
    
    void wait(Mutex& m);
    void signal();
    void broadcast();

    CondVar(const CondVar&) = delete;
    CondVar& operator=(const CondVar&) = delete;
};

// 4. Read/Write Locks

class RWLock {
private:
    pthread_rwlock_t rwlock;
    friend class RWLockReadGuard;
    friend class RWLockWriteGuard;
public:
    RWLock();
    ~RWLock();

    RWLock(const RWLock&) = delete;
    RWLock& operator=(const RWLock&) = delete;
};

class RWLockReadGuard {
private:
    RWLock& lock;
public:
    RWLockReadGuard(RWLock& l);
    ~RWLockReadGuard();
};

class RWLockWriteGuard {
private:
    RWLock& lock;
public:
    RWLockWriteGuard(RWLock& l);
    ~RWLockWriteGuard();
};