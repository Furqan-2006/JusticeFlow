# OS Layer

The `os_layer` is the foundational system services layer of the platform. It abstracts all direct operating-system interaction — process lifecycle, inter-process communication, thread management, memory mapping, and signal handling — exposing a clean, unified interface through `os_layer.h` to every subsystem above it.

The layer is split across five subsystems, each with a single responsibility:

| Subsystem | Owner | Role |
|---|---|---|
| `scheduler/` | Furqan | Control plane — daemon lifecycle, timers, job dispatch |
| `ipc/` | Abdullah | Data plane — sockets, FIFOs, shared memory |
| `threading/` | Abu Bakar | Execution layer — thread pool, sessions, concurrency primitives |
| `memory/` | Shared | Memory mapping and locking utilities |
| `process/` | Shared | Process registry and lifecycle helpers |

---

## Directory Structure

```
os_layer/
├── scheduler/
│   ├── include/
│   │   ├── scheduler_module.h     # Umbrella header
│   │   ├── scheduler.h            # Core scheduling logic
│   │   ├── daemon.h               # Daemonisation (double-fork + setsid)
│   │   ├── timer.h                # Interval timers (SIGALRM + setitimer)
│   │   ├── job_executor.h         # DB and AI job runner
│   │   ├── process_spawner.h      # Agent spawning (fork + exec)
│   │   └── signal_handler.h       # SIGCHLD, SIGALRM handling
│   └── src/
│       ├── scheduler.cpp
│       ├── daemon.cpp
│       ├── timer.cpp
│       ├── job_executor.cpp
│       ├── process_spawner.cpp
│       └── signal_handler.cpp
│
├── ipc/
│   ├── include/
│   │   ├── ipc_module.h           # Umbrella header
│   │   ├── ipc_manager.h          # Unified IPC interface
│   │   ├── unix_socket.h          # UNIX domain sockets (PostgreSQL / libpq)
│   │   ├── fifo.h                 # Named pipes for agent communication
│   │   ├── shared_memory.h        # POSIX shared memory (shm_open + mmap)
│   │   └── shm_layout.h           # Shared struct layout definitions
│   └── src/
│       ├── ipc_manager.cpp
│       ├── unix_socket.cpp
│       ├── fifo.cpp
│       └── shared_memory.cpp
│
├── threading/
│   ├── include/
│   │   ├── threading_module.h     # Umbrella header
│   │   ├── thread_pool.h          # Thread pool management
│   │   ├── worker.h               # Individual worker thread logic
│   │   ├── session_manager.h      # Session lifecycle tracking
│   │   └── sync.h                 # Mutex, semaphore, condvar wrappers
│   └── src/
│       ├── thread_pool.cpp
│       ├── worker.cpp
│       ├── session_manager.cpp
│       └── connection_gate.cpp    # Semaphore-based connection limiter
│
├── memory/
│   ├── include/
│   │   ├── mmap_handler.h
│   │   └── mlock_guard.h
│   └── src/
│       ├── mmap_handler.cpp
│       └── mlock_guard.cpp
│
├── process/
│   ├── include/
│   │   ├── process_manager.h
│   │   └── process_registry.h
│   └── src/
│       ├── process_manager.cpp
│       └── process_registry.cpp
│
├── README.md
├── DESIGN.md
└── os_layer.h                     # Top-level public interface
```

---

## Subsystem Summaries

### Scheduler (`scheduler/`) — Control Plane
Owns the daemon lifecycle and drives all timed and event-triggered work. On startup it performs a double-fork to detach from the terminal, sets up interval timers via `setitimer`, and routes `SIGALRM` / `SIGCHLD` signals to the appropriate handlers. `job_executor` runs database maintenance and AI inference jobs; `process_spawner` forks and execs agent child processes on demand.

### IPC (`ipc/`) — Data Plane
Provides the communication backbone. `ipc_manager` presents a single interface regardless of transport. UNIX domain sockets carry PostgreSQL traffic via `libpq`; named FIFOs (created with `mkfifo`) carry lightweight command/response messages to and from spawned agents; POSIX shared memory (mapped with `shm_open` + `mmap`) carries high-throughput structured data defined in `shm_layout.h`.

### Threading (`threading/`) — Execution Layer
Manages concurrent request processing. A fixed-size thread pool picks up jobs dispatched by the scheduler. `session_manager` tracks the lifecycle of active client sessions. `connection_gate` enforces an upper bound on simultaneous database connections using a semaphore, preventing resource exhaustion under load.

### Memory (`memory/`)
Utility wrappers around `mmap`/`munmap` and `mlock`/`munlock`. `mlock_guard` is an RAII type that pins critical memory regions and releases them on scope exit, ensuring no sensitive data is ever swapped to disk.

### Process (`process/`)
Shared helpers used across subsystems. `process_registry` maintains a live table of all child PIDs, their roles, and their current state. `process_manager` wraps `waitpid` and provides clean reap/restart logic consumed by `signal_handler`.

---

## Public Interface

All consumers outside `os_layer/` should include only:

```cpp
#include "os_layer.h"
```

This header transitively pulls in `scheduler_module.h`, `ipc_module.h`, and `threading_module.h`. Direct inclusion of subsystem headers from outside the layer is discouraged.

---

## Dependencies

| Dependency | Used by |
|---|---|
| POSIX (`unistd.h`, `signal.h`, `sys/wait.h`) | scheduler, process |
| `sys/mman.h` | memory, ipc/shared_memory |
| `sys/socket.h`, `sys/un.h` | ipc/unix_socket |
| `libpq` (PostgreSQL C client) | ipc/unix_socket |
| `pthread` | threading |

---

## Ownership

| Subsystem | Primary Owner |
|---|---|
| `scheduler/` | Furqan |
| `ipc/` | Abdullah |
| `threading/` | Abu Bakar |
| `memory/`, `process/` | Shared |