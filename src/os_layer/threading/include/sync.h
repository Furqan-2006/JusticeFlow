#ifndef SYNC_H
#define SYNC_H

#include <pthread.h>
#include <semaphore.h>

// 1. Mutex & MutexGuard

class Mutex
{
private:
    pthread_mutex_t lock;
    friend class MutexGuard;
    friend class CondVar;

public: 
    Mutex()
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        
        // we need robust mutexes for shared memory 
        // robust mutexes: if a thread dies while having acquired the mutex, the OS marks it as inconsistent, another thread can recover/acquire the mutex
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
        // allowing mutex to be shared in between processes as well, NOT ONLY THREADS
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        
        pthread_mutex_init(&lock, &attr);
        // actual mutex banadia with yeh wale attributes 
        pthread_mutexattr_destroy(&attr);
    }
    
    ~Mutex()
    {
        pthread_mutex_destroy(&lock);
    }
    // destroys the mutex when obj del, resource leaks are prevented
};

class MutexGuard
{
private:
    Mutex& m;
public:
    MutexGuard(Mutex& mutex) : m(mutex)
    {
        pthread_mutex_lock(&m.lock);
    }
    ~MutexGuard()
    {
        pthread_mutex_unlock(&m.lock);
    }
};

// 2. Semaphore & SemGuard

class Semaphore 
{
private:
    sem_t sem;
    // every semaphore has a counter + waiting queue, controlling access to shared resources 
    friend class SemGuard;
    
public: 
    Semaphore(int initial_value)
    {
        sem_init(&sem, 0, initial_value);
        // &sem -> reference to the semaphore object
        // 0 -> the number of threads that share the semaphore 
        // initial_value -> starting count the number of threads that are permitted to enter their critical sections   
    }
    
    ~Semaphore()
    {
        sem_destroy(&sem);
    }
    
    int get_value()
    {
        int val = 0;
        sem_getvalue(&sem, &val);
        // reads current semaphore initial_value into the val variable 
        return val;
    }
};

class SemGuard
{
private:
    Semaphore& s;
    // reference to a Semaphore Object 
public:
    SemGuard(Semaphore& sem) : s(sem)
    {
        sem_wait(&s.sem);
        // semaphore acquired and the available resources decrement for the semaphore 
    }
    ~SemGuard()
    {
        sem_post(&s.sem);
        // releases the acquired semaphore
    }
};

// 3. Condition Variable
/*
This is used for thread coordination waiting + notification, not the same as a mutex lock 
A system where the thread sleeps until someone tells them to continue, this someone is another thread that operates and tells this thread to continue when a certain condition is met
this is called wakeup -> when another thread signals 

NOT A LOCK BUT A WAITING MECHANISM TIED TO A MUTEX 
*/

class CondVar
{
private:
    pthread_cond_t cond;
public:
    CondVar()
    {
        pthread_cond_init(&cond, nullptr);
    }
    ~CondVar()
    {
        pthread_cond_destroy(&cond);
    }
    
    void wait(Mutex& m)
    {
        pthread_cond_wait(&cond, &m.lock);
        /*
            what happens here [3 atomic operations]
            - unlock mutex (so that other threads may start their work, and when a condiiton is met that other thread can signal this thread to wake up)
            - puts thread to sleep
            - relocks mutex when waking up
            
            WHY THIS IS NECESSARY

            Imagine:

            Thread A checks condition
            Condition is false
            Thread A sleeps

            If mutex was NOT released:

            Thread B could never update condition -> DEADLOCK
        */
    }
    // a thread calls this when it wants to sleep until a condition is met 
    
    void signal()
    {
        pthread_cond_signal(&cond);
    }
    // wakes up 1 waiting thread
    
    void broadcast()
    {
        pthread_cond_broadcast(&cond);    
    }
    // wakes up all waiting threads
};

// 4. Read/Write Locks (For config data)

class RWLock
{
private:
    pthread_rwlock_t rwlock;
    friend class RWLockReadGuard;
    friend class RWLockWriteGuard;
public:
    RWLock()
    {
        pthread_rwlock_init(&rwlock, nullptr);
    }
    ~RWLock()
    {
        pthread_rwlock_destroy(&rwlock);
    }
};

class RWLockReadGuard
{
private:
    RWLock& lock;
public:
    RWLockReadGuard(RWLock& l) : lock(l)
    {
        pthread_rwlock_rdlock(&lock.rwlock);
    } 
    ~RWLockReadGuard()
    {
        pthread_rwlock_unlock(&lock.rwlock);
    }
};

class RWLockWriteGuard
{
private:
    RWLock& lock;
public:
    RWLockWriteGuard(RWLock& l) : lock(l)
    {
        pthread_rwlock_wrlock(&lock.rwlock);
    }
    ~RWLockWriteGuard()
    {
        pthread_rwlock_unlock(&lock.rwlock);
    }
};

#endif
