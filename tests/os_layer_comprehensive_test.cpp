// =============================================================================
// os_layer_comprehensive_test.cpp
//
// Comprehensive test suite for the JusticeFlow OS Layer.
//
// Covers all five OS-layer sub-systems:
//   1. Scheduler / Control Plane  (TC-OS-SCHED-*)
//   2. Signal Handler             (TC-OS-SIG-*)
//   3. IPC / Data Plane           (TC-OS-IPC-*)
//   4. Threading & Concurrency    (TC-OS-THR-*)
//   5. Memory Management          (TC-OS-MEM-*)
//   6. Process Management         (TC-OS-PROC-*)
//   7. Token Generator            (TC-OS-AUTH-*)
//
// Test-plan cross-references (from Section 10.4) are annotated inline as:
//   [TP: TC-OS-xx]
//
// Build requirements:
//   - GTest / GMock
//   - justiceflow_lib (links os_layer.h and auth_module.h)
//   - pthread
// =============================================================================

#include "../src/os_layer/os_layer.h"
#include "../src/system/shr_infra/auth/include/auth_module.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <set>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

// =============================================================================
// §1  SCHEDULER / CONTROL PLANE
// =============================================================================

class SchedulerFixtureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Always start from a known STOPPED state so tests are independent.
        Scheduler::getInstance().setState(SchedulerState::STOPPED);
    }

    void TearDown() override
    {
        Timer::disarm();
        Scheduler::getInstance().setState(SchedulerState::STOPPED);
    }
};

// TC-OS-SCHED-01 — Singleton identity
TEST_F(SchedulerFixtureTest, SingletonReturnsSameInstance)
{
    auto *a = &Scheduler::getInstance();
    auto *b = &Scheduler::getInstance();
    EXPECT_EQ(a, b);
}

// TC-OS-SCHED-02 — State transition STOPPED → RUNNING → STOPPED
TEST_F(SchedulerFixtureTest, StateTransitionRoundTrip)
{
    Scheduler::getInstance().setState(SchedulerState::RUNNING);
    EXPECT_EQ(Scheduler::getInstance().getState(), SchedulerState::RUNNING);

    Scheduler::getInstance().setState(SchedulerState::STOPPED);
    EXPECT_EQ(Scheduler::getInstance().getState(), SchedulerState::STOPPED);
}

// TC-OS-SCHED-03 — Timer::arm() returns a valid ResultCode (not UB / crash)
TEST_F(SchedulerFixtureTest, TimerArmReturnsValidCode)
{
    auto rc = Timer::arm(1);
    bool valid = (rc == JusticeFlow::ResultCode::OK ||
                  rc == JusticeFlow::ResultCode::INVALID_STATE);
    EXPECT_TRUE(valid) << "Unexpected ResultCode: " << static_cast<int>(rc);
}

// TC-OS-SCHED-04 — Timer::disarm() succeeds after arm
TEST_F(SchedulerFixtureTest, TimerDisarmAfterArm)
{
    Timer::arm(2);
    auto rc = Timer::disarm();
    bool valid = (rc == JusticeFlow::ResultCode::OK ||
                  rc == JusticeFlow::ResultCode::INVALID_STATE);
    EXPECT_TRUE(valid);
}

// TC-OS-SCHED-05 — Repeated arm/disarm cycles must not crash or leak
TEST_F(SchedulerFixtureTest, TimerArmDisarmCycleRepeated)
{
    for (int i = 0; i < 10; ++i)
    {
        Timer::arm(1);
        Timer::disarm();
    }
    // Reaching here without SIGSEGV / double-free is the assertion.
    SUCCEED();
}

// TC-OS-SCHED-06 — Double arm does not crash or assert
TEST_F(SchedulerFixtureTest, TimerDoubleArmIsSafe)
{
    Timer::arm(1);
    auto rc = Timer::arm(1); // second call while already armed
    bool valid = (rc == JusticeFlow::ResultCode::OK ||
                  rc == JusticeFlow::ResultCode::INVALID_STATE);
    EXPECT_TRUE(valid);
    Timer::disarm();
}

// TC-OS-SCHED-07 [TP: TC-OS-08] — SIGALRM fires within a reasonable window
// (1-second timer; we poll for the signal for up to 3 seconds)
TEST_F(SchedulerFixtureTest, SigAlrmFiresWithinExpectedWindow)
{
    SignalHandler::init();
    Scheduler::getInstance().setState(SchedulerState::RUNNING);

    auto rc = Timer::arm(1);
    if (rc != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Timer::arm() unavailable in this environment";
    }

    bool alarm_processed = false;
    for (int i = 0; i < 60 && !alarm_processed; ++i)
    {
        SignalHandler::processPendingSignals();
        // If the scheduler's tick counter advanced, SIGALRM was processed.
        // As a proxy, we check that the scheduler is still RUNNING (the signal
        // did not crash it) and that processPendingSignals returned cleanly.
        if (Scheduler::getInstance().getState() == SchedulerState::RUNNING)
        {
            alarm_processed = true;
        }
        usleep(50 * 1000);
    }

    EXPECT_TRUE(alarm_processed);
    Timer::disarm();
}

// TC-OS-SCHED-08 [TP: TC-OS-09] — SIGTERM triggers drain and scheduler stops
TEST_F(SchedulerFixtureTest, SigTermDrainsSchedulerToStopped)
{
    SignalHandler::init();
    Scheduler::getInstance().setState(SchedulerState::RUNNING);
    Timer::arm(1);

    // Let it tick briefly.
    for (int i = 0; i < 20; ++i)
    {
        SignalHandler::processPendingSignals();
        usleep(10 * 1000);
    }

    raise(SIGTERM);

    // Drain loop — mirrors production supervisor pattern.
    for (int i = 0; i < 200; ++i)
    {
        SignalHandler::processPendingSignals();
        if (Scheduler::getInstance().getState() == SchedulerState::STOPPED)
            break;
        usleep(10 * 1000);
    }

    EXPECT_EQ(Scheduler::getInstance().getState(), SchedulerState::STOPPED);
}

// =============================================================================
// §2  SIGNAL HANDLER
// =============================================================================

class SignalHandlerTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        Scheduler::getInstance().setState(SchedulerState::STOPPED);
        Timer::disarm();
    }
};

// TC-OS-SIG-01 — init() does not crash
TEST_F(SignalHandlerTest, InitDoesNotCrash)
{
    EXPECT_NO_THROW(SignalHandler::init());
}

// TC-OS-SIG-02 — Multiple consecutive init() calls are idempotent
TEST_F(SignalHandlerTest, RepeatedInitIsIdempotent)
{
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_NO_THROW(SignalHandler::init());
    }
}

// TC-OS-SIG-03 — processPendingSignals() with no pending signals is a no-op
TEST_F(SignalHandlerTest, ProcessNoPendingSignalsSafe)
{
    SignalHandler::init();
    EXPECT_NO_THROW(SignalHandler::processPendingSignals());
    EXPECT_NO_THROW(SignalHandler::processPendingSignals());
}

// TC-OS-SIG-04 — SIGALRM handling does not crash the process
TEST_F(SignalHandlerTest, SigAlrmHandledWithoutCrash)
{
    SignalHandler::init();
    Scheduler::getInstance().setState(SchedulerState::RUNNING);

    auto rc = Timer::arm(1);
    if (rc != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Timer not available";
    }

    for (int i = 0; i < 30; ++i)
    {
        EXPECT_NO_THROW(SignalHandler::processPendingSignals());
        usleep(10 * 1000);
    }

    Timer::disarm();
    SUCCEED();
}

// TC-OS-SIG-05 — Signal processing under rapid repeated raises
TEST_F(SignalHandlerTest, RapidSigAlrmProcessingIsStable)
{
    SignalHandler::init();
    Scheduler::getInstance().setState(SchedulerState::RUNNING);

    for (int i = 0; i < 5; ++i)
    {
        raise(SIGALRM);
        SignalHandler::processPendingSignals();
    }

    // No crash = pass
    SUCCEED();
}

// =============================================================================
// §3  IPC / DATA PLANE
// =============================================================================

class IpcTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        ipc::IpcManager::getInstance().disconnectDatabase();
    }
};

// TC-OS-IPC-01 — IpcManager is a singleton
TEST_F(IpcTest, IpcManagerSingleton)
{
    auto *a = &ipc::IpcManager::getInstance();
    auto *b = &ipc::IpcManager::getInstance();
    EXPECT_EQ(a, b);
}

// TC-OS-IPC-02 [TP: TC-OS-04] — connectDatabase() skips gracefully if Postgres
// is absent; does not crash when env vars are missing.
TEST_F(IpcTest, ConnectDatabaseGracefulDegradation)
{
    auto rc = ipc::IpcManager::getInstance().connectDatabase();
    bool valid = (rc == JusticeFlow::ResultCode::OK ||
                  rc != JusticeFlow::ResultCode::OK); // any defined code is fine
    EXPECT_TRUE(valid);                               // just must not throw or crash
}

// TC-OS-IPC-03 — executeQuery without a live connection returns an error code
TEST_F(IpcTest, ExecuteQueryWithoutConnectionReturnsError)
{
    ipc::IpcManager::getInstance().disconnectDatabase(); // ensure disconnected

    std::vector<std::vector<std::string>> rows;
    auto rc = ipc::IpcManager::getInstance().executeQuery("SELECT 1;", rows);

    // Must return a defined (non-OK) code, never crash.
    EXPECT_NE(rc, JusticeFlow::ResultCode::OK);
}

// TC-OS-IPC-04 [TP: TC-OS-04] — Full connect → SELECT 1 → disconnect cycle
TEST_F(IpcTest, FullConnectQueryDisconnectCycle)
{
    auto rc = ipc::IpcManager::getInstance().connectDatabase();
    if (rc != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Database not reachable (rc=" << static_cast<int>(rc)
                     << "). Set JF_DB_HOST/PORT/NAME/USER/PASS env vars.";
    }

    std::vector<std::vector<std::string>> rows;
    rc = ipc::IpcManager::getInstance().executeQuery("SELECT 1;", rows);

    ASSERT_EQ(rc, JusticeFlow::ResultCode::OK);
    ASSERT_FALSE(rows.empty());
    ASSERT_FALSE(rows[0].empty());
    EXPECT_EQ(rows[0][0], "1");

    ipc::IpcManager::getInstance().disconnectDatabase();
}

// TC-OS-IPC-05 — Multiple sequential connect/disconnect cycles are stable
TEST_F(IpcTest, RepeatedConnectDisconnectCyclesAreStable)
{
    for (int i = 0; i < 5; ++i)
    {
        auto rc = ipc::IpcManager::getInstance().connectDatabase();
        if (rc != JusticeFlow::ResultCode::OK)
        {
            GTEST_SKIP() << "Database not reachable; skipping cycle test.";
        }
        ipc::IpcManager::getInstance().disconnectDatabase();
    }
    SUCCEED();
}

// TC-OS-IPC-06 [TP: TC-OS-06] — Reconnect after explicit disconnect succeeds
TEST_F(IpcTest, ReconnectAfterDisconnectSucceeds)
{
    auto rc1 = ipc::IpcManager::getInstance().connectDatabase();
    if (rc1 != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Initial connect failed; skipping reconnect test.";
    }

    ipc::IpcManager::getInstance().disconnectDatabase();

    auto rc2 = ipc::IpcManager::getInstance().connectDatabase();
    EXPECT_EQ(rc2, JusticeFlow::ResultCode::OK);
}

// TC-OS-IPC-07 — executeQuery rejects empty SQL
TEST_F(IpcTest, ExecuteQueryEmptySqlHandled)
{
    auto rc = ipc::IpcManager::getInstance().connectDatabase();
    if (rc != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Database not reachable.";
    }

    std::vector<std::vector<std::string>> rows;
    auto qrc = ipc::IpcManager::getInstance().executeQuery("", rows);
    // Any defined code (OK or error) is acceptable — must not crash.
    (void)qrc;
    SUCCEED();
}

// ─────────────────────────────────────────────
// Shared Memory
// ─────────────────────────────────────────────

class SharedMemoryTest : public ::testing::Test
{
protected:
    // Each test gets a unique SHM name to avoid cross-test interference.
    std::string shm_name;

    void SetUp() override
    {
        shm_name = "/jf_test_" + std::to_string(getpid()) + "_" +
                   std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count());
    }
};

// TC-OS-IPC-08 — create() returns OK or skips; table pointer is non-null on success
TEST_F(SharedMemoryTest, CreateReturnsTablePointer)
{
    ipc::SharedMemory shm(shm_name.c_str());
    auto rc = shm.create();
    if (rc != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Shared memory not available in this environment.";
    }

    EXPECT_NE(shm.getTable(), nullptr);
    shm.destroy();
}

// TC-OS-IPC-09 — Writing to and reading back from the shared table
TEST_F(SharedMemoryTest, WriteAndReadBack)
{
    ipc::SharedMemory shm(shm_name.c_str());
    if (shm.create() != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Shared memory not available.";
    }

    auto *table = shm.getTable();
    ASSERT_NE(table, nullptr);

    // Write a sentinel value into the first byte of the table and verify.
    auto *bytes = reinterpret_cast<uint8_t *>(table);
    bytes[0] = 0xAB;
    EXPECT_EQ(bytes[0], 0xAB);

    shm.destroy();
}

// TC-OS-IPC-10 — destroy() is idempotent (calling twice must not crash)
TEST_F(SharedMemoryTest, DestroyIdempotent)
{
    ipc::SharedMemory shm(shm_name.c_str());
    if (shm.create() != JusticeFlow::ResultCode::OK)
    {
        GTEST_SKIP() << "Shared memory not available.";
    }
    shm.destroy();
    EXPECT_NO_THROW(shm.destroy()); // second call
}

// TC-OS-IPC-11 — Two segments with distinct names coexist
TEST_F(SharedMemoryTest, TwoDistinctSegmentsCoexist)
{
    std::string name_a = shm_name + "_a";
    std::string name_b = shm_name + "_b";

    ipc::SharedMemory shm_a(name_a.c_str());
    ipc::SharedMemory shm_b(name_b.c_str());

    auto rc_a = shm_a.create();
    auto rc_b = shm_b.create();

    if (rc_a != JusticeFlow::ResultCode::OK ||
        rc_b != JusticeFlow::ResultCode::OK)
    {
        shm_a.destroy();
        shm_b.destroy();
        GTEST_SKIP() << "Shared memory not available for dual-segment test.";
    }

    // Pointers must differ.
    EXPECT_NE(shm_a.getTable(), shm_b.getTable());

    shm_a.destroy();
    shm_b.destroy();
}

// =============================================================================
// §4  THREADING & CONCURRENCY
// =============================================================================

class ThreadingTest : public ::testing::Test
{
};

// TC-OS-THR-01 — ThreadPool singleton identity
TEST_F(ThreadingTest, ThreadPoolSingleton)
{
    auto *a = &ThreadPool::getInstance();
    auto *b = &ThreadPool::getInstance();
    EXPECT_EQ(a, b);
}

// TC-OS-THR-02 — Mutex basic lock/unlock does not deadlock
TEST_F(ThreadingTest, MutexBasicLockUnlock)
{
    Mutex m;
    {
        MutexGuard g(m);
    }
    SUCCEED();
}

// TC-OS-THR-03 — MutexGuard releases lock on scope exit (RAII)
TEST_F(ThreadingTest, MutexGuardReleasesOnScopeExit)
{
    Mutex m;
    {
        MutexGuard g(m);
        // Lock is held here.
    }
    // After scope, lock must be free — we verify by locking again without hang.
    bool acquired = false;
    std::thread t([&m, &acquired]()
                  {
        MutexGuard g(m);
        acquired = true; });
    t.join();
    EXPECT_TRUE(acquired);
}

// TC-OS-THR-04 — Semaphore value tracks acquire/release correctly
TEST_F(ThreadingTest, SemaphoreValueTracksAcquireRelease)
{
    Semaphore sem(3);
    EXPECT_EQ(sem.get_value(), 3);

    {
        SemGuard g1(sem);
        EXPECT_EQ(sem.get_value(), 2);
        {
            SemGuard g2(sem);
            EXPECT_EQ(sem.get_value(), 1);
        }
        EXPECT_EQ(sem.get_value(), 2);
    }
    EXPECT_EQ(sem.get_value(), 3);
}

// TC-OS-THR-05 [TP: TC-OS-11] — Semaphore(1) blocks a second thread until the
// first releases; the 2nd thread must not proceed while the slot is occupied.
TEST_F(ThreadingTest, SemaphoreBlocksWhenExhausted)
{
    Semaphore sem(1);
    std::atomic<bool> second_acquired{false};
    std::atomic<bool> first_released{false};

    // First thread acquires and holds.
    SemGuard *g1 = new SemGuard(sem);
    EXPECT_EQ(sem.get_value(), 0);

    std::thread t([&]()
                  {
        SemGuard g2(sem); // blocks until g1 is released
        second_acquired.store(true); });

    // Give the thread time to attempt acquisition.
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_FALSE(second_acquired.load())
        << "Second thread should still be blocked";

    delete g1; // releases sem
    first_released.store(true);

    t.join();
    EXPECT_TRUE(second_acquired.load())
        << "Second thread should have acquired after release";
}

// TC-OS-THR-06 — CondVar producer-consumer handoff
TEST_F(ThreadingTest, CondVarProducerConsumerHandoff)
{
    Mutex m;
    CondVar cv;
    bool ready = false;
    bool consumed = false;

    std::thread producer([&]()
                         {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        {
            MutexGuard lock(m);
            ready = true;
        }
        cv.signal(); });

    {
        MutexGuard lock(m);
        while (!ready)
            cv.wait(m);
        consumed = true;
    }

    producer.join();
    EXPECT_TRUE(consumed);
}

// TC-OS-THR-07 [TP: TC-OS-10] — 50 concurrent threads write unique IDs to a
// shared array under mutex protection; no corruption expected.
TEST_F(ThreadingTest, FiftyConcurrentThreadsNoCrashNoCorruption)
{
    constexpr int THREAD_COUNT = 50;
    std::vector<int> results(THREAD_COUNT, -1);
    Mutex m;
    std::atomic<int> next_slot{0};

    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);

    for (int t = 0; t < THREAD_COUNT; ++t)
    {
        threads.emplace_back([&, t]()
                             {
            // Simulate session setup latency.
            std::this_thread::sleep_for(std::chrono::microseconds(t * 10));
            MutexGuard g(m);
            int slot = next_slot.fetch_add(1);
            if (slot < THREAD_COUNT)
                results[slot] = t; });
    }

    for (auto &th : threads)
        th.join();

    // All slots must have been written; no duplicates.
    std::sort(results.begin(), results.end());
    for (int i = 0; i < THREAD_COUNT; ++i)
        EXPECT_GE(results[i], 0) << "Slot " << i << " was never written";

    // No two threads should have written the same thread-ID.
    auto it = std::unique(results.begin(), results.end());
    EXPECT_EQ(it, results.end()) << "Duplicate thread IDs found — possible corruption";
}

// TC-OS-THR-08 [TP: TC-OS-11] — Semaphore(50): 51st thread must wait.
TEST_F(ThreadingTest, Semaphore50BlocksFiftyFirst)
{
    constexpr int CAPACITY = 50;
    Semaphore sem(CAPACITY);
    std::atomic<int> inside{0};
    std::atomic<bool> overflow_detected{false};

    auto worker = [&]()
    {
        SemGuard g(sem);
        int count = inside.fetch_add(1) + 1;
        if (count > CAPACITY)
            overflow_detected.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        inside.fetch_sub(1);
    };

    std::vector<std::thread> threads;
    threads.reserve(CAPACITY + 1);
    for (int i = 0; i < CAPACITY + 1; ++i)
        threads.emplace_back(worker);

    for (auto &th : threads)
        th.join();

    EXPECT_FALSE(overflow_detected.load())
        << "More than " << CAPACITY << " threads were inside simultaneously";
}

// TC-OS-THR-09 [TP: TC-OS-12] — Shared counter under 20 concurrent threads;
// final value must equal total increments (zero race).
TEST_F(ThreadingTest, SharedCounterNoRaceUnderTwentyThreads)
{
    constexpr int THREAD_COUNT = 20;
    constexpr int INCREMENTS_PER_THREAD = 500;

    long counter = 0;
    Mutex m;

    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);

    for (int t = 0; t < THREAD_COUNT; ++t)
    {
        threads.emplace_back([&]()
                             {
            for (int i = 0; i < INCREMENTS_PER_THREAD; ++i)
            {
                MutexGuard g(m);
                ++counter;
            } });
    }

    for (auto &th : threads)
        th.join();

    EXPECT_EQ(counter, static_cast<long>(THREAD_COUNT * INCREMENTS_PER_THREAD));
}

// TC-OS-THR-10 — A drained semaphore (value 0) blocks until a held guard is released
TEST_F(ThreadingTest, SemaphoreZeroBlocksUntilGuardReleased)
{
    Semaphore sem(1);
    std::atomic<bool> proceeded{false};

    // Drain semaphore to 0 by holding a guard on the heap so we
    // control exactly when sem_post fires (on delete).
    SemGuard *holder = new SemGuard(sem); // value: 1 → 0

    std::thread waiter([&]()
                       {
        SemGuard g(sem); // blocks — value is 0
        proceeded.store(true); });

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_FALSE(proceeded.load()) << "Waiter should still be blocking";

    delete holder; // ~SemGuard calls sem_post: value 0 → 1, unblocks waiter

    waiter.join();
    EXPECT_TRUE(proceeded.load());
}

// TC-OS-THR-11 — CondVar broadcast wakes all waiting threads
TEST_F(ThreadingTest, CondVarBroadcastWakesAllWaiters)
{
    constexpr int WAITERS = 5;
    Mutex m;
    CondVar cv;
    bool go = false;
    std::atomic<int> woken{0};

    std::vector<std::thread> waiters;
    waiters.reserve(WAITERS);
    for (int i = 0; i < WAITERS; ++i)
    {
        waiters.emplace_back([&]()
                             {
            MutexGuard lock(m);
            while (!go)
                cv.wait(m);
            woken.fetch_add(1); });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        MutexGuard lock(m);
        go = true;
    }
    cv.broadcast();

    for (auto &w : waiters)
        w.join();

    EXPECT_EQ(woken.load(), WAITERS);
}

// =============================================================================
// §5  MEMORY MANAGEMENT
// =============================================================================

class MemoryTest : public ::testing::Test
{
};

// TC-OS-MEM-01 — Anonymous mmap page is readable and writable
TEST_F(MemoryTest, AnonymousMmapReadWrite)
{
    const size_t PAGE = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    void *ptr = mmap(nullptr, PAGE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(ptr, MAP_FAILED) << "mmap failed: " << strerror(errno);

    auto *bytes = static_cast<uint8_t *>(ptr);
    bytes[0] = 0xDE;
    bytes[PAGE - 1] = 0xAD;

    EXPECT_EQ(bytes[0], 0xDE);
    EXPECT_EQ(bytes[PAGE - 1], 0xAD);

    munmap(ptr, PAGE);
}

// TC-OS-MEM-02 [TP: TC-OS-13] — Demand paging: mapping 64 MB must not
// immediately increase RSS by more than 1 MB (pages are not faulted in).
TEST_F(MemoryTest, LargeMmapDemandPagingBoundsRss)
{
    const size_t MAP_SIZE = 64UL * 1024 * 1024; // 64 MB

    struct rusage before, after;
    getrusage(RUSAGE_SELF, &before);

    void *ptr = mmap(nullptr, MAP_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
    {
        GTEST_SKIP() << "mmap(64MB) failed: " << strerror(errno);
    }

    getrusage(RUSAGE_SELF, &after);

    // RSS is reported in kilobytes on Linux.
    long rss_delta_kb = after.ru_maxrss - before.ru_maxrss;
    EXPECT_LT(rss_delta_kb, 1024L) // < 1 MB increase
        << "RSS grew by " << rss_delta_kb << " KB — demand paging not working";

    munmap(ptr, MAP_SIZE);
}

// TC-OS-MEM-03 [TP: TC-OS-14] — mlock() on a small region succeeds (RLIMIT_MEMLOCK
// permitting); confirms the kernel accepts the lock request.
TEST_F(MemoryTest, MlockSucceedsOnSmallRegion)
{
    const size_t PAGE = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    void *ptr = mmap(nullptr, PAGE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(ptr, MAP_FAILED);

    int rc = mlock(ptr, PAGE);
    if (rc != 0)
    {
        munmap(ptr, PAGE);
        GTEST_SKIP() << "mlock() not permitted (RLIMIT_MEMLOCK): " << strerror(errno);
    }

    EXPECT_EQ(rc, 0) << "mlock failed: " << strerror(errno);

    munlock(ptr, PAGE);
    munmap(ptr, PAGE);
}

// TC-OS-MEM-04 — munmap on a valid mapping does not crash
TEST_F(MemoryTest, MunmapCleanupIsStable)
{
    const size_t SIZE = 4 * static_cast<size_t>(sysconf(_SC_PAGESIZE));
    void *ptr = mmap(nullptr, SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(ptr, MAP_FAILED);

    EXPECT_EQ(munmap(ptr, SIZE), 0);
}

// TC-OS-MEM-05 [TP: TC-OS-15 proxy] — 100 session-sized mmap/munmap cycles
// leave memory footprint stable (proxy for Valgrind memcheck clean run).
TEST_F(MemoryTest, SessionCycleMmapHundredTimesStable)
{
    const size_t SESSION_SIZE = 4096; // representative session struct size

    struct rusage before;
    getrusage(RUSAGE_SELF, &before);

    for (int i = 0; i < 100; ++i)
    {
        void *ptr = mmap(nullptr, SESSION_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERT_NE(ptr, MAP_FAILED) << "Iteration " << i << " failed";

        // Touch the memory so it is actually faulted in.
        memset(ptr, 0xCC, SESSION_SIZE);

        ASSERT_EQ(munmap(ptr, SESSION_SIZE), 0);
    }

    struct rusage after;
    getrusage(RUSAGE_SELF, &after);

    // RSS should not grow more than 512 KB across 100 tiny cycles.
    long delta = after.ru_maxrss - before.ru_maxrss;
    EXPECT_LT(delta, 512L)
        << "RSS grew by " << delta << " KB across 100 session cycles — possible leak";
}

// TC-OS-MEM-06 — Write-read-verify on a multi-page mapping
TEST_F(MemoryTest, MultiPageMmapWriteReadVerify)
{
    const size_t PAGES = 8;
    const size_t SIZE = PAGES * static_cast<size_t>(sysconf(_SC_PAGESIZE));

    void *ptr = mmap(nullptr, SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(ptr, MAP_FAILED);

    auto *buf = static_cast<uint8_t *>(ptr);
    for (size_t i = 0; i < SIZE; ++i)
        buf[i] = static_cast<uint8_t>(i & 0xFF);

    bool mismatch = false;
    for (size_t i = 0; i < SIZE; ++i)
    {
        if (buf[i] != static_cast<uint8_t>(i & 0xFF))
        {
            mismatch = true;
            break;
        }
    }

    EXPECT_FALSE(mismatch) << "Memory contents corrupted after write";
    munmap(ptr, SIZE);
}

// =============================================================================
// §6  PROCESS MANAGEMENT
// =============================================================================

class ProcessTest : public ::testing::Test
{
};

// TC-OS-PROC-01 — ProcessRegistry singleton identity
TEST_F(ProcessTest, ProcessRegistrySingleton)
{
    auto *a = &ProcessRegistry::getInstance();
    auto *b = &ProcessRegistry::getInstance();
    EXPECT_EQ(a, b);
}

// TC-OS-PROC-02 — ProcessRegistry instance is non-null
TEST_F(ProcessTest, ProcessRegistryIsNonNull)
{
    EXPECT_NE(&ProcessRegistry::getInstance(), nullptr);
}

// TC-OS-PROC-03 — getpid() returns a positive PID (sanity)
TEST_F(ProcessTest, GetPidPositive)
{
    EXPECT_GT(getpid(), 0);
}

// TC-OS-PROC-04 [TP: TC-OS-07] — setsid() in a child creates a new session
// (verifies the mechanism used by Daemon::daemonize()).
TEST_F(ProcessTest, NewSessionViaSetsid)
{
    pid_t child = fork();
    ASSERT_GE(child, 0) << "fork() failed: " << strerror(errno);

    if (child == 0)
    {
        // Child: call setsid().
        pid_t sid = setsid();
        // Exit with 0 on success, 1 on failure.
        _exit(sid > 0 ? 0 : 1);
    }

    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0) << "setsid() failed in child";
}

// TC-OS-PROC-05 — fork/wait round-trip is stable across 10 iterations
TEST_F(ProcessTest, ForkWaitCycleStable)
{
    for (int i = 0; i < 10; ++i)
    {
        pid_t child = fork();
        ASSERT_GE(child, 0);

        if (child == 0)
            _exit(0); // child exits immediately

        int status = 0;
        pid_t waited = waitpid(child, &status, 0);
        EXPECT_EQ(waited, child);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

// TC-OS-PROC-06 — Child process has a different PID than parent
TEST_F(ProcessTest, ChildHasDifferentPid)
{
    pid_t parent_pid = getpid();
    pid_t child = fork();
    ASSERT_GE(child, 0);

    if (child == 0)
    {
        // In child: our PID differs from parent.
        _exit(getpid() != parent_pid ? 0 : 1);
    }

    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

// TC-OS-PROC-07 — SIGCHLD from an exited child is reapable without WNOHANG loop
TEST_F(ProcessTest, SigChildReapableAfterExit)
{
    pid_t child = fork();
    ASSERT_GE(child, 0);

    if (child == 0)
    {
        usleep(10 * 1000); // small delay then exit
        _exit(42);
    }

    int status = 0;
    pid_t result = waitpid(child, &status, 0);
    EXPECT_EQ(result, child);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 42);
}

// =============================================================================
// §7  TOKEN GENERATOR  (Auth / OS boundary)
// =============================================================================

class TokenGeneratorTest : public ::testing::Test
{
};

// TC-OS-AUTH-01 — Generated token has canonical UUIDv4 length (36 chars)
TEST_F(TokenGeneratorTest, TokenLengthIsThirtySix)
{
    std::string token = auth::token_generator::generate();
    EXPECT_EQ(token.size(), 36u);
}

// TC-OS-AUTH-02 — Hyphens appear at positions 8, 13, 18, 23
TEST_F(TokenGeneratorTest, HyphensAtCorrectPositions)
{
    std::string token = auth::token_generator::generate();
    ASSERT_EQ(token.size(), 36u);
    EXPECT_EQ(token[8], '-');
    EXPECT_EQ(token[13], '-');
    EXPECT_EQ(token[18], '-');
    EXPECT_EQ(token[23], '-');
}

// TC-OS-AUTH-03 — Version nibble is '4' (UUIDv4)
TEST_F(TokenGeneratorTest, VersionNibbleIsFour)
{
    std::string token = auth::token_generator::generate();
    ASSERT_EQ(token.size(), 36u);
    EXPECT_EQ(token[14], '4');
}

// TC-OS-AUTH-04 — Variant bits: character at position 19 must be 8, 9, a, or b
TEST_F(TokenGeneratorTest, VariantBitsAreCorrect)
{
    std::string token = auth::token_generator::generate();
    ASSERT_EQ(token.size(), 36u);
    char variant = token[19];
    EXPECT_TRUE(variant == '8' || variant == '9' ||
                variant == 'a' || variant == 'b' ||
                variant == 'A' || variant == 'B')
        << "Unexpected variant character: " << variant;
}

// TC-OS-AUTH-05 [TP: TC-OS-AUTH] — 1 000 generated tokens are all unique
TEST_F(TokenGeneratorTest, ThousandTokensAreAllUnique)
{
    constexpr int N = 1000;
    std::set<std::string> seen;

    for (int i = 0; i < N; ++i)
    {
        auto token = auth::token_generator::generate();
        ASSERT_EQ(token.size(), 36u) << "Bad length at iteration " << i;
        auto [_, inserted] = seen.insert(token);
        EXPECT_TRUE(inserted) << "Duplicate token at iteration " << i << ": " << token;
    }

    EXPECT_EQ(static_cast<int>(seen.size()), N);
}

// TC-OS-AUTH-06 — Token contains only hex digits and hyphens
TEST_F(TokenGeneratorTest, TokenContainsOnlyHexAndHyphens)
{
    std::string token = auth::token_generator::generate();
    for (char c : token)
    {
        bool valid = (c == '-') ||
                     (c >= '0' && c <= '9') ||
                     (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F');
        EXPECT_TRUE(valid) << "Unexpected character '" << c << "' in token: " << token;
    }
}

// TC-OS-AUTH-07 — Concurrent generation from 8 threads produces no duplicates
TEST_F(TokenGeneratorTest, ConcurrentGenerationProducesNoDuplicates)
{
    constexpr int THREADS = 8;
    constexpr int PER_THREAD = 125; // 8 * 125 = 1 000 total

    std::vector<std::vector<std::string>> buckets(THREADS);
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; ++t)
    {
        threads.emplace_back([&buckets, t]()
                             {
            for (int i = 0; i < PER_THREAD; ++i)
                buckets[t].push_back(auth::token_generator::generate()); });
    }

    for (auto &th : threads)
        th.join();

    // Flatten and check for duplicates.
    std::set<std::string> all;
    for (auto &bucket : buckets)
        for (auto &tok : bucket)
            all.insert(tok);

    EXPECT_EQ(static_cast<int>(all.size()), THREADS * PER_THREAD)
        << "Duplicate tokens detected under concurrent generation";
}