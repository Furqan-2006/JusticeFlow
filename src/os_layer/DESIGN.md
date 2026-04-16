# OS Layer — Design Document

This document describes the internal architecture of the `os_layer`: process topology, IPC transport choices, concurrency model, and the full startup and shutdown sequences.

---

## Table of Contents

1. [Architectural Goals](#1-architectural-goals)
2. [Process Topology](#2-process-topology)
3. [IPC Architecture](#3-ipc-architecture)
4. [Threading Model](#4-threading-model)
5. [Signal Handling](#5-signal-handling)
6. [Startup Sequence](#6-startup-sequence)
7. [Shutdown Sequence](#7-shutdown-sequence)
8. [Memory Strategy](#8-memory-strategy)
9. [Error Handling & Fault Isolation](#9-error-handling--fault-isolation)
10. [Design Decisions & Trade-offs](#10-design-decisions--trade-offs)

---

## 1. Architectural Goals

- **Isolation** — faults in agent child processes must not crash the supervisor.
- **Low latency IPC** — shared memory for bulk data, FIFOs for control signals; no unnecessary copies.
- **Bounded concurrency** — a semaphore gate prevents thread or DB connection storms under peak load.
- **Clean daemonisation** — the supervisor detaches fully from the terminal and session of its launcher.
- **Deterministic shutdown** — every child is reaped, every IPC resource is destroyed, no zombies or leaked shm segments.

---

## 2. Process Topology

```
                        ┌─────────────────────────────────────┐
                        │           Supervisor Process        │
                        │         (daemon, PID stored in      │
                        │          /var/run/<app>.pid)        │
                        │                                     │
                        │  ┌──────────┐   ┌────────────────┐  │
                        │  │ Scheduler│   │  Thread Pool   │  │
                        │  │  (timer) │   │  (N workers)   │  │
                        │  └────┬─────┘   └───────┬────────┘  │
                        │       │                 │           │
                        │  ┌────▼─────────────────▼────────┐  │
                        │  │         IPC Manager           │  │
                        │  │  unix_socket | fifo | shm     │  │
                        │  └────┬──────────────┬───────────┘  │
                        └───────┼──────────────┼──────────────┘
                                │              │
               fork+exec        │              │ fork+exec
                    ┌───────────▼──┐      ┌────▼──────────────┐
                    │  Agent [0]   │      │    Agent [N]      │
                    │  (child PID) │ ...  │    (child PID)    │
                    └──────────────┘      └───────────────────┘
```

**Supervisor** is the single long-running daemon. It owns the scheduler, the thread pool, and all IPC endpoints.

**Agents** are short-to-medium-lived child processes created by `process_spawner` via `fork` + `exec`. Each agent:

- reads commands from its dedicated FIFO (`/tmp/<app>_agent_<pid>.fifo`)
- writes results into a named shared memory segment (layout defined by `shm_layout.h`)
- signals completion by writing a byte to the FIFO

The `process_registry` maps each child PID to its role and FIFO/shm identifiers, enabling the supervisor to route results and reap children cleanly.

---

## 3. IPC Architecture

Three transports are used concurrently, each selected for a specific traffic class:

```
 ┌────────────────────────────────────────────────────────────────┐
 │                         IPC Manager                            │
 │                   (unified interface layer)                    │
 └───────────┬──────────────────┬─────────────────┬───────────────┘
             │                  │                 │
    ┌────────▼──────┐  ┌────────▼──────┐  ┌───────▼────────┐
    │  UNIX Socket  │  │  Named FIFO   │  │ Shared Memory  │
    │  (libpq conn  │  │  (mkfifo)     │  │ (shm_open +    │
    │   to Postgres)│  │               │  │  mmap)         │
    └───────────────┘  └───────────────┘  └────────────────┘
          │                   │                   │
          ▼                   ▼                   ▼
    PostgreSQL DB       Agent control        Bulk result
    (queries, writes)   (commands,           data exchange
                         completion acks)    (zero-copy)
```

### Transport Decision Table

| Traffic Class             | Transport                           | Rationale                                           |
| ------------------------- | ----------------------------------- | --------------------------------------------------- |
| Database queries / writes | UNIX socket via `libpq`             | Standard, reliable; PostgreSQL speaks this natively |
| Agent command dispatch    | Named FIFO                          | Simple, ordered, no setup overhead per message      |
| Agent result payloads     | POSIX shared memory                 | Zero-copy for potentially large inference outputs   |
| AI / DB job triggers      | Internal (scheduler → job_executor) | In-process call, no IPC needed                      |

### Shared Memory Layout (`shm_layout.h`)

Each agent-result segment follows a fixed header + payload structure:

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

Segments are named `/shm_agent_<pid>` and unlinked by the supervisor after the result is consumed.

---

## 4. Threading Model

```
   Incoming Jobs (from Scheduler)
           │
           ▼
   ┌───────────────┐
   │  Thread Pool  │   N worker threads (N configured at startup)
   │  (work queue) │─────────────────────────────────────────────┐
   └───────────────┘                                             │
           │                                                     │
     ┌─────▼─────┐   ┌──────────────┐   ┌────────────────┐       │
     │  Worker   │   │   Worker     │   │    Worker      │  ...  │
     │  Thread   │   │   Thread     │   │    Thread      │       │
     └─────┬─────┘   └──────┬───────┘   └────────┬───────┘       │
           │                │                    │               │
           └────────────────┼────────────────────┘               │
                            │                                    │
                   ┌────────▼────────┐                           │
                   │ Connection Gate │  (semaphore, max M slots) │
                   └────────┬────────┘                           │
                            │                                    │
                   ┌────────▼────────┐                           │
                   │  IPC Manager /  │                           │
                   │   PostgreSQL    │                           │
                   └─────────────────┘                           │
```

- **Thread pool size (N)** is set from configuration at daemon startup and does not resize at runtime.
- **Connection gate (M)** is a POSIX semaphore (`sem_init`) initialised to the maximum permitted simultaneous DB connections. Every worker acquires one slot before touching the database and releases it on completion or error. `M ≤ N` always.
- **Session manager** tracks which worker is serving which logical session; used for logging, timeout enforcement, and orderly teardown.
- **`sync.h`** provides thin RAII wrappers (`MutexGuard`, `SemGuard`, `CondVar`) so no subsystem calls raw `pthread_mutex_lock` / `sem_wait` directly.

---

## 5. Signal Handling

All signal handling is centralised in `signal_handler.cpp`. The handler installs masks on all worker threads so signals are delivered only to the main supervisor thread.

| Signal    | Source                | Action                                                            |
| --------- | --------------------- | ----------------------------------------------------------------- |
| `SIGALRM` | `setitimer` expiry    | Tick the scheduler; trigger due jobs                              |
| `SIGCHLD` | Agent child exits     | `waitpid(WNOHANG)` loop; update process_registry; reap or restart |
| `SIGTERM` | System / orchestrator | Begin graceful shutdown sequence                                  |
| `SIGINT`  | Developer / terminal  | Same as `SIGTERM` in daemon mode                                  |
| `SIGHUP`  | Config reload request | Re-read config; reconfigure timer intervals                       |

`SIGCHLD` uses `waitpid` in a loop (not a single call) to handle multiple children exiting between signal deliveries — a standard `SIGCHLD` safe pattern.

---

## 6. Startup Sequence

```
main()
  │
  ├─1─ Parse config / CLI args
  │
  ├─2─ daemon.cpp: double-fork + setsid
  │       fork() → parent exits
  │       setsid() → new session, no controlling terminal
  │       fork() → session leader exits
  │       child continues as daemon
  │
  ├─3─ Write PID file (/var/run/<app>.pid)
  │
  ├─4─ signal_handler: install handlers (SIGALRM, SIGCHLD, SIGTERM, SIGHUP)
  │       Block all signals on future worker threads via pthread_sigmask
  │
  ├─5─ ipc_manager: initialise transports
  │       unix_socket: open libpq connection pool to PostgreSQL
  │       fifo: ensure FIFO directory exists (no per-agent FIFOs yet)
  │       shared_memory: (no segments yet; created per-agent at spawn time)
  │
  ├─6─ thread_pool: create N worker threads
  │       connection_gate: initialise semaphore to M
  │       session_manager: initialise empty session table
  │
  ├─7─ process_registry: initialise empty registry
  │
  ├─8─ scheduler: initialise job queue, load job definitions from config
  │
  ├─9─ timer: arm setitimer(ITIMER_REAL, interval)
  │
  └─10─ Enter main event loop (sigsuspend / select on control FD)
```

After step 10 the daemon is fully operational. `SIGALRM` drives the scheduler tick; worker threads pull jobs from the queue and process them through the connection gate.

---

## 7. Shutdown Sequence

Triggered by `SIGTERM` or `SIGINT`:

```
signal_handler receives SIGTERM
  │
  ├─1─ scheduler: stop arming new timer ticks; drain pending job queue
  │
  ├─2─ process_spawner: send SIGTERM to all registered agent PIDs
  │       Wait up to T seconds for clean exit (SIGKILL stragglers)
  │       process_registry: reap all children via waitpid
  │
  ├─3─ thread_pool: set shutdown flag; wake all waiting workers
  │       Join all N threads (workers finish in-flight jobs first)
  │
  ├─4─ ipc_manager: tear down transports
  │       shared_memory: shm_unlink all remaining segments
  │       fifo: unlink all agent FIFOs
  │       unix_socket: close libpq connections
  │
  ├─5─ memory: munlock + munmap any pinned regions via mlock_guard RAII
  │
  ├─6─ Remove PID file
  │
  └─7─ exit(0)
```

Steps 2 and 3 are the longest; the rest are sub-millisecond. No IPC resource is leaked across a clean shutdown.

---

## 8. Memory Strategy

The OS layer uses four distinct memory mechanisms, each chosen for a specific access pattern.
The guiding principle is: **avoid unnecessary copies, and never let sensitive data reach swap**.

### POSIX Shared Memory — Agent Result Payloads

Agent result payloads (inference outputs, query results) are exchanged through `shm_open` +
`mmap(MAP_SHARED)`. When the supervisor spawns an agent, it creates a named segment
`/shm_agent_<pid>` and passes the name to the child. The agent writes its result directly into
the mapped region; the supervisor reads from the same physical pages — no kernel copy occurs in
either direction. The supervisor calls `shm_unlink` immediately after consuming the result so the
segment does not outlive its use.

### Memory-Mapped Files — Large Data, Model Assets & Shared Memory Flushes

Large files (model weights, bulk datasets, evidence files) are loaded through `mmap_handler`
using `mmap(MAP_PRIVATE | MAP_POPULATE)`. `MAP_POPULATE` pre-faults the pages at map time,
trading an upfront cost for elimination of page-fault stalls during inference. `madvise(MADV_SEQUENTIAL)`
or `madvise(MADV_RANDOM)` hints are applied depending on the known access pattern of the asset.
Writes are never needed on these mappings; `MAP_PRIVATE` ensures the underlying file is never modified.

`mmap_handler` also owns `msync()` calls for any `MAP_SHARED` region where writes must be
flushed to the backing store before the segment is handed off or unlinked. Specifically, the
scheduler calls `msync(MS_SYNC)` on the agent status table in shared memory after each update
to guarantee the main application never reads a partially written state.

### Memory Locking — Sensitive In-Memory Data

Credential buffers, session keys, and any material that must not be written to disk are pinned
via `mlock_guard`. This is an RAII type: the constructor calls `mlock` on the region, the
destructor calls `munlock` and zeroes the buffer before releasing. Locking is intentionally
narrow — only the exact buffers that hold sensitive data — to avoid exhausting the process
`RLIMIT_MEMLOCK` budget. Large regions such as model weights are never locked.

### General Heap Allocations

Everything else uses standard `malloc` / C++ allocators. No custom allocator is introduced at
this layer. Keeping the allocator default simplifies tooling (Valgrind, ASan, heaptrack) and
avoids the class of bugs that custom pools introduce.

### Summary

| Concern                        | Mechanism                              | Key property                  |
|-------------------------------|----------------------------------------|-------------------------------|
| Agent result payloads          | `shm_open` + `mmap(MAP_SHARED)`        | Zero-copy between processes   |
| Large file / model data + shm flushes | `mmap(MAP_PRIVATE \| MAP_POPULATE)` + `msync()` | Pre-faulted, no mutation; writes flushed atomically |
| Sensitive in-memory data       | `mlock_guard` (RAII)                   | Never swapped, zeroed on free |
| General allocations            | Standard `malloc` / C++ allocators     | Tooling-compatible, simple    |


---

## 9. Error Handling & Fault Isolation

**Agent crashes** are isolated by design — agents are separate processes. `SIGCHLD` triggers a reap cycle; `process_registry` marks the slot as dead. The scheduler decides whether to restart (transient failure) or escalate (repeated failure within a window).

**Worker thread panics** are contained within `worker.cpp`'s top-level try/catch. A faulting worker logs the error, releases any held `connection_gate` slot, and signals the pool to replace itself.

**IPC failures** (broken FIFO, shm_open error) are surfaced as typed error codes through `ipc_manager`; callers never see raw `errno`. On a fatal IPC error the affected agent is killed and reaped; the supervisor does not exit.

**Timer drift** — `setitimer` is re-armed after every `SIGALRM` handler returns. Accumulated drift is logged; jobs that miss their window are enqueued with a `LATE` tag for observability.

---

## 10. Design Decisions & Trade-offs

### Why double-fork for daemonisation?

The first fork lets the parent exit (satisfying the shell). `setsid()` creates a new session. The second fork ensures the daemon is never a session leader, preventing it from accidentally acquiring a controlling terminal if it opens a TTY device.

### Why `setitimer` instead of `timerfd` or `timer_create`?

`setitimer` is universally available across POSIX targets. `timerfd` would require Linux-specific code. The single-process constraint (only one `ITIMER_REAL` per process) is acceptable because we have one scheduler.

### Why FIFOs for agent control and not another UNIX socket?

FIFOs require zero handshake and no accept loop. Each agent opens its own FIFO read-end; the supervisor writes the command and moves on. For the simple command/ack protocol this is sufficient and avoids socket lifecycle management per agent.

### Why shared memory for agent results instead of the FIFO?

Inference outputs can be large (kilobytes to megabytes). Writing them through a FIFO requires a kernel copy on both ends. Shared memory is a single `mmap` reference; the agent writes directly into the segment the supervisor will read — zero extra copies.

### Why a semaphore gate instead of a connection pool inside `libpq`?

A semaphore at the threading layer enforces the concurrency limit regardless of which `libpq` function is called, including multi-step transactions. A pool-level limit inside `libpq` would only guard the connection checkout step.

### Why are `memory/` and `process/` shared rather than owned?

Both subsystems are pure utilities with no state that belongs to a single control-plane or data-plane concern. Assigning them to Scheduler or IPC would create artificial coupling; shared ownership with a clear API surface is cleaner.
