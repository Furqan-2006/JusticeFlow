# OS Layer — Full Architecture

---

## Furqan — `process/` + `scheduler/`

### `process/`

**`process_registry.h/.cpp`**
A singleton that owns a `std::unordered_map<pid_t, ProcessRecord>` where `ProcessRecord` holds the agent name, its FIFO path, its shm segment name, and a `ProcessState` enum (`RUNNING`, `COMPLETED`, `FAILED`, `REAPED`). Pure data — no POSIX calls, no forking. Just register, lookup, update state, and remove. Every other module depends on this so it must be the first thing built in the entire OS layer.

**`process_manager.h/.cpp`**
Owns all `waitpid` logic. Two operations only — `reapOne(pid_t)` for targeted reaping and `reapAll()` for shutdown. On reap it calls `process_registry` to mark the slot `REAPED`. Also owns the restart decision — if a process exits with a non-zero code, `process_manager` checks a `restart_count` in the registry and decides whether to notify the scheduler to respawn or escalate to an error. No forking happens here — that belongs to `process_spawner`.

---

### `scheduler/`

**`daemon.h/.cpp`**
No dependencies on anything in the OS layer. Pure POSIX — double-fork, `setsid`, close inherited file descriptors, redirect stdin/stdout/stderr to `/dev/null`, write PID file. Returns a `ResultCode` to the caller. Completely stateless — just a namespace with an `init()` function. No class needed.

**`signal_handler.h/.cpp`**
Depends on `process_manager` and needs a forward reference to `Scheduler`. Owns a static signal dispatch table — a fixed array mapping signal numbers to handler functions. On `SIGALRM` calls `Scheduler::getInstance().tick()`. On `SIGCHLD` calls `process_manager.reapAll()` in a `WNOHANG` loop. On `SIGTERM`/`SIGINT` calls `Scheduler::getInstance().setState(DRAINING)`. On `SIGHUP` calls a config reload callback. All handlers kept minimal — no heap allocation, no blocking calls inside the handler itself.

**`timer.h/.cpp`**
Depends on `signal_handler` being registered before it arms anything. Owns `setitimer(ITIMER_REAL)` setup and teardown. Two methods — `arm(interval_seconds)` and `disarm()`. `disarm()` is called in the `DRAINING` state so no new ticks fire during shutdown. Tracks drift — compares expected tick time against actual delivery time using `clock_gettime` and logs if drift exceeds a threshold. Stateless otherwise — namespace, no class.

**`job_executor.h/.cpp`**
Depends on `ipc/unix_socket` being available. Owns all direct PostgreSQL job execution — `expireWarrants()`, `expireBailRecords()`, `expireWorkloadAssignments()`, `checkDatabaseHealth()`. Each is a concrete `SqlJob` subclass. Receives the `libpq` connection from `ipc_manager` — does not manage the connection itself. Connection lifecycle belongs to `ipc_manager`, not here.

**`process_spawner.h/.cpp`**
Depends on `process_registry`, `ipc_types.h` for FIFO paths, and `process_manager`. Owns `fork` + `exec` for all three AI agents. Before forking, creates the agent's FIFO with `mkfifo` and registers the pending entry in `process_registry`. After fork, parent returns immediately — never blocks. Each agent is a concrete `AgentJob` subclass. Owns FIFO cleanup on agent completion — called by the observer when `process_manager` reaps the child.

**`scheduler.h/.cpp`**
The last file Furqan writes. Singleton + State Machine. Four states — `INITIALIZING → RUNNING → DRAINING → STOPPED`. Owns the `JobRegistry` — a fixed array of `Job*` with `interval_ticks` and `next_fire_tick` values. The `tick()` method increments the counter, iterates the registry, and calls `execute()` on any due job. Owns the `JobObserver` list — `StatusTableUpdater` registers itself here. State transitions are the only thing that can interrupt a tick cycle.

```
Scheduler (Singleton, State Machine)
│
├── JobRegistry        — holds all Job objects + tick intervals
├── TickCounter        — incremented on every SIGALRM
│
├── Job (abstract)
│   ├── SqlJob         — uses job_executor → libpq → PostgreSQL
│   └── AgentJob       — uses process_spawner → fork + exec
│
└── JobObserver (abstract)
    └── StatusTableUpdater  — writes to SharedStatusTable on completion
```

**`scheduler_module.h`**
Umbrella header for the scheduler subsystem. Includes `scheduler.h`, `daemon.h`, `timer.h`, `job_executor.h`, `process_spawner.h`, and `signal_handler.h` in the correct dependency order. This is the only scheduler header that `os_layer.h` includes. No implementation — includes only.

---
---

## Abdullah — `ipc/`

**`unix_socket.h/.cpp`**
Owns the entire `libpq` connection lifecycle. Not a singleton — a connection object that `ipc_manager` holds. Wraps `PQconnectdb`, `PQfinish`, `PQstatus`, and `PQexec` behind clean methods — `connect()`, `disconnect()`, `execute()`, `isHealthy()`. Never exposes a raw `PGconn*` outside this file. `execute()` returns a `ResultCode` and passes results out by reference. All `PQresult` memory managed internally — callers never call `PQclear` directly. This is the file `job_executor` depends on so it needs to be stable early.

**`fifo.h/.cpp`**
Owns named pipe lifecycle for all three AI agents. Three operations — `create(path)` wrapping `mkfifo`, `readStatus(path, AgentStatusMessage&)` for the scheduler to receive agent completion messages, and `destroy(path)` wrapping `unlink`. Reads are non-blocking — `O_NONBLOCK` so the scheduler never hangs waiting for an agent that crashed silently. If no data is available, return a specific `ResultCode` so the caller knows to try again next tick rather than treating it as an error. Depends on `ipc_types.h` for `AgentStatusMessage` and FIFO path constants.

**`shm_layout.h`**
Header-only. Defines the fixed `ShmResultHeader` struct that every agent-result shared memory segment begins with — `magic`, `version`, `agent_pid`, `timestamp_us`, `payload_bytes`, and `status`. No implementation. Kept separate from `shared_memory.h` deliberately — any file that needs to read or write a result payload includes only this header without pulling in the full `shm_open`/`mmap` lifecycle. Both `shared_memory.h` and the Python AI agents depend on this layout being stable.

```cpp
struct ShmResultHeader {
    uint32_t magic;         // sanity sentinel
    uint32_t version;       // layout version
    pid_t    agent_pid;     // producing agent
    uint64_t timestamp_us;  // microseconds since epoch
    size_t   payload_bytes; // byte length of payload following header
    int      status;        // 0 = ok, non-zero = agent error code
};
// payload bytes follow immediately after the header
```

**`shared_memory.h/.cpp`**
Owns the `SharedStatusTable` segment lifecycle. Three operations — `create()` wrapping `shm_open` + `ftruncate` + `mmap`, `attach()` for the main application process to map an already-created segment, and `destroy()` wrapping `munmap` + `shm_unlink`. The mutex inside `SharedStatusTable` must be initialised with `PTHREAD_PROCESS_SHARED` and `PTHREAD_MUTEX_ROBUST` attributes — robust mutexes recover if the owning process dies holding the lock. Returns a typed `SharedStatusTable*` pointer, never a raw `void*`. Depends on `ipc_types.h` for the struct layout and `shm_layout.h` for the result header definition.

**`ipc_manager.h/.cpp`**
The unified interface — the only IPC file the rest of the system ever includes directly. Singleton. Owns one `unix_socket` instance, one `fifo` instance, and one `shared_memory` instance as private members. Exposes a high-level interface — `connectDatabase()`, `disconnectDatabase()`, `executeQuery()`, `readAgentStatus(agent_index, AgentStatusMessage&)`, `updateAgentStatus(agent_index, AgentStatus&)`, `getStatusTable()`. No caller outside `ipc/` ever touches `unix_socket`, `fifo`, or `shared_memory` directly.

**`ipc_module.h`**
Umbrella header for the IPC subsystem. Includes `ipc_manager.h`, `unix_socket.h`, `fifo.h`, `shared_memory.h`, and `shm_layout.h`. This is the only IPC header that `os_layer.h` includes. No implementation — includes only.

---
---

## Abu Bakar — `threading/`

**`sync.h`**
Header-only. Three RAII types — `MutexGuard` (wraps `pthread_mutex_t`, locks on construction, unlocks on destruction), `SemGuard` (wraps `sem_t`, `sem_wait` on construction, `sem_post` on destruction), `CondVar` (wraps `pthread_cond_t`, exposes `wait(MutexGuard&)`, `signal()`, `broadcast()`). Also wraps `pthread_rwlock_t` as `RWLockReadGuard` and `RWLockWriteGuard` for read-heavy config data. No `.cpp` needed — all inline. First file to write because every other threading file depends on it.

**`connection_gate.h/.cpp`**
A single counting semaphore initialised to `M` — the maximum concurrent database connections. Two methods only — `acquire()` and `release()`. Internally uses `SemGuard` from `sync.h`. Value of `M` comes from `DBConfig::max_connections`. Singleton. Every worker calls `acquire()` before touching `ipc_manager.executeQuery()` and `release()` on completion or error path. Separate from `thread_pool` by design — DB connection limit is an independent policy from pool size.

**`session_manager.h/.cpp`**
Owns the session registry — a `std::unordered_map<int, SessionContext>` mapping worker thread IDs to active sessions. Protected by `MutexGuard` from `sync.h`. Four operations — `register(thread_id, SessionContext)`, `unregister(thread_id)`, `getSession(thread_id, SessionContext&)`, and `getActiveCount()`. `getActiveCount()` is what `SharedStatusTable.active_sessions` reads — coordinate with Abdullah so the status table update reads from here rather than maintaining a separate counter. Singleton.

**`worker.h/.cpp`**
Represents one thread's execution loop. Not a thread itself — just the work function a thread runs. Owns the per-session request processing loop — authenticate via `AuthManager`, process request, release `connection_gate` slot on completion or error. Top-level `try/catch` around the entire loop — on catch, log the error, release the gate slot, and signal the pool to mark this worker available. Depends on `connection_gate`, `sync.h`, and `session_manager`. Does not create or join threads — that belongs to `thread_pool`.

**`thread_pool.h/.cpp`**
The assembler. Owns thread creation, the work queue, and shutdown. On `init(N)` creates N `pthread` threads each running the `worker` loop. Owns a condition-variable-protected work queue — `submit(Job*)` adds to the queue and calls `CondVar::signal()`, sleeping workers wake and pull the next job. On shutdown sets a flag, calls `CondVar::broadcast()` to wake all sleeping workers, then joins all N threads. The `CondVar` for AI result notification is also owned here and broadcast by `StatusTableUpdater` from the scheduler side.

**`threading_module.h`**
Umbrella header for the threading subsystem. Includes `thread_pool.h`, `worker.h`, `session_manager.h`, `connection_gate.h`, and `sync.h`. This is the only threading header that `os_layer.h` includes. No implementation — includes only.

---
---

## Shared — `memory/`

**`mmap_handler.h/.cpp`**
Owns all `mmap`/`munmap` operations and `madvise` hints for file-backed mappings. Two primary operations — `mapFile(path, size, MapMode&)` wrapping `mmap(MAP_PRIVATE | MAP_POPULATE)` for large read-only assets such as evidence files and model weights, and `unmap(addr, size)` wrapping `munmap`. `MapMode` is a small enum — `SEQUENTIAL` applies `madvise(MADV_SEQUENTIAL)`, `RANDOM` applies `madvise(MADV_RANDOM)`. Also owns `msync()` for flushing `MAP_SHARED` writes — called by the scheduler after updating the agent status table in shared memory to guarantee the main application never reads a partially written state. `MAP_PRIVATE` mappings never call `msync` — writes on those are discarded by design.

**`mlock_guard.h/.cpp`**
RAII type that pins a memory region in physical RAM. Constructor calls `mlock(addr, size)` on the region; destructor calls `munlock` and zeroes the buffer with `memset` before releasing. Intended strictly for credential buffers, session keys, and security-critical structures — never for large regions such as model weights or evidence files, to avoid exhausting the process `RLIMIT_MEMLOCK` budget. Used by `session_manager` to pin the session table and by `auth_manager` to pin credential buffers.

---
---

## Top-Level Interface

**`os_layer.h`**
The single public interface for the entire OS layer. Any module outside `os_layer/` includes only this file — never individual subsystem headers directly. Includes the three umbrella headers in dependency order:

```cpp
#include "scheduler/include/scheduler_module.h"
#include "ipc/include/ipc_module.h"
#include "threading/include/threading_module.h"
```

`memory/` and `process/` headers are not exposed through `os_layer.h` directly — they are internal utilities consumed by the three subsystems above. If an external module needs `mlock_guard` for credential pinning, it includes it directly from `memory/include/mlock_guard.h` by exception only.

---
---

## Combined OS Layer Build Order

```
Phase 1 — Common foundations (no inter-module deps)
  1.  process_registry          [Furqan]
  2.  process_manager           [Furqan]
  3.  sync.h                    [Abu Bakar]

Phase 2 — Independent modules
  4.  daemon                    [Furqan]
  5.  unix_socket               [Abdullah]   ← unblocks Furqan's job_executor
  6.  connection_gate           [Abu Bakar]
  7.  session_manager           [Abu Bakar]

Phase 3 — Mid-tier
  8.  signal_handler            [Furqan]
  9.  fifo                      [Abdullah]
  10. shm_layout.h              [Abdullah]   ← header only, no .cpp
  11. shared_memory             [Abdullah]
  12. mmap_handler              [Shared]
  13. mlock_guard               [Shared]
  14. worker                    [Abu Bakar]

Phase 4 — Assemblers
  15. timer                     [Furqan]
  16. ipc_manager               [Abdullah]   ← assembles all three transports
  17. thread_pool               [Abu Bakar]  ← assembles threading layer

Phase 5 — Top level
  18. job_executor              [Furqan]     ← needs unix_socket ready
  19. process_spawner           [Furqan]
  20. scheduler                 [Furqan]     ← assembles everything

Phase 6 — Umbrella headers + top-level interface
  21. scheduler_module.h        [Furqan]     ← header only
  22. ipc_module.h              [Abdullah]   ← header only
  23. threading_module.h        [Abu Bakar]  ← header only
  24. os_layer.h                [Furqan]     ← header only, final step
```

---

## Full Dependency Graph

```
os_layer.h
├── scheduler_module.h
│   └── scheduler
│       ├── timer
│       │   └── signal_handler
│       │       └── process_manager
│       │           └── process_registry
│       ├── job_executor
│       │   └── ipc_manager
│       │       ├── unix_socket     → libpq → PostgreSQL
│       │       ├── fifo            → ipc_types.h
│       │       └── shared_memory   → ipc_types.h + shm_layout.h
│       └── process_spawner
│           ├── process_registry
│           └── ipc_types.h
├── ipc_module.h
│   └── ipc_manager (see above)
└── threading_module.h
    └── thread_pool
        ├── worker
        │   ├── connection_gate
        │   │   └── sync.h
        │   └── session_manager
        │       └── sync.h
        └── sync.h

memory/ (internal, not in os_layer.h)
├── mmap_handler    ← used by shared_memory, job_executor
└── mlock_guard     ← used by session_manager, auth_manager
```

---

## Cross-Module Coordination Points

| Dependency | Modules Involved | Who Waits | Who Delivers |
|---|---|---|---|
| `unix_socket` stub | `job_executor` ← `unix_socket` | Furqan | Abdullah |
| `shm_layout.h` stable | `shared_memory` + AI agents ← `shm_layout.h` | Abdullah, AI team | Abdullah |
| `shm` init before thread pool | `shared_memory` ← `thread_pool` | Abu Bakar | Abdullah |
| `session_manager.getActiveCount()` feeds shm | `session_manager` → `SharedStatusTable` | Abdullah | Abu Bakar |
| `AuthManager` stub needed for `worker` | `worker` ← `AuthManager` | Abu Bakar | Furqan |
| Threading join before IPC teardown | `thread_pool` shutdown → `ipc_manager` teardown | Abdullah | Abu Bakar |
| `StatusTableUpdater` broadcasts to `thread_pool` CondVar | `scheduler` → `thread_pool` | Abu Bakar | Furqan |
| `mmap_handler.msync()` called after shm write | `mmap_handler` ← `scheduler` status update | Furqan | Shared |
| `mlock_guard` available before session table init | `mlock_guard` ← `session_manager` | Abu Bakar | Shared |
