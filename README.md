# ⚖️ JusticeFlow

> **A legal-grade Police Case & Evidence Management System** — a multi-course semester project integrating Database Management Systems, Software Design & Architecture, Artificial Intelligence, and Operating Systems into one cohesive platform.

---

## 📌 Project Overview

JusticeFlow is a full-stack, multi-disciplinary system for managing police cases, evidence, warrants, arrests, and officer duties. It demonstrates real-world integration of four core CS disciplines:

| Course | Integration |
|--------|------------|
| **DBMS** | PostgreSQL schema with audit triggers, ACID transactions, stored procedures, and role-based permissions |
| **SDA** | 5 applied design patterns (Strategy, Observer, Repository, Singleton, Template Method), full UML documentation |
| **AI** | Crime hotspot detection, case priority recommendation, and officer workload optimization (ML models) |
| **OS** | Privilege daemon, IPC (Unix sockets, shared memory, FIFOs), thread pool, mmap, process synchronization |

---

## 🏗️ Architecture

```
┌──────────────────────────────────────────────┐
│          Flask Dashboard  (external UI)       │
└───────────────────┬──────────────────────────┘
                    │ Unix Domain Socket IPC
                    │ Flask client ⇌ C++ listener
┌───────────────────▼──────────────────────────┐
│                 API Gateway                   │
│   Listens on /tmp/justiceflow.sock            │
│   Parses JSON commands · Routes to subsystems │
└───────────────────┬──────────────────────────┘
                    │
┌───────────────────▼──────────────────────────────────────────────┐
│                        C++ Application                            │
│                                                                   │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │           Shared Infrastructure Layer                       │  │
│  │              Authentication & Session                       │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                   │
│  ┌──────────────────┐ ┌──────────────────┐ ┌─────────────────┐  │
│  │   Subsystem 1    │ │   Subsystem 2    │ │   Subsystem 3   │  │
│  │ Crime Intel &    │ │ Investigation &  │ │ Security &      │  │
│  │ Resource Optim.  │ │ Case Processing  │ │ Enforcement     │  │
│  │                  │ │                  │ │                 │  │
│  │ Case Management  │ │ FIR Registration │ │ Arrest/Warrant  │  │
│  │ Duty & Patrol    │ │ Evidence Mgmt    │ │ Forensic & Lab  │  │
│  │ Officers & Staff │ │ Charge Sheet     │ │ Audit & Comply  │  │
│  └────────┬─────────┘ └───────┬──────────┘ └───────┬─────────┘  │
└───────────┼───────────────────┼─────────────────────┼────────────┘
            │                   │ << Includes >>       │ << Includes >>
    Read/Write        ┌─────────▼──────┐  ┌───────────▼────────┐
┌───────────▼──────┐  │ Crime Hotspot  │  │  Case Priority     │
│   PostgreSQL DB  │  │ Analyzer (ML)  │  │  Recommender (ML)  │
│ public | audit   │  └────────────────┘  └────────────────────┘
│ analytics        │            << Runs On / Spawns >>
└──────────────────┘  ┌────────────────────────────────────────┐
                      │           OS Layer (Linux)              │
                      │  Scheduler | IPC | Threads | mmap      │
                      └────────────────────────────────────────┘
                             + Officer Workload Balancer (ML)
```

---

## 📁 Repository Structure

```
justiceflow/
│
├── dashboard/                      # Python Flask web interface
│   ├── app.py                      # Flask routes + views
│   ├── db.py                       # psycopg2 connection (direct DB queries)
│   ├── run.py                      # Application entry point
│   │
│   ├── client/                     # Unix socket client to C++ backend
│   │   ├── __init__.py
│   │   ├── socket_client.py        # Connects to /tmp/justiceflow.sock
│   │   ├── request_builder.py      # Builds JSON commands
│   │   └── response_parser.py      # Parses C++ responses
│   │
│   ├── services/                   # Business logic layer
│   │   ├── __init__.py
│   │   ├── auth_service.py         # Login, logout (calls C++ via socket)
│   │   ├── case_service.py         # Create/view cases
│   │   ├── duty_service.py         # Assign duties
│   │   └── officer_service.py      # Officer management
│   │
│   ├── templates/
│   │   ├── login.html
│   │   ├── dashboard.html
│   │   ├── cases.html
│   │   ├── duties.html
│   │   ├── analytics.html
│   │   ├── ai_status.html
│   │   └── os_metrics.html
│   │
│   ├── static/
│   │   ├── js/
│   │   │   ├── charts.js
│   │   │   ├── live_updates.js
│   │   │   └── socket_client.js    # WebSocket/polling for real-time updates
│   │   └── css/styles.css
│   │
│   ├── requirements.txt
│   └── README.md                   # Socket protocol documentation
│
├── db/                             # PostgreSQL database layer
│   ├── 00_schema.sql               # Schema namespaces: public, audit, analytics
│   ├── 01_types.sql                # Enum type catalog
│   ├── 02_tables.sql               # Full relational schema
│   ├── 03_functions.sql            # Stored procedures & validation functions
│   ├── 04_triggers.sql             # Trigger wiring
│   ├── 05_indexes.sql              # Index strategy
│   ├── 06_permissions.sql          # Role grants & revokes
│   ├── schema_patch.sql            # Live-schema migration patches
│   └── generate_data.py            # Synthetic data seeding pipeline
│
├── docs/                           # UML diagrams, architecture diagrams, SDA deliverables
│
├── src/                            # C++ backend + AI agents
│   │
│   ├── api_gateway/                # Flask ↔ C++ bridge / dispatcher
│   │   ├── include/
│   │   │   ├── api_gateway.h       # Request dispatcher
│   │   │   ├── command_router.h    # Routes commands to subsystems
│   │   │   └── auth_validator.h    # Token + permission checks
│   │   ├── src/
│   │   │   ├── api_gateway.cpp     # Init + main loop
│   │   │   ├── command_router.cpp  # Subsystem routing logic
│   │   │   └── auth_validator.cpp  # Auth checks (calls shr_infra auth)
│   │   └── README.md
│   │
│   ├── ai_agents/
│   │   ├── hotspot_agent.py        # Crime Hotspot Analyzer (ML model)
│   │   ├── priority_agent.py       # Case Priority Recommender (ML model)
│   │   └── workload.py             # Officer Workload Balancer (ML model)
│   │
│   ├── common/                     # Shared C++ contracts
│   │   ├── common.h                # Domain structs
│   │   ├── constants.h             # Enums & result codes
│   │   ├── dbconfig.h              # Env/file-driven DB config loader
│   │   ├── ipc_types.h             # Shared IPC struct types
│   │   └── logger.h                # Inline logger
│   │
│   ├── os_layer/                   # OS integration (C++)
│   │   ├── scheduler/              # Furqan — control plane
│   │   │   ├── include/
│   │   │   │   ├── scheduler_module.h
│   │   │   │   ├── scheduler.h
│   │   │   │   ├── daemon.h
│   │   │   │   ├── timer.h
│   │   │   │   ├── job_executor.h
│   │   │   │   ├── process_spawner.h
│   │   │   │   └── signal_handler.h
│   │   │   └── src/
│   │   │       ├── scheduler.cpp
│   │   │       ├── daemon.cpp          # double-fork + setsid
│   │   │       ├── timer.cpp           # SIGALRM + setitimer
│   │   │       ├── job_executor.cpp    # runs DB + AI jobs
│   │   │       ├── process_spawner.cpp # fork + exec agents
│   │   │       └── signal_handler.cpp  # SIGCHLD, SIGALRM
│   │   │
│   │   ├── ipc/                    # Abdullah — data plane
│   │   │   ├── include/
│   │   │   │   ├── ipc_module.h
│   │   │   │   ├── ipc_manager.h
│   │   │   │   ├── unix_socket.h
│   │   │   │   ├── fifo.h
│   │   │   │   ├── shared_memory.h
│   │   │   │   ├── shm_layout.h        # shared struct layout
│   │   │   │   ├── domain_socket.h     # Unix domain socket listener
│   │   │   │   └── socket_request.h    # request/response types
│   │   │   └── src/
│   │   │       ├── ipc_manager.cpp
│   │   │       ├── unix_socket.cpp
│   │   │       ├── fifo.cpp            # mkfifo for agents
│   │   │       ├── shared_memory.cpp   # shm_open + mmap
│   │   │       ├── domain_socket.cpp
│   │   │       └── socket_request.cpp
│   │   │
│   │   ├── threading/              # Abu Bakar — execution layer
│   │   │   ├── include/
│   │   │   │   ├── threading_module.h
│   │   │   │   ├── thread_pool.h
│   │   │   │   ├── worker.h
│   │   │   │   ├── session_manager.h
│   │   │   │   ├── connection_gate.h
│   │   │   │   └── sync.h              # mutex, semaphore, condvar wrappers
│   │   │   └── src/
│   │   │       ├── thread_pool.cpp
│   │   │       ├── worker.cpp
│   │   │       ├── session_manager.cpp
│   │   │       └── connection_gate.cpp # semaphore-based admission control
│   │   │
│   │   ├── memory/
│   │   │   ├── include/
│   │   │   │   ├── mmap_handler.h
│   │   │   │   └── mlock_guard.h
│   │   │   └── src/
│   │   │       ├── mmap_handler.cpp
│   │   │       └── mlock_guard.cpp
│   │   │
│   │   ├── process/
│   │   │   ├── include/
│   │   │   │   ├── process_manager.h
│   │   │   │   └── process_registry.h
│   │   │   └── src/
│   │   │       ├── process_manager.cpp
│   │   │       └── process_registry.cpp
│   │   │
│   │   ├── os_layer.h              # Top-level OS interface
│   │   ├── README.md
│   │   └── DESIGN.md               # Process topology, IPC diagram
│   │
│   ├── shr_infra/                  # Shared authentication & session layer
│   │   └── auth/
│   │       ├── include/
│   │       │   ├── auth_manager.h
│   │       │   ├── auth_module.h
│   │       │   ├── session_store.h
│   │       │   ├── duty_cache.h
│   │       │   └── token_generator.h
│   │       └── src/
│   │           ├── auth_manager.cpp
│   │           ├── session_store.cpp
│   │           ├── duty_cache.cpp
│   │           └── token_generator.cpp
│   │
│   ├── subsystem1/                 # Crime Intelligence & Resource Optimization
│   │                               # Case Management · Duty & Patrol · Officers & Personnel
│   │
│   ├── subsystem2/                 # Investigation & Case Processing
│   │   ├── CaseManager.h/cpp       # Case service — registration/fetch via IpcManager
│   │   ├── EvidenceManager.h/cpp   # Evidence service + mmap support
│   │   ├── InvestigationManager.h/cpp
│   │   ├── Case.h/cpp              # Case entity with Strategy transition hooks
│   │   ├── Evidence.h/cpp          # Evidence entity — Observer notifications
│   │   ├── ChargeSheet.h/cpp       # Charge sheet submission and locking
│   │   ├── ICaseStatusStrategy.h   # Strategy pattern interface
│   │   └── IEvidenceObserver.h     # Observer pattern interface
│   │
│   └── subsystem3/                 # Security & Enforcement
│       ├── audit/
│       │   ├── include/
│       │   │   ├── audit_manager.h     # Singleton entry point for audit queries
│       │   │   └── audit_query.h       # Template Method pattern — query builder
│       │   └── src/
│       │       ├── audit_manager.cpp
│       │       └── audit_query.cpp
│       │
│       ├── enforcement/
│       │   ├── include/
│       │   │   ├── warrant_manager.h   # Warrant state machine
│       │   │   ├── arrest_manager.h    # Custody state machine
│       │   │   └── bail_manager.h      # Bail state machine
│       │   └── src/
│       │       ├── warrant_manager.cpp
│       │       ├── arrest_manager.cpp
│       │       └── bail_manager.cpp
│       │
│       ├── forensic/
│       │   ├── include/
│       │   │   ├── forensic_manager.h      # Forensic state machine (6 operations)
│       │   │   └── forensic_repository.h   # Repository pattern — pure DB access
│       │   └── src/
│       │       ├── forensic_manager.cpp
│       │       └── forensic_repository.cpp
│       │
│       ├── sql/
│       │   ├── audit_triggers.sql          # SECURITY DEFINER + immutability enforcement
│       │   ├── enforcement_jobs.sql        # expire_warrants() + expire_bail_records()
│       │   └── forensic_triggers.sql       # Evidence status sync triggers
│       │
│       ├── subsystem3.h                    # Public API
│       └── subsystem3.cpp                  # Routing to managers
│
├── tests/
│   ├── db_test.cpp
│   ├── os_layer_integration_test.cpp
│   ├── os_smoke_test.cpp
│   └── superviser_test.cpp
│
├── .gitignore
├── folder_structure.txt
├── justiceflow_setup.sh            # Ubuntu bootstrap / setup script
├── MakeFile
├── requirements.txt                # Python dependencies
└── README.md
```

---

## ⚙️ Prerequisites

### System
- Ubuntu 22.04+ (or compatible Linux distribution)
- GCC 11+ / Clang 14+ with C++17 support
- CMake 3.20+
- PostgreSQL 14+

### Python
- Python 3.10+
- pip packages listed in `requirements.txt`

### C++ Libraries
- `libpq-dev` — PostgreSQL C client
- `libpthread` — POSIX threads
- `nlohmann/json` — JSON serialization

---

## 🚀 Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/Furqan-2006/JusticeFlow.git
cd JusticeFlow
```

### 2. Run the Setup Script

```bash
chmod +x justiceflow_setup.sh
./justiceflow_setup.sh
```

### 3. Initialize the Database

```bash
psql -U postgres -d justiceflow -f db/00_schema.sql
psql -U postgres -d justiceflow -f db/01_types.sql
psql -U postgres -d justiceflow -f db/02_tables.sql
psql -U postgres -d justiceflow -f db/03_functions.sql
psql -U postgres -d justiceflow -f db/04_triggers.sql
psql -U postgres -d justiceflow -f db/05_indexes.sql
psql -U postgres -d justiceflow -f db/06_permissions.sql
```

Optionally seed with synthetic data:

```bash
python3 db/generate_data.py
```

### 4. Build the C++ Backend

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 5. Install Python Dependencies

```bash
pip install -r requirements.txt
```

### 6. Start the System

```bash
# Start the C++ backend daemon first
./build/justiceflow_daemon

# Then start the Flask dashboard
cd dashboard
python3 run.py
```

The dashboard will be available at `http://localhost:5000`.

---

## 🎓 Course Integration Details

### Database Management Systems (DBMS)
- Full normalized schema across `public`, `audit`, and `analytics` namespaces
- ACID-compliant transactions enforced at application and DB level
- Audit triggers with `SECURITY DEFINER` and immutability enforcement
- Scheduled SQL jobs for warrant/bail expiry and analytics logging
- Role-based permissions with least-privilege enforcement

### Software Design & Architecture (SDA)
- **Strategy Pattern** — `ICaseStatusStrategy` for pluggable case state transitions
- **Observer Pattern** — `IEvidenceObserver` for evidence state change notifications
- **Repository Pattern** — `forensic_repository` as a pure DB-access abstraction layer
- **Singleton Pattern** — `audit_manager` as the single controlled audit entry point
- **Template Method Pattern** — `audit_query` for parameterized query construction
- UML class, sequence, and component diagrams in `docs/`

### Artificial Intelligence
- **Crime Hotspot Analyzer** (`hotspot_agent.py`) — Geospatial crime cluster detection
- **Case Priority Recommender** (`priority_agent.py`) — ML-based case urgency scoring
- **Officer Workload Balancer** (`workload.py`) — Assignment optimization via DB repository and IPC reporting

### Operating Systems
- **Daemon** — Double-fork + `setsid` privilege daemon (`daemon.cpp`)
- **Scheduler** — SIGALRM-driven tick loop with observer notifications
- **IPC** — Unix domain sockets, named FIFOs, and `mmap`-backed shared memory
- **Thread Pool** — Worker queue with semaphore-based admission control
- **Process Management** — `waitpid`-driven lifecycle, registry, and fork/exec spawning
- **Memory Safety** — `mlock` RAII guard and `mmap` handler for sensitive data

---

## 🧪 Running Tests

```bash
cd build
ctest --output-on-failure
```

| Test Binary | Purpose |
|-------------|---------|
| `db_test` | DB connectivity smoke test through the OS layer |
| `os_layer_integration_test` | Auth, threading, scheduler, and session primitives |
| `os_smoke_test` | Auth and sync primitive smoke checks |
| `superviser_test` | Supervisor loop and drain behavior |

---

## 🔐 Security Notes

- Database credentials must not be hardcoded. Use environment variables or a config file excluded via `.gitignore`.
- `auth_manager` uses `mlock` to prevent sensitive memory from being paged to disk.
- Session tokens are UUID-based, generated from OS randomness (`/dev/urandom`).
- Token validation is enforced at the API Gateway level via `auth_validator` before any subsystem is reached.

---

## 👥 Team

| Name | Responsibility |
|------|---------------|
| Furqan | OS Scheduler (control plane) — daemon, timer, job executor, process spawner |
| Abdullah | OS IPC (data plane) — Unix socket, FIFO, shared memory, domain socket listener |
| Abu Bakar | OS Threading (execution layer) — thread pool, worker, session manager, sync |

---

## 📝 License

This project was developed as a university semester project. All rights reserved by the contributing team members.
