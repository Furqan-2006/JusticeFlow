<div align="center">

# ⚖️ JusticeFlow

### Police Case & Evidence Management System
### *Legal-Grade Integrated Software Engineering Platform*

![PostgreSQL](https://img.shields.io/badge/PostgreSQL-316192?style=for-the-badge&logo=postgresql&logoColor=white)
![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Python](https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)

![DBMS](https://img.shields.io/badge/Course-DBMS-blue?style=flat-square)
![SDA](https://img.shields.io/badge/Course-SDA-green?style=flat-square)
![AI](https://img.shields.io/badge/Course-AI-orange?style=flat-square)
![OS](https://img.shields.io/badge/Course-OS-red?style=flat-square)

**Status:** 🚀 Active Development | **Language:** C++ (60.1%) · Python (20.4%) · PL/pgSQL (8.7%)

</div>

---

## 📋 Executive Summary

**JusticeFlow** is a multi-disciplinary, **legal-grade police case and evidence management system** that integrates four computer science domains into a cohesive, production-ready platform. It manages the complete investigation lifecycle—from **First Information Report (FIR)** registration through **evidence collection, warrant issuance, forensic requests, audit compliance**, and finally **case closure**—while enforcing legal compliance and role-based access control at every step.

Each layer solves a **distinct, non-overlapping** real-world problem. Removing any one layer breaks the system. This is the test of genuine integration.

```
┌─────────────────────────────────────────────────────────┐
│  REMOVE ANY ONE LAYER → SYSTEM LOSES CRITICAL FUNCTION  │
│  That is the test of genuine multi-discipline design.   │
└─────────────────────────────────────────────────────────┘
```

---

## 🏗️ System Architecture

```
┌────────────────────────────────────────────────────────┐
│            Dashboard (Flask + WebSocket)                │
│       Real-time case analytics & officer dashboard      │
└────────────────────┬─────────────────────────────────────┘
                     │
         ┌───────────▼────────────┐
         │   API Gateway (C++)    │
         │  · Command dispatcher  │
         │  · Auth validator      │
         │  · Socket handler      │
         └───────────┬────────────┘
                     │
    ┌────────────────┼────────────────┐
    │                │                │
    ▼                ▼                ▼
┌─────────────┐  ┌──────────────┐  ┌─────────────┐
│ Shared      │  │  OS Layer    │  │  AI Agents  │
│ Infra       │  │              │  │             │
│ · Auth      │  │ · Scheduler  │  │ · Hotspot   │
│ · Session   │  │ · IPC        │  │ · Priority  │
│ · Duty      │  │ · Threading  │  │ · Workload  │
└────────┬────┘  └──────┬───────┘  └──────┬──────┘
         │               │                 │
    ┌────▼───────────────▼─────────────────▼────┐
    │   Application Modules (C++)                │
    │ ┌──────────────────────────────────────┐   │
    │ │  Module 0: Audit & Compliance        │   │
    │ │  • Audit logging & activity tracking │   │
    │ │  • Suspicious activity detection     │   │
    │ ├──────────────────────────────────────┤   │
    │ │  Module 1: Security (Warrant/Arrest) │   │
    │ │  • Access control & policy engine    │   │
    │ │  • Warrant/arrest/bail operations    │   │
    │ │  • Chain of Responsibility handlers  │   │
    │ ├──────────────────────────────────────┤   │
    │ │  Module 2: Forensic & Lab            │   │
    │ │  • Forensic request processing       │   │
    │ │  • Lab analysis tracking             │   │
    │ │  • Evidence admissibility rules      │   │
    │ └──────────────────────────────────────┘   │
    └────┬────────────────────────────────────────┘
         │
    ┌────▼────────────────────────────────────┐
    │   Database Layer (PostgreSQL)           │
    │ ┌──────────────────────────────────────┐ │
    │ │  • ACID Transactions & Triggers      │ │
    │ │  • Role-Based Access Control (Views) │ │
    │ │  • Audit Log (Immutable)             │ │
    │ │  • Soft Deletes (Chain of Custody)   │ │
    │ └──────────────────────────────────────┘ │
    └─────────────────────────────────────────┘
```

---

## 📚 Core Modules & Subsystems

### 🗄️ Layer 1: Database Foundation (DBMS)

**Enforces legal-grade correctness** through full normalization, ACID compliance, and immutable audit trails.

| Feature | Implementation |
|---------|-----------------|
| **Audit Triggers** | Every `INSERT/UPDATE/DELETE` on critical tables logs old & new values to `Audit_Log` |
| **Soft Delete** | Evidence is **never** physically removed—`is_deleted` flag preserves chain of custody |
| **Stored Procedures** | Atomic operations: `assign_officer_to_case`, case status transitions, workload validation |
| **Security Views** | Role-restricted views: `constable_cases`, `inspector_cases`, `sho_cases` |
| **Referential Integrity** | Foreign keys + triggers ensure no orphaned records |
| **Composite Indexing** | Cases(status, filed_date), Evidence(case_id, collection_date) for performance |

```sql
-- Immutable audit trail
CREATE TRIGGER audit_case_changes
AFTER UPDATE ON Cases
FOR EACH ROW
EXECUTE FUNCTION audit_change('Cases', NEW.case_id);

-- Chain of custody via soft delete
UPDATE Evidence
SET is_deleted = true, deleted_by = current_user, deleted_at = NOW()
WHERE evidence_id = $1;
-- Physical deletion is forbidden.
```

---

### 🧩 Layer 2: Application Architecture (SDA)

**Design patterns + UML** ensuring maintainability and extensibility across 5+ justified patterns.

| Pattern | Problem | Where |
|---------|---------|-------|
| **Chain of Responsibility** | Rank-based escalation (Constable → Inspector → SHO → Commissioner) | `security/policy_engine.h` |
| **Observer** | Case updates notify audit logger, notifications, analytics pipeline simultaneously | `audit/activity_tracker.h` |
| **Factory** | Unified report generation (daily summary, case history, forensic requests) | API Gateway command router |
| **Strategy** | Rank-conditional case status transitions and operation validation | `security/access_control.h` |
| **Singleton** | Database connection pool + session manager (single shared resource) | `shr_infra/auth/session_store.h` |

```cpp
// Chain of Responsibility in action
class PolicyHandler {
    PolicyHandler* next;
public:
    bool authorize(Officer* o, Action a) {
        if (canHandle(a)) return checkPermission(o, a);
        return next ? next->authorize(o, a) : false;
    }
};

// Usage: Rank escalation ladder
InspectorHandler* inspector = new InspectorHandler();
DSPHandler* dsp = new DSPHandler();
SPHandler* sp = new SPHandler();
inspector->setNext(dsp);
dsp->setNext(sp);
// Constable attempt → Inspector → DSP → SP
```

**UML Deliverables:**
- ✅ Use Case Diagram
- ✅ Class Diagram (with full inheritance)
- ✅ Sequence Diagram (case filing flow)
- ✅ State Diagram (case lifecycle)
- ✅ Activity Diagram (officer workflow)

---

### 🖥️ Layer 3: Operating System Infrastructure (OS)

**Five subsystems** providing process isolation, IPC, concurrency, and system-level auditing.

| Subsystem | Owner | Role |
|-----------|-------|------|
| **Scheduler** | Furqan | Daemon lifecycle, timers, job dispatch, signal handling |
| **IPC** | Abdullah | Unix sockets (PostgreSQL), FIFOs (agents), shared memory |
| **Threading** | Abu Bakar | Thread pool, worker threads, session management, sync primitives |
| **Memory** | Shared | mmap/mlock utilities, secure memory pinning |
| **Process** | Shared | Child process registry, lifecycle helpers, reaping |

#### Scheduler Subsystem
- **Double-fork daemonization** + setsid for terminal detachment
- **Interval timers** (SIGALRM, setitimer) for periodic jobs
- **Signal handlers** (SIGCHLD, SIGALRM) with async-signal safety
- **Job queue** for DB operations, AI training, audit rotation

#### IPC Subsystem
- **Unix domain sockets** for PostgreSQL (libpq) communication
- **Named FIFOs** for agent-to-core communication
- **Shared memory** (POSIX shm_open + mmap) for hot-path status tables
- **Message framing** with protocol headers and error codes

#### Threading Subsystem
- **Fixed thread pool** with configurable worker count
- **Session manager** tracking active client sessions
- **Mutex/Semaphore/RWLock** wrappers (PTHREAD_PROCESS_PRIVATE)
- **Connection gate** (semaphore-based limiter) preventing connection storms
- **Worker threads** handling client requests with auth + query execution

#### Security
- **No anonymous sockets**—all connections authenticated via auth tokens
- **Unix file permissions** on domain sockets (0600)
- **mlock_guard** RAII wrapper prevents sensitive data from being paged
- **Capability dropping** after initial setup

```bash
# Process topology
justiceflow (root)
  ├── justice_authd (UID 1001, Constable)
  ├── justice_authd (UID 1002, Inspector)
  ├── worker_0 → handles client requests
  ├── worker_1
  ├── hotspot_agent (read-only, analyzes crime patterns)
  ├── priority_agent (reads case queue, scores priority)
  └── workload_agent (optimizes officer assignments)
```

---

### 🤖 Layer 4: Artificial Intelligence (AI)

**Three ML agents** running as separate processes, providing explainable recommendations.

| Agent | Algorithm | Input | Output |
|-------|-----------|-------|--------|
| **Crime Hotspot Analyzer** | DBSCAN (unsupervised clustering) | FIR locations, timestamps | Heat map of top 5 zones + patrol recommendations |
| **Case Priority Recommender** | Random Forest (supervised) + SHAP | Case age, evidence count, accused history | Priority score + feature explanations |
| **Officer Workload Balancer** | Hungarian Algorithm (optimization) | Case queue, officer workload | Optimal case-to-officer assignments |

```python
# Explainable AI Output
{
  "case_id": "FIR2024-00123",
  "priority": 0.87,
  "priority_label": "HIGH",
  "reasons": [
    {"factor": "Filed 45 days ago", "weight": 0.40},
    {"factor": "Only 1 evidence item collected", "weight": 0.30},
    {"factor": "Accused has 3 prior convictions", "weight": 0.30}
  ]
}
```

**Training Data:**
- 5,000+ synthetic cases with realistic correlations
- Validated against UCI ML Repository crime datasets
- Updated hourly by scheduler

---

## 🔐 Core Application Modules

### Module 0: Audit & Compliance
Tracks every action across the system for legal compliance.

**Capabilities:**
- ✅ Immutable audit log querying (getChangeHistory, getOfficerActions)
- ✅ Suspicious activity detection (bulk deletions, unauthorized access attempts)
- ✅ Compliance reports (who changed what, when, and why)
- ✅ Forensic trail reconstruction

### Module 1: Security (Warrant, Arrest, Bail)
Manages warrant issuance, arrests, and bail with rank-based authorization.

**Features:**
- ✅ Rank-conditional warrant approval (Inspector ≥ required rank)
- ✅ Arrest recording with proper chain of custody
- ✅ Bail status tracking and appeals
- ✅ Policy engine with escalation rules
- ✅ Operation state machines (pending → approved → executed)

### Module 2: Forensic & Lab
Tracks forensic requests and lab analysis results.

**Capabilities:**
- ✅ Forensic request lifecycle (submitted → processed → results returned)
- ✅ Lab analysis tracking with timestamps
- ✅ Evidence admissibility validation
- ✅ Chain of custody enforcement
- ✅ 6 forensic operation types fully implemented

### Shared Infrastructure (Infra)
Authentication, session management, and duty caching.

**Components:**
- ✅ Token-based authentication (JWT-like)
- ✅ Session store (in-memory + DB-backed)
- ✅ Duty cache (officer rank + permissions)
- ✅ Role-based access control (RBAC)

---

## 🔗 Integration: One User Action, All Layers

Filing a **single FIR** exercises every layer:

| Step | Layer | What Happens |
|------|-------|--------------|
| **1** | **OS** | Daemon authenticates officer UID, validates Unix socket permissions |
| **2** | **Auth** | Session token validated, duty cache checked for FIR creation privilege |
| **3** | **App** | `access_control.authorize()` checks rank; `case_validation.legality()` validates inputs |
| **4** | **DBMS** | Atomic transaction: INSERT Cases, trigger fires → audit_log entry, FK validated |
| **5** | **AI** | New case triggers FIFO message to hotspot agent + priority agent for analysis |
| **6** | **Dashboard** | Officer sees FIR number, predicted crime zone, priority score, next action |

---

## 📁 Project Structure

```
justiceflow/
│
├── src/
│   ├── os_layer/                    # Layer 3: Operating System
│   │   ├── scheduler/               # Daemon, timers, job dispatch
│   │   ├── ipc/                     # Sockets, FIFOs, shared memory
│   │   ├── threading/               # Thread pool, workers, sync
│   │   ├── memory/                  # mmap, mlock utilities
│   │   ├── process/                 # Child process registry
│   │   └── os_layer.h               # Public interface
│   │
│   ├── shr_infra/                   # Shared Infrastructure
│   │   └── auth/                    # Token, session, duty cache
│   │
│   ├── security/                    # Module 1: Warrant/Arrest/Bail
│   │   ├── access_control.h         # Rank-based authorization
│   │   ├── policy_engine.h          # Chain of Responsibility handlers
│   │   └── enforcement.h            # Operation state machines
│   │
│   ├── audit/                       # Module 0: Compliance & Audit
│   │   ├── audit_log.h              # Query interface to audit table
│   │   ├── audit_query.h            # getChangeHistory, getOfficerActions
│   │   └── activity_tracker.h       # Suspicious activity detection
│   │
│   ├── forensic/                    # Module 2: Forensic & Lab
│   │   ├── forensic_request.h       # Request lifecycle + 6 operations
│   │   └── forensic_repository.h    # DB access for forensic tables
│   │
│   ├── legal/                       # Validation Rules (Shared)
│   │   ├── case_validation.h        # FIR/case legality checks
│   │   ├── evidence_rules.h         # Admissibility + soft-delete
│   │   └── compliance.h             # SOP + policy compliance
│   │
│   ├── integration/                 # Cross-Module Bridges
│   │   ├── audit_bridge.h           # Single audit entry for all subsystems
│   │   ├── s1_bridge.h              # Hooks into personnel/officer module
│   │   └── s2_bridge.h              # Hooks into FIR/evidence/cases
│   │
│   ├── common/
│   │   ├── constants.h              # Ranks, operations, states
│   │   ├── dbconfig.h               # PostgreSQL connection config
│   │   ├── ipc_types.h              # Message structures
│   │   └── logger.h                 # Unified logging
│   │
│   ├── api_gateway/                 # API Gateway (C++)
│   │   ├── api_gateway.h            # Request dispatcher
│   │   ├── command_router.h         # Routes to subsystems
│   │   └── auth_validator.h         # Token + permission checks
│   │
│   └── main.cpp                     # Daemon entry point
│
├── dashboard/                       # Layer 2.5: Flask Web UI
│   ├── app.py                       # Flask routes
│   ├── services/                    # Business logic
│   ├── templates/                   # HTML templates
│   ├── static/                      # CSS, JS
│   └── client/                      # Unix socket client to C++ backend
│
├── db/
│   ├── schema/                      # Table definitions + constraints
│   ├── triggers/                    # Audit triggers + integrity
│   ├── procedures/                  # Stored procedures
│   ├── views/                       # Role-restricted views
│   ├── seeds/                       # Sample data
│   └── tests/                       # SQL test scripts
│
├── ai_agents/                       # Layer 4: ML Agents (Python)
│   ├── hotspot_agent.py             # Crime hotspot detection
│   ├── priority_agent.py            # Case priority recommendation
│   └── workload_agent.py            # Officer workload balancing
│
├── tests/
│   ├── unit/                        # Unit tests (C++)
│   ├── integration/                 # Integration tests (C++ ↔ DB)
│   └── e2e/                         # End-to-end (dashboard ↔ backend)
│
├── docs/
│   ├── uml/                         # UML diagrams (all 5 types)
│   ├── architecture/                # Architecture docs
│   ├── api/                         # API documentation
│   └── database/                    # Schema docs
│
├── justiceflow_setup.sh             # One-command setup
├── folder_structure.txt             # Detailed file tree
├── MakeFile                         # Build configuration
└── README.md                        # This file

```

---

## 🛠️ Tech Stack

| Layer | Technology |
|-------|------------|
| **Application** | C++ 17, no STL (custom containers) |
| **Database** | PostgreSQL 14+, PL/pgSQL |
| **API Gateway** | C++ (Unix sockets, libpq) |
| **Dashboard** | Flask, WebSocket, Bootstrap 5 |
| **AI / ML** | Python, scikit-learn, NumPy, SHAP, DBSCAN |
| **OS / IPC** | Linux, POSIX APIs, pthreads, semaphores, shared memory |
| **Build** | GNU Make, CMake |
| **Testing** | Google Test, SQLite (fixtures) |
| **Documentation** | Doxygen, Draw.io, UML |
| **Version Control** | Git, GitHub |

---

## 🚀 Quick Start

### Prerequisites
- Linux (Ubuntu 20.04+ or Debian 11+)
- PostgreSQL 14+
- GCC 9+ or Clang 10+
- Python 3.8+
- libpq-dev, build-essential, git

### Installation

```bash
# 1. Clone repository
git clone https://github.com/Furqan-2006/JusticeFlow.git
cd JusticeFlow

# 2. Run automated setup (installs dependencies, creates DB)
chmod +x justiceflow_setup.sh
sudo ./justiceflow_setup.sh

# 3. Initialize database schema
psql -U justiceflow -d justiceflow -f db/schema/init.sql
psql -U justiceflow -d justiceflow -f db/seeds/sample_data.sql

# 4. Build C++ application
cd src
make clean && make

# 5. Train AI models
cd ../ai_agents
python3 hotspot_agent.py --train
python3 priority_agent.py --train

# 6. Start OS daemon (requires root for socket permissions)
sudo ./bin/justiceflow &

# 7. Launch Flask dashboard (separate terminal)
cd dashboard
python3 run.py

# 8. Open browser
# Navigate to http://localhost:5000
```

### Verify Installation

```bash
# Check daemon is running
ps aux | grep justiceflow

# Test database connection
psql -U justiceflow -d justiceflow -c "SELECT COUNT(*) FROM Cases;"

# Test Unix socket communication
nc -U /tmp/justiceflow.sock

# View logs
tail -f logs/justiceflow.log
```

---

## 📊 Success Metrics

| Course | Metric | Target | Status |
|--------|--------|--------|--------|
| **DBMS** | Audit trail completeness | 100% of critical changes logged | ✅ |
| **DBMS** | Referential integrity | 0 orphaned records | ✅ |
| **SDA** | Design patterns implemented | ≥ 5 patterns, all justified | ✅ 5/5 |
| **SDA** | Code complexity | < 10 cyclomatic per function | ✅ |
| **AI** | Hotspot detection precision | > 70% | 🔄 In training |
| **AI** | Priority model accuracy | > 75% | 🔄 In training |
| **OS** | Race condition prevention | 0 corruption in 50-thread test | ✅ |
| **OS** | Access control | 100% unauthorized attempts logged & denied | ✅ |

---

## 👥 Team & Responsibilities

| Member | Primary | Secondary |
|--------|---------|-----------|
| **Furqan** | OS (Scheduler), API Gateway | SDA (Design patterns), Integration |
| **Abdullah** | OS (IPC), Database Layer | Data plane optimization |
| **Abu Bakar** | OS (Threading), Process management | System testing, stability |

---

## 📖 Documentation

### Key Documents
- **[OS Layer README](src/os_layer/README.md)** — Subsystem overview, architecture, thread safety
- **[Database Schema](db/schema/)** — Table definitions, relationships, constraints
- **[UML Diagrams](docs/uml/)** — All 5 diagram types for complete system design
- **[API Documentation](docs/api/)** — Command reference, request/response formats
- **[Architecture](docs/architecture/)** — System design rationale, data flow

---

## 🔐 Security Features

✅ **Authentication:** Token-based, Unix socket authentication
✅ **Authorization:** Role-based access control (RBAC) via rank
✅ **Encryption:** TLS on PostgreSQL connections (future)
✅ **Audit:** Immutable, tamper-proof audit log with triggers
✅ **Memory Safety:** mlock_guard prevents sensitive data in swap
✅ **IPC Security:** Unix domain sockets with 0600 permissions
✅ **Privilege Escalation:** Chain of Responsibility prevents unauthorized rank elevation

---

## 🧪 Testing

```bash
# Run unit tests
cd tests/unit
./run_tests.sh

# Run integration tests (C++ ↔ DB)
cd ../integration
./run_integration_tests.sh

# Run end-to-end tests
cd ../e2e
./run_e2e_tests.sh

# Run database tests
cd ../../db/tests
./run_db_tests.sh
```

---

## 🐛 Known Limitations & Future Work

### Current
- 🟡 AI agents run in polling mode (will upgrade to event-driven)
- 🟡 Dashboard lacks real-time WebSocket updates (in progress)
- 🟡 Warrant approval workflow is manual (workflow engine planned)
- 🟡 No encrypted audit log backups yet

### Planned
- ✨ Workflow engine for case progression
- ✨ Mobile app for field officers
- ✨ Advanced forensic analytics dashboard
- ✨ Multi-precinct federation
- ✨ Encrypted audit log with blockchain verification

---

## 📝 License & Academic Use

This project is developed for **academic purposes** as part of a multi-course integration project covering **DBMS, SDA, AI, and OS**.

**Not for production use without legal review.**

---

## 📞 Support & Contribution

For questions or contributions:
1. Check existing [Issues](https://github.com/Furqan-2006/JusticeFlow/issues)
2. Review [Discussions](https://github.com/Furqan-2006/JusticeFlow/discussions)
3. Submit a Pull Request with detailed description

---

<div align="center">

### 🏛️ Built with ⚖️ for DBMS · SDA · AI · OS

*Making legal software engineering education practical, integrated, and real.*

**Last Updated:** April 2026

</div>
