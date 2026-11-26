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
├── dashboard/                      # Python Flask web interface (read-only today)
│   ├── app.py                      # Flask routes; every datum comes from the gateway
│   ├── run.py                      # Entry point (127.0.0.1:5000; JF_DASHBOARD_HOST/PORT)
│   ├── client/
│   │   └── gateway.py              # Unix-socket client for /tmp/justiceflow.sock
│   ├── templates/                  # base, dashboard, cases, case_detail, analytics,
│   │                               # workload, ai_status, os_metrics
│   ├── static/                     # css/styles.css, js/live_updates.js (5 s polling)
│   ├── legacy/                     # old Streamlit prototype (direct DB, mock data); unused
│   └── requirements.txt            # Flask only. The dashboard never opens a DB connection
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
│   │   ├── ha.py                   # Crime Hotspot Analyzer
│   │   ├── pa.py                   # Case Priority Recommender
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
│   │   │       ├── process_spawner.cpp # spawn agents, reap them, record status
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

### 2. Run the demo

You need PostgreSQL, a C++17 toolchain with libpq headers, Python 3 and `make`. Install the Python packages once, then point the scripts at a PostgreSQL account that can create databases:

```bash
make setup-python                       # agents' dependencies (numpy, scikit-learn, psycopg2, ...)
pip install -r dashboard/requirements.txt

export PGHOST=localhost PGUSER=postgres PGPASSWORD=<admin password>
make demo                               # reset DB, build, start daemon + dashboard
make smoke                              # walk the demo story and check every step
make demo-stop
```

`make demo` creates the database from `db/schema/`, seeds about 22,000 synthetic rows (150 officers, all with the password `justice123`), builds the daemon, and starts it and the dashboard at `http://localhost:5000`. It prints a CNIC to sign in with. The daemon and agents use the development passwords of the roles the schema creates; set `JF_DB_PASS` and `JF_AI_DB_PASS` to override them.

### 3. The demo story

Sign in as an Inspector, then:

1. **Register an FIR** from the Cases page. It gets a generated FIR number.
2. **Attach evidence** on the case page. It gets an evidence number.
3. **Issue a warrant** on the same page (Inspector rank or above).
4. **Warrant expiry:** the daemon expires warrants whose `valid_until` date has passed. Dates are whole days, so to see this live, move one back as the database admin, for example `UPDATE warrants SET issue_date = CURRENT_DATE - 3, valid_until = CURRENT_DATE - 1 WHERE warrant_number = '...'`. Within 5 seconds (the interval `make demo` sets) it shows as Expired.
5. **AI agents:** the AI Analytics, Workload and AI Agents pages fill in a few seconds after start as the agents run.
6. **Audit trail:** every change is recorded in `audit.audit_log` with the officer's id and belt number, and the application role cannot alter or delete those rows.

### 4. Running the pieces by hand

```bash
make db-reset                           # needs the PG* admin variables above
make build

# daemon (headless: serves the gateway until SIGTERM/SIGINT)
export JF_DB_HOST=127.0.0.1 JF_DB_USER=justice_app JF_DB_PASS=<justice_app password>
export JF_AI_DB_PASS=<justice_ai password>
./build/justiceflow --headless --pid-file /tmp/justiceflow.pid

# dashboard
cd dashboard && python3 run.py
```

Sign in with an officer's CNIC and password (the seeded officers all use `justice123`). The daemon verifies the credentials, issues a session token, and checks it on every data request. Set `JF_DASHBOARD_SECRET` to keep dashboard logins across restarts. After five wrong passwords a CNIC is locked for five minutes.

### Background jobs

While it runs, the daemon expires warrants whose `valid_until` date has passed (every 30 s) and runs the three AI agents (hotspot, priority, workload) every 5 minutes, the first run a few seconds after start. The agents connect as the `justice_ai` role, so give the daemon its password and the agent location:

```bash
export JF_AI_DB_PASS=<justice_ai password>
export JF_AGENT_SCRIPT_DIR=$PWD/src/ai_agents   # default: src/ai_agents relative to where you start it
# optional: JF_AGENT_INTERVAL_S (300), JF_EXPIRE_INTERVAL_S (30), JF_AI_DB_HOST / JF_AI_DB_NAME / JF_AI_DB_USER
```

An agent's status on the AI Agents page is 0 for a successful last run; otherwise it is the agent's exit code (124 means it timed out) with the reason beside it.

Sign in with an officer's CNIC and password (the seeded officers all use `justice123`; run `make db-reset` to reseed). The daemon verifies the credentials, issues a session token, and checks it on every data request. Set `JF_DASHBOARD_SECRET` to keep dashboard logins across restarts. After five wrong passwords a CNIC is locked for five minutes.

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
- **Crime Hotspot Analyzer** (`ha.py`) — Geospatial crime cluster detection (DBSCAN)
- **Case Priority Recommender** (`pa.py`) — Case urgency scoring
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

`make smoke` (with the stack running, see above) is the end-to-end check: it signs in, registers an FIR, adds evidence, issues a warrant, waits for the daemon to expire it and for the agents to run, checks the audit trail and that the application role cannot change it, then confirms the dashboard shows the result.

The C++ unit and integration binaries are built and listed with `make list-tests`; run one with, for example, `make db_test`.

---

## 🔐 Security Notes

- Database credentials must not be hardcoded. Use environment variables or a config file excluded via `.gitignore`.
- `auth_manager` uses `mlock` to prevent sensitive memory from being paged to disk.
- Session tokens are UUID-based, generated from OS randomness (`/dev/urandom`).
- Every data request and every write carries a session token, checked by the gateway's `CommandRouter` before anything runs. Writes execute as the signed-in officer, so the audit log names the person who acted.
- Failed logins return one generic message and are throttled per CNIC.
- The dashboard keeps the token in a signed, HttpOnly cookie and never opens a database connection.

---

## 👥 Team

| Name | Responsibility |
|------|---------------|
| Furqan | Subsystem 3, Authentication module, OS Scheduler (control plane) — daemon, timer, job executor, process spawner |
| Abdullah | Subsystem 2, Flask Dashboard, OS IPC (data plane) — Unix socket, FIFO, shared memory, domain socket listener |
| Abu Bakar | Subsystem 1, Flask Dashboard, OS Threading (execution layer) — thread pool, worker, session manager, sync |

---

## 📝 License

This project was developed as a university semester project. All rights reserved by the contributing team members.
