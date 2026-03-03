<div align="center">

# ⚖️ JusticeFlow

### Police Case & Evidence Management System

*A multi-course integrated project demonstrating legal-grade software engineering*

![PostgreSQL](https://img.shields.io/badge/PostgreSQL-316192?style=for-the-badge&logo=postgresql&logoColor=white)
![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![Python](https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)

![DBMS](https://img.shields.io/badge/Course-DBMS-blue?style=flat-square)
![SDA](https://img.shields.io/badge/Course-SDA-green?style=flat-square)
![AI](https://img.shields.io/badge/Course-AI-orange?style=flat-square)
![OS](https://img.shields.io/badge/Course-OS-red?style=flat-square)

</div>

---

## 📋 Overview

**JusticeFlow** manages the complete investigation lifecycle — from FIR registration through evidence collection to case closure — enforcing legal-grade compliance at every step. It is architected so that each of four CS disciplines solves a **real, distinct, non-overlapping problem** that naturally arises in mission-critical legal software.

```
Remove any one layer → the system loses critical, non-replaceable functionality.
That is the test of genuine integration.
```

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────┐
│          Application Layer  (SDA)           │
│   CaseManager   EvidenceMgr   OfficerMgr    │
│   (Strategy)    (Observer)    (Factory)     │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│           Database Layer  (DBMS)            │
│   Cases(Triggers) · Evidence(SoftDel)       │
│   Officers(Views) · AuditLog(Immutable)     │
└──────────────────────┬──────────────────────┘
                       │
         ┌─────────────┴──────────────┐
         ▼                            ▼
┌────────────────┐         ┌──────────────────────┐
│   AI Layer     │         │     OS Layer          │
│ · Hotspot Det. │         │ · Privilege Daemon    │
│ · Priority Rec.│         │ · File Integrity Mon. │
│ · Workload Bal.│         │ · Job Scheduler       │
└────────────────┘         └──────────────────────┘
```

---

## 📚 Course Modules

### 🗄️ DBMS — Data Integrity & Audit Foundation

Enforces legal-grade correctness through a fully normalized schema (3NF), ACID-compliant transactions, and automated auditing.

| Feature | Description |
|---|---|
| Audit Triggers | Every `INSERT` / `UPDATE` / `DELETE` on critical tables auto-logs to `Audit_Log` with old & new values |
| Soft Delete | Evidence is **never** physically removed — `is_deleted` flag preserves chain of custody |
| Stored Procedures | `assign_officer_to_case` validates rank, case status, and workload atomically |
| Security Views | `constable_cases` exposes only rows assigned to the calling officer |
| Indexing | Composite index on `Cases(status, filed_date)` with before/after benchmark scripts |

```sql
-- Every case change is permanently recorded
CREATE TRIGGER audit_case_changes
AFTER UPDATE ON Cases
FOR EACH ROW
INSERT INTO Audit_Log(table_name, record_id, action, old_value, new_value, changed_by, timestamp)
VALUES ('Cases', OLD.case_id, 'UPDATE', row_to_json(OLD), row_to_json(NEW), current_user, NOW());
```

---

### 🧩 SDA — Architecture & Design Patterns

Full UML suite backed by five justified design patterns in C++.

| Pattern | Problem Solved |
|---|---|
| **Strategy** | Rank-conditional case status transitions (Inspector vs. SHO privileges) |
| **Chain of Responsibility** | Layered authorization: Constable → Inspector → SHO → Commissioner |
| **Observer** | Case updates simultaneously notify `AuditLogger`, `NotificationService`, `AnalyticsPipeline` |
| **Factory** | Unified report generation (daily summary, case history, chain-of-custody) |
| **Singleton** | Database connection pool — single shared resource, controlled access |

**UML Deliverables:** Use Case · Class (with inheritance hierarchy) · Sequence · State (case lifecycle) · Activity diagrams

```cpp
// Open/Closed Principle in action — adding a new rank requires ZERO changes here
class AuthorizationHandler {
    AuthorizationHandler* next;
public:
    virtual bool authorize(Officer* o, Action a) {
        if (canHandle(a)) return checkPermission(o, a);
        return next ? next->authorize(o, a) : false;
    }
};
```

---

### 🤖 AI — Intelligent Decision Support

Three ML agents run as **separate read-only processes** against the database. AI supports — never replaces — human authority. All predictions are explainable.

| Agent | Algorithm | Output |
|---|---|---|
| **Crime Hotspot Analyzer** | DBSCAN (unsupervised) | Heat map of top 5 crime zones with patrol recommendations |
| **Case Priority Recommender** | Random Forest (supervised) | Ranked case queue with SHAP-based feature explanations |
| **Officer Workload Balancer** | Hungarian Algorithm (optimization) | Optimal case-to-officer assignments minimizing workload variance |

```python
# Explainable priority scoring
"Case #FIR2024-00123 — Priority: 0.87 (High)
 Reasons:
   - Filed 45 days ago (overdue)         weight: 0.40
   - Only 1 evidence item collected      weight: 0.30
   - Accused has 3 prior convictions     weight: 0.30"
```

> **Training Data:** 5,000+ synthetic cases with realistic correlations, validated against UCI ML Repository crime datasets.

---

### 🖥️ OS — Process Isolation, Concurrency & System Auditing

Builds system-level infrastructure that all other layers depend on.

| Component | OS Concepts |
|---|---|
| **`justice_authd` Daemon** | `seteuid` / `setegid`, Unix domain sockets, `fork` / `setsid`, capability dropping |
| **File Integrity Monitor** | `inotify_add_watch`, `IN_MODIFY` / `IN_DELETE` events, automatic rollback from backup |
| **Concurrent Case Access** | Named POSIX semaphores (`sem_open`), shared memory (`shm_open` / `mmap`), atomic ops |
| **Job Scheduler** | `SIGALRM`, `fork` / `exec`, `wait` — runs audit rotation (hourly), ML retraining (daily) |
| **System Monitor** | `/proc/stat` · `/proc/meminfo` · live CPU, memory, and process health dashboard |

```bash
# Privilege daemon enforces rank at the process level
[10:23:15] UID 1001 (Constable Raj)  DENIED  → attempt to close case #123
[10:25:03] UID 1002 (SHO Sharma)     GRANTED → case closure #123
```

---

## 🔗 Integration: One Action, All Four Layers

Filing a single FIR exercises every layer simultaneously:

| Step | Layer | What Happens |
|---|---|---|
| 1 | **OS** | `justice_authd` authenticates officer UID and enforces rank |
| 2 | **SDA** | `CaseFactory` creates the case object; `AuthorizationChain` validates rights |
| 3 | **DBMS** | Atomic transaction: `INSERT` into `Cases`, audit trigger fires, FK validated |
| 4 | **AI** | New case triggers hotspot re-analysis and priority scoring |
| 5 | **Dashboard** | Officer sees FIR number, crime zone classification, and priority score |

---

## ✅ Success Metrics

| Course | Metric | Target |
|---|---|---|
| DBMS | Audit trail completeness | 100% of critical changes logged |
| DBMS | Referential integrity | 0 orphaned records in any test |
| SDA | Design patterns | Minimum 5, all justified |
| SDA | Cyclomatic complexity | < 10 per function |
| AI | Hotspot detection precision | > 70% |
| AI | Priority model accuracy | > 75% |
| OS | Race condition prevention | 0 data corruption in 50-process test |
| OS | Unauthorized access blocking | 100% of attempts denied and logged |

---

## 🚀 Quick Start

```bash
# 1. Clone the repository
git clone https://github.com/<your-username>/justiceflow.git
cd justiceflow

# 2. Run environment setup (installs all dependencies)
chmod +x justiceflow_setup.sh
sudo ./justiceflow_setup.sh

# 3. Initialize the database
psql -U justiceflow -d justiceflow -f db/schema/init.sql
psql -U justiceflow -d justiceflow -f db/seeds/sample_data.sql

# 4. Build the C++ application
cd src && make

# 5. Generate synthetic AI training data
python3 ai/data/generate_synthetic.py --count 5000

# 6. Train ML models
python3 ai/agents/hotspot.py --train
python3 ai/agents/priority.py --train

# 7. Start the OS daemon (requires root)
sudo ./bin/justice_authd &

# 8. Launch JusticeFlow
./bin/justiceflow
```

---

## 🛠️ Tech Stack

| Layer | Technology |
|---|---|
| Application | C++ (no STL except utilities) |
| Database | PostgreSQL |
| AI / ML | Python · scikit-learn · NumPy · SHAP |
| OS / IPC | Linux · POSIX APIs · Unix domain sockets · Named semaphores · Shared memory |
| Build | GNU Make |
| Docs | Doxygen · Draw.io / Lucidchart |
| Version Control | Git |

---

## 👥 Team

| Member | Primary | Secondary |
|---|---|---|
| Member 1 | DBMS — Schema, Triggers, Procedures | SDA — Data layer classes |
| Member 2 | SDA — Design Patterns, UML | OS — Application integration |
| Member 3 | AI — ML Models & Pipelines | OS — System daemons |

---

## 📁 Project Structure

```
justiceflow/
├── src/
│   ├── core/           # Base classes (Officer, Case, Evidence)
│   ├── patterns/       # Design pattern implementations
│   └── os_layer/       # Daemon, monitor, scheduler
├── db/
│   ├── schema/         # Table definitions
│   ├── triggers/       # Audit & integrity triggers
│   ├── procedures/     # Stored procedures
│   ├── views/          # Role-restricted views
│   ├── seeds/          # Sample data
│   └── tests/          # SQL test scripts
├── ai/
│   ├── agents/         # Hotspot, Priority, Workload agents
│   ├── models/         # Saved model files
│   └── data/           # Synthetic data generator
├── docs/
│   ├── uml/            # All UML diagrams
│   └── api/            # API documentation
├── scripts/            # Utility scripts
├── logs/               # Application & daemon logs
└── justiceflow_setup.sh
```

---

## 📄 License

This project is developed for academic purposes as part of a multi-course integration project.

---

<div align="center">
<sub>Built with ⚖️ for DBMS · SDA · AI · OS</sub>
</div>
=======
# JusticeFlow
A legal-grade Police Case &amp; Evidence Management System integrating DBMS (audit triggers, ACID transactions), SDA (5 design patterns, UML), AI (crime hotspot detection, case prioritization), and OS (privilege daemon, file integrity monitor, process synchronization) into one cohesive system.
>>>>>>> ae86abeaa2cea43c8081f9148ac19c8d75a692dc
