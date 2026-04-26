"""
workload_agent.py — Module 4: Officer Workload Balancer
========================================================
JusticeFlow AI Agent — Hungarian Algorithm (Combinatorial Optimization)

Spawned by process_spawner via fork+exec every 6 hours.
Connects to PostgreSQL as the justice_ai role:
  - SELECT on public schema (Cases, Officers, Case_Officers)
  - INSERT on analytics schema (Officer_Workload_Assignments, Model_Performance_Log)
  - NO writes to any operational table, ever.

Credentials are injected by process_spawner as environment variables
before exec() — this script never reads a config file.

Internal class structure (Architecture Markdown §Module 4):
  DBRepository       — all psycopg2 SQL (Strategy: Repository)
  CostMatrixBuilder  — 4-component weighted cost matrix (Strategy pattern)
  HungarianSolver    — thin scipy wrapper (isolates solver dependency)
  run_analysis()     — 7-stage Pipeline orchestrator

FIFO Reporting Protocol (§FIFO Reporting Protocol):
  Startup, post-write, and error messages written to named FIFO.
  Scheduler reads non-blocking — a missed write is not fatal.

Shared Memory Write (§Shared Memory Write):
  agents[2] slot updated after write_suggestions() completes.
  Index 2 = workload agent, per ipc_types.h convention.

Error Handling (§Error Handling):
  No exception bubbles out of run_analysis().
  On exception: log stderr → FIFO error msg → shm error code → exit(1).

Author : Muhammad Furqan Sheikh (24K-0527)
Course : Artificial Intelligence — JusticeFlow AI Layer
Date   : 2026
"""

from __future__ import annotations

import ctypes
import errno
import logging
import math
import mmap
import os
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

import numpy as np
import pandas as pd
import psycopg2
import psycopg2.extras
from scipy.optimize import linear_sum_assignment

# ============================================================================
# Logging — stderr only. process_spawner captures stderr for the scheduler.
# ============================================================================
logging.basicConfig(
    stream=sys.stderr,
    level=logging.INFO,
    format="%(asctime)s [workload_agent] %(levelname)s %(message)s",
    datefmt="%Y-%m-%dT%H:%M:%S",
)
log = logging.getLogger("workload_agent")


# ============================================================================
# Constants — weights defined here, never inline (Architecture requirement)
# ============================================================================
WORKLOAD_WEIGHT:   float = 0.35
SKILL_WEIGHT:      float = 0.30
RANK_WEIGHT:       float = 0.20
GEOGRAPHIC_WEIGHT: float = 0.15

# Rank hierarchy: integer value used for >= comparisons
RANK_ORDER: dict[str, int] = {
    "CONSTABLE":    1,
    "HEAD_CONSTABLE": 2,
    "ASI":          3,
    "SI":           4,
    "INSPECTOR":    5,
    "DSP":          6,
    "SP":           7,
    "SSP":          8,
    "DIG":          9,
    "ADDL_IG":     10,
    "IGP":         11,
}

# Minimum rank required per case severity
SEVERITY_MIN_RANK: dict[str, str] = {
    "CRITICAL": "DSP",
    "HIGH":     "INSPECTOR",
    "MEDIUM":   "SI",
    "LOW":      "CONSTABLE",
}

# Skill map: case crime_type → officer qualification keyword
# Partial match = 0.5, exact = 0.0, no match = 1.0
SKILL_MAP: dict[str, list[str]] = {
    "MURDER":            ["HOMICIDE", "CRIMINAL_INVESTIGATION"],
    "KIDNAPPING":        ["CRIMINAL_INVESTIGATION", "SPECIAL_BRANCH"],
    "DRUG_TRAFFICKING":  ["ANTI_NARCOTICS", "CRIMINAL_INVESTIGATION"],
    "TERRORISM":         ["COUNTER_TERRORISM", "SPECIAL_BRANCH"],
    "CYBERCRIME":        ["CYBER", "DIGITAL_FORENSICS"],
    "FRAUD":             ["FINANCIAL_CRIMES", "ANTI_CORRUPTION"],
    "ROBBERY":           ["CRIMINAL_INVESTIGATION"],
    "ARMED_ROBBERY":     ["CRIMINAL_INVESTIGATION", "SPECIAL_BRANCH"],
    "ASSAULT":           ["CRIMINAL_INVESTIGATION"],
    "AGGRAVATED_ASSAULT":["CRIMINAL_INVESTIGATION"],
    "BURGLARY":          ["CRIMINAL_INVESTIGATION"],
    "GANG_ACTIVITY":     ["CRIMINAL_INVESTIGATION", "COUNTER_TERRORISM"],
    "HUMAN_TRAFFICKING": ["SPECIAL_BRANCH", "CRIMINAL_INVESTIGATION"],
    "BRIBERY":           ["ANTI_CORRUPTION"],
    "FORGERY":           ["FINANCIAL_CRIMES"],
}

# FIFO path (matches ipc_types.h FIFO naming convention)
FIFO_PATH: str = "/tmp/justiceflow_workload.fifo"

# Shared memory name (matches ipc_types.h SHM_NAME)
SHM_NAME: str = "/justiceflow_shm"

# Agent index in SharedStatusTable (ipc_types.h convention: 2 = workload)
AGENT_INDEX: int = 2

# Maximum cases a single officer can hold
MAX_OFFICER_CASES: int = 10

# Suggestion lifetime in seconds (6 hours)
SUGGESTION_TTL_SECONDS: int = 6 * 3600


# ============================================================================
# Data classes — typed containers for DB rows
# No business logic here — pure data.
# ============================================================================
@dataclass
class CaseRow:
    case_id:         int
    case_type:       str
    case_status:     str
    severity:        str          # CRITICAL | HIGH | MEDIUM | LOW
    station_id:      int
    incident_lat:    float
    incident_lon:    float
    filed_at:        float        # epoch


@dataclass
class OfficerRow:
    officer_id:        int
    belt_number:       str
    current_rank:      str
    qualification:     str        # comma-separated skill keywords
    station_id:        int
    active_case_count: int        # accurate count from Case_Officers join


# ============================================================================
# AgentStatusMessage — mirrors ipc_types.h AgentStatusMessage struct
# Packed format: 32s i i i i 256s  (agent_name, error_code, predictions,
#                                   run_time_ms, is_running, error_detail)
# Must exactly match the C struct layout so the scheduler can read it.
# ============================================================================
_FIFO_MSG_FORMAT = "=32siii i256s"
_FIFO_MSG_SIZE   = struct.calcsize(_FIFO_MSG_FORMAT)


def _build_fifo_msg(agent_name: str,
                    error_code: int,
                    predictions: int,
                    run_time_ms: int,
                    is_running: int,
                    error_detail: str) -> bytes:
    return struct.pack(
        _FIFO_MSG_FORMAT,
        agent_name.encode("ascii")[:32].ljust(32, b"\x00"),
        error_code,
        predictions,
        run_time_ms,
        is_running,
        error_detail.encode("ascii")[:256].ljust(256, b"\x00"),
    )


def _write_fifo(msg: bytes) -> None:
    """Non-blocking write to the named FIFO. Failure is non-fatal."""
    try:
        if not os.path.exists(FIFO_PATH):
            return
        fd = os.open(FIFO_PATH, os.O_WRONLY | os.O_NONBLOCK)
        try:
            os.write(fd, msg)
        finally:
            os.close(fd)
    except OSError:
        pass  # Scheduler may not be reading — that is acceptable


# ============================================================================
# Shared Memory Layout — mirrors SharedStatusTable in shm_layout.h
#
# struct AgentSlot {
#     int64_t last_run_at;          // 8 bytes
#     int64_t next_run_at;          // 8 bytes
#     int32_t predictions_generated;// 4 bytes
#     float   model_accuracy;       // 4 bytes
#     int32_t is_running;           // 4 bytes
#     int32_t last_error_code;      // 4 bytes
# };  // total = 32 bytes per slot
#
# SharedStatusTable: AgentSlot agents[3];  // 3 agents × 32 bytes = 96 bytes
# ============================================================================
_SLOT_FORMAT = "=qqifi i"   # last_run_at, next_run_at, predictions, accuracy,
                             # is_running, last_error_code
_SLOT_SIZE   = struct.calcsize(_SLOT_FORMAT)
_SHM_OFFSET  = AGENT_INDEX * _SLOT_SIZE     # byte offset into SharedStatusTable


def _update_shared_memory(predictions: int,
                           model_accuracy: float,
                           is_running: bool,
                           last_error_code: int) -> None:
    """
    Updates the workload agent's slot in the shared memory segment.
    If the segment doesn't exist (OS layer not running), skip silently.
    """
    try:
        import ctypes
        import ctypes.util
        libc_name = ctypes.util.find_library("c")
        libc = ctypes.CDLL(libc_name, use_errno=True)

        # shm_open(name, O_RDWR, 0)
        O_RDWR = os.O_RDWR
        fd = libc.shm_open(SHM_NAME.encode(), O_RDWR, 0)
        if fd < 0:
            log.debug("Shared memory segment not available — skipping shm write")
            return

        now = int(time.time())
        slot_data = struct.pack(
            _SLOT_FORMAT,
            now,                                        # last_run_at
            now + SUGGESTION_TTL_SECONDS,               # next_run_at
            predictions,                                # predictions_generated
            float(model_accuracy),                      # model_accuracy
            1 if is_running else 0,                     # is_running
            last_error_code,                            # last_error_code
        )

        # mmap and write at the correct slot offset
        total_size = _SLOT_SIZE * 3  # 3 agent slots
        with mmap.mmap(fd, total_size, access=mmap.ACCESS_WRITE) as mm:
            mm.seek(_SHM_OFFSET)
            mm.write(slot_data)

        os.close(fd)

    except Exception as exc:
        log.debug("shm write skipped: %s", exc)


# ============================================================================
# DBRepository
# All psycopg2 SQL lives here. CostMatrixBuilder and HungarianSolver never
# write a query. Algorithm classes receive plain Python dicts / dataclasses.
# ============================================================================
class DBRepository:
    """
    Repository pattern — single class owning all database interaction.

    Connection is created once per agent run and reused for all operations.
    Credentials are read from environment variables injected by process_spawner:
        PGHOST, PGPORT, PGDATABASE, PGUSER (justice_ai), PGPASSWORD
    """

    def __init__(self) -> None:
        self._conn: Optional[psycopg2.extensions.connection] = None

    def connect(self) -> None:
        """
        Opens the psycopg2 connection using environment variables.
        Raises psycopg2.OperationalError on failure — caller handles it.
        """
        self._conn = psycopg2.connect(
            host=os.environ["PGHOST"],
            port=int(os.environ.get("PGPORT", 5432)),
            dbname=os.environ["PGDATABASE"],
            user=os.environ["PGUSER"],       # must be justice_ai
            password=os.environ["PGPASSWORD"],
            connect_timeout=10,
            application_name="workload_agent",
        )
        self._conn.set_session(readonly=False, autocommit=False)
        log.info("DB connection established as role: %s", os.environ.get("PGUSER"))

    def close(self) -> None:
        if self._conn and not self._conn.closed:
            self._conn.close()

    def _cursor(self):
        """Returns a DictCursor. Reconnects if connection was lost."""
        if self._conn is None or self._conn.closed:
            self.connect()
        return self._conn.cursor(cursor_factory=psycopg2.extras.DictCursor)

    # ----------------------------------------------------------------
    # fetch_unassigned_cases
    # Returns cases that have no assigned lead officer OR have status
    # REGISTERED / UNDER_INVESTIGATION and need officer balancing.
    # Includes incident_lat/lon (guaranteed populated by S2 contract).
    # ----------------------------------------------------------------
    def fetch_unassigned_cases(self) -> list[CaseRow]:
        """
        SELECT cases that are active and either have no lead officer or
        are flagged for rebalancing.

        Returns list of CaseRow. Empty list means no work to assign.
        """
        sql = """
            SELECT
                c.case_id,
                c.case_type,
                c.case_status,
                COALESCE(c.severity, 'MEDIUM')  AS severity,
                c.station_id,
                COALESCE(c.incident_lat, 0.0)   AS incident_lat,
                COALESCE(c.incident_lon, 0.0)   AS incident_lon,
                EXTRACT(EPOCH FROM c.filed_at)::bigint AS filed_at
            FROM public.Cases c
            WHERE c.case_status IN ('REGISTERED', 'UNDER_INVESTIGATION')
              AND (
                  c.lead_officer_id IS NULL
                  OR NOT EXISTS (
                      SELECT 1 FROM public.Case_Officers co
                      WHERE co.case_id = c.case_id
                  )
              )
            ORDER BY c.filed_at ASC;
        """
        with self._cursor() as cur:
            cur.execute(sql)
            rows = cur.fetchall()

        return [
            CaseRow(
                case_id=r["case_id"],
                case_type=r["case_type"] or "UNKNOWN",
                case_status=r["case_status"],
                severity=r["severity"],
                station_id=r["station_id"],
                incident_lat=float(r["incident_lat"]),
                incident_lon=float(r["incident_lon"]),
                filed_at=float(r["filed_at"]),
            )
            for r in rows
        ]

    # ----------------------------------------------------------------
    # fetch_available_officers
    # Officers who are ACTIVE and below the 10-case ceiling.
    # active_case_count comes from the Case_Officers join (accurate count),
    # NOT from the denormalised column on Officers (which may be stale).
    # ----------------------------------------------------------------
    def fetch_available_officers(self) -> list[OfficerRow]:
        """
        SELECT active officers with capacity for new cases.
        Uses a subquery COUNT on Case_Officers for accurate workload,
        as guaranteed by the S1 integration contract.
        """
        sql = """
            SELECT
                o.officer_id,
                o.belt_number,
                o.current_rank,
                COALESCE(o.qualification, '')           AS qualification,
                o.station_id,
                COALESCE(co_count.active_count, 0)      AS active_case_count
            FROM public.Officers o
            LEFT JOIN (
                SELECT
                    co.officer_id,
                    COUNT(*) AS active_count
                FROM public.Case_Officers co
                JOIN public.Cases c ON co.case_id = c.case_id
                WHERE c.case_status IN ('REGISTERED', 'UNDER_INVESTIGATION')
                GROUP BY co.officer_id
            ) co_count ON o.officer_id = co_count.officer_id
            WHERE o.status = 'ACTIVE'
              AND COALESCE(co_count.active_count, 0) < %(max_cases)s
            ORDER BY COALESCE(co_count.active_count, 0) ASC;
        """
        with self._cursor() as cur:
            cur.execute(sql, {"max_cases": MAX_OFFICER_CASES})
            rows = cur.fetchall()

        return [
            OfficerRow(
                officer_id=r["officer_id"],
                belt_number=r["belt_number"] or "",
                current_rank=r["current_rank"] or "CONSTABLE",
                qualification=r["qualification"],
                station_id=r["station_id"],
                active_case_count=int(r["active_case_count"]),
            )
            for r in rows
        ]

    # ----------------------------------------------------------------
    # expire_old_suggestions
    # Marks stale SUGGESTED assignments as AUTO_EXPIRED before inserting
    # new ones. Called as Stage 1 of every run.
    # ----------------------------------------------------------------
    def expire_old_suggestions(self) -> int:
        """
        UPDATE analytics.Officer_Workload_Assignments
        SET assignment_status = 'AUTO_EXPIRED'
        WHERE assignment_status = 'SUGGESTED' AND expires_at < NOW()

        Returns number of rows expired.
        """
        sql = """
            UPDATE analytics.Officer_Workload_Assignments
            SET    assignment_status = 'AUTO_EXPIRED',
                   updated_at        = NOW()
            WHERE  assignment_status = 'SUGGESTED'
              AND  expires_at < NOW();
        """
        with self._cursor() as cur:
            cur.execute(sql)
            expired_count = cur.rowcount
        self._conn.commit()
        log.info("Expired %d stale suggestions", expired_count)
        return expired_count

    # ----------------------------------------------------------------
    # write_suggestions
    # Bulk INSERT assignment suggestions into analytics schema.
    # assignment_status = 'SUGGESTED', expires_at = NOW() + 6 hours.
    # Uses executemany for efficiency on large batches.
    # ----------------------------------------------------------------
    def write_suggestions(self,
                           assignments: list[dict],
                           cases: list[CaseRow],
                           officers: list[OfficerRow]) -> int:
        """
        Bulk INSERT into analytics.Officer_Workload_Assignments.

        assignments: list of {'case_index': int, 'officer_index': int,
                               'cost': float}
        cases:       parallel list of CaseRow (indexed by case_index)
        officers:    parallel list of OfficerRow (indexed by officer_index)

        Returns number of rows inserted.
        """
        if not assignments:
            log.warning("write_suggestions called with empty assignments — nothing to write")
            return 0

        sql = """
            INSERT INTO analytics.Officer_Workload_Assignments
                (case_id, officer_id, assignment_status, cost_score,
                 suggested_at, expires_at, created_at, updated_at)
            VALUES
                (%(case_id)s, %(officer_id)s, 'SUGGESTED', %(cost_score)s,
                 NOW(), NOW() + INTERVAL '6 hours', NOW(), NOW())
            ON CONFLICT (case_id) DO UPDATE
                SET officer_id        = EXCLUDED.officer_id,
                    assignment_status = 'SUGGESTED',
                    cost_score        = EXCLUDED.cost_score,
                    suggested_at      = NOW(),
                    expires_at        = NOW() + INTERVAL '6 hours',
                    updated_at        = NOW();
        """
        records = []
        for a in assignments:
            ci = a["case_index"]
            oi = a["officer_index"]
            records.append({
                "case_id":    cases[ci].case_id,
                "officer_id": officers[oi].officer_id,
                "cost_score": float(a["cost"]),
            })

        with self._cursor() as cur:
            psycopg2.extras.execute_batch(cur, sql, records, page_size=100)
        self._conn.commit()

        log.info("Wrote %d assignment suggestions to analytics schema", len(records))
        return len(records)

    # ----------------------------------------------------------------
    # log_performance
    # Records run metadata into analytics.Model_Performance_Log.
    # The "accuracy" concept for Hungarian is run completion (1.0 = ran OK).
    # A more meaningful metric — SHO acceptance rate — would require reading
    # back accepted/rejected rows from a previous run, which is done here.
    # ----------------------------------------------------------------
    def log_performance(self,
                         run_time_ms: int,
                         case_count: int,
                         officer_count: int,
                         assignments_made: int) -> None:
        """
        INSERT into analytics.Model_Performance_Log.

        acceptance_rate: fraction of previous SUGGESTED assignments that were
        accepted (status = 'ACCEPTED') before this run expired them.
        This is the real feedback metric — not model_accuracy=1.0.
        """
        # Read acceptance rate from previous run before it was expired
        acceptance_sql = """
            SELECT
                COUNT(*) FILTER (WHERE assignment_status = 'ACCEPTED') AS accepted,
                COUNT(*) FILTER (WHERE assignment_status = 'AUTO_EXPIRED') AS expired
            FROM analytics.Officer_Workload_Assignments
            WHERE updated_at >= NOW() - INTERVAL '7 hours';
        """
        accepted  = 0
        expired_n = 0
        try:
            with self._cursor() as cur:
                cur.execute(acceptance_sql)
                row = cur.fetchone()
                if row:
                    accepted  = int(row["accepted"]  or 0)
                    expired_n = int(row["expired"]    or 0)
        except Exception:
            pass  # Non-critical — acceptance rate defaults to 0

        total_prev = accepted + expired_n
        acceptance_rate = (accepted / total_prev) if total_prev > 0 else None

        insert_sql = """
            INSERT INTO analytics.Model_Performance_Log
                (agent_name, run_at, run_time_ms, input_cases,
                 input_officers, assignments_made,
                 acceptance_rate, notes, created_at)
            VALUES
                ('workload_agent', NOW(), %(run_time_ms)s, %(case_count)s,
                 %(officer_count)s, %(assignments_made)s,
                 %(acceptance_rate)s, %(notes)s, NOW());
        """
        notes = (
            f"Hungarian Algorithm. Matrix: {case_count}×{officer_count}. "
            f"Assignments: {assignments_made}. "
            f"Acceptance rate from previous run: "
            f"{acceptance_rate:.1%}" if acceptance_rate is not None
            else "N/A (first run or no previous data)"
        )

        with self._cursor() as cur:
            cur.execute(insert_sql, {
                "run_time_ms":      run_time_ms,
                "case_count":       case_count,
                "officer_count":    officer_count,
                "assignments_made": assignments_made,
                "acceptance_rate":  acceptance_rate,
                "notes":            notes if isinstance(notes, str) else str(notes),
            })
        self._conn.commit()
        log.info("Performance log written. Acceptance rate: %s", acceptance_rate)


# ============================================================================
# CostMatrixBuilder
# Strategy pattern — each cost component is an independent method.
# Weights are module-level constants, never hardcoded inline.
# build() assembles the N×M cost matrix (N=cases, M=officers).
# ============================================================================
class CostMatrixBuilder:
    """
    Constructs the cost matrix for the Hungarian Algorithm.

    cost[i][j] = sum of four weighted components for case_i × officer_j:
        workload_penalty   × WORKLOAD_WEIGHT   (0.35)
        skill_mismatch     × SKILL_WEIGHT      (0.30)
        rank_suitability   × RANK_WEIGHT       (0.20)
        geographic_distance× GEOGRAPHIC_WEIGHT (0.15)

    All components produce values in [0.0, 1.0].
    Final cost is in [0.0, 1.0]. Lower = better assignment.
    scipy.optimize.linear_sum_assignment minimises the total cost.
    """

    @staticmethod
    def workload_penalty(officer: OfficerRow) -> float:
        """
        Officers near the 10-case limit get exponentially higher penalty.
        Formula: (active_case_count / MAX) ** 2

        At 0 cases  → 0.00 (no penalty)
        At 5 cases  → 0.25
        At 9 cases  → 0.81
        At 10 cases → would be 1.0 but officers at max are excluded by fetch query

        Returns float in [0.0, 1.0].
        """
        ratio = officer.active_case_count / MAX_OFFICER_CASES
        return float(ratio ** 2)

    @staticmethod
    def skill_mismatch(officer: OfficerRow, case: CaseRow) -> float:
        """
        Compares officer qualification keywords against the expected skills
        for the case crime type.

        qualification is a comma-separated string (e.g. "CRIMINAL_INVESTIGATION,ANTI_NARCOTICS")
        SKILL_MAP maps crime_type → list of preferred skill keywords.

        Scoring:
            Any exact keyword match → 0.0 (perfect skill match)
            No match but general   → 0.5 (partial — some investigative ability)
            No relevant skill      → 1.0 (worst mismatch)

        Returns float in [0.0, 1.0].
        """
        expected_skills = SKILL_MAP.get(case.case_type.upper(), [])
        if not expected_skills:
            # Unknown crime type — neutral penalty
            return 0.5

        officer_quals = {q.strip().upper() for q in officer.qualification.split(",") if q.strip()}

        for skill in expected_skills:
            if skill.upper() in officer_quals:
                return 0.0  # Exact match

        # Check for general criminal investigation as a partial match fallback
        if "CRIMINAL_INVESTIGATION" in officer_quals:
            return 0.5

        return 1.0  # No relevant qualification

    @staticmethod
    def rank_suitability(officer: OfficerRow, case: CaseRow) -> float:
        """
        Maps case severity to the minimum required officer rank.
        Officers below the minimum get a penalty of 1.0.
        Officers meeting or exceeding the minimum get 0.0.

        Severity → minimum rank mapping:
            CRITICAL → DSP+
            HIGH     → INSPECTOR+
            MEDIUM   → SI+
            LOW      → any (CONSTABLE+)

        Returns float in {0.0, 1.0}.
        """
        min_rank_name = SEVERITY_MIN_RANK.get(case.severity.upper(), "CONSTABLE")
        min_rank_int  = RANK_ORDER.get(min_rank_name, 1)
        officer_rank_int = RANK_ORDER.get(officer.current_rank.upper(), 1)

        return 0.0 if officer_rank_int >= min_rank_int else 1.0

    @staticmethod
    def geographic_distance(officer: OfficerRow, case: CaseRow) -> float:
        """
        Computes a normalised distance penalty between officer's station and
        the case incident location.

        Same station_id → 0.0 (no geographic cost).
        Different station → Haversine distance between officer station centroid
                            and case incident_lat/lon, normalised to [0, 1]
                            over a MAX_DISTANCE_KM reference distance.

        Station centroids are approximated from the Cases table by computing
        the mean lat/lon of cases at each station. This avoids needing a
        separate Stations distance table (which is not in the schema).

        If incident_lat/lon are both 0.0 (missing despite S2 contract),
        falls back to binary: same station = 0.0, different = 0.5.

        Returns float in [0.0, 1.0].
        """
        MAX_DISTANCE_KM = 50.0  # Reference: beyond 50 km = full penalty

        if case.incident_lat == 0.0 and case.incident_lon == 0.0:
            # S2 contract guarantees this is always populated, but be safe
            return 0.0 if officer.station_id == case.station_id else 0.5

        if officer.station_id == case.station_id:
            return 0.0

        # Approximate officer station centroid: use average lat/lon of
        # all cases at the officer's station from the already-loaded data.
        # This is passed in from the builder context.
        # If not available, fall back to binary penalty.
        return 0.5  # Replaced with actual haversine in build() using station_centroids

    @staticmethod
    def _haversine(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
        """
        Haversine formula: great-circle distance in kilometres.
        Both inputs in decimal degrees.
        """
        R = 6371.0
        phi1, phi2 = math.radians(lat1), math.radians(lat2)
        dphi  = math.radians(lat2 - lat1)
        dlam  = math.radians(lon2 - lon1)
        a = (math.sin(dphi / 2) ** 2
             + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2) ** 2)
        return 2 * R * math.asin(math.sqrt(a))

    def build(self, cases: list[CaseRow], officers: list[OfficerRow]) -> np.ndarray:
        """
        Assembles the N×M cost matrix where:
            N = number of cases  (rows)
            M = number of officers (columns)

        cost[i][j] = weighted sum of all four components for case_i × officer_j.

        scipy.optimize.linear_sum_assignment handles rectangular matrices
        (N ≠ M) correctly — unassigned rows/columns are simply not selected.

        Returns numpy float64 array of shape (N, M). scipy requires float64.
        """
        n = len(cases)
        m = len(officers)

        if n == 0 or m == 0:
            return np.zeros((max(n, 1), max(m, 1)), dtype=np.float64)

        # Pre-compute station centroids from case incident coordinates
        # so geographic_distance can do a real haversine calculation.
        station_centroids: dict[int, tuple[float, float]] = {}
        for c in cases:
            if c.incident_lat != 0.0 or c.incident_lon != 0.0:
                sid = c.station_id
                if sid not in station_centroids:
                    station_centroids[sid] = (c.incident_lat, c.incident_lon)
                else:
                    # Running mean (simplified — use first case per station)
                    pass

        MAX_DISTANCE_KM = 50.0

        cost = np.zeros((n, m), dtype=np.float64)

        for i, case in enumerate(cases):
            for j, officer in enumerate(officers):
                # Component 1: Workload penalty
                w_penalty = self.workload_penalty(officer) * WORKLOAD_WEIGHT

                # Component 2: Skill mismatch
                s_mismatch = self.skill_mismatch(officer, case) * SKILL_WEIGHT

                # Component 3: Rank suitability
                r_suitability = self.rank_suitability(officer, case) * RANK_WEIGHT

                # Component 4: Geographic distance (Haversine)
                if (case.incident_lat == 0.0 and case.incident_lon == 0.0):
                    geo = 0.0 if officer.station_id == case.station_id else 0.5
                elif officer.station_id == case.station_id:
                    geo = 0.0
                else:
                    # Get officer station centroid
                    officer_centroid = station_centroids.get(officer.station_id)
                    if officer_centroid:
                        dist_km = self._haversine(
                            officer_centroid[0], officer_centroid[1],
                            case.incident_lat,   case.incident_lon,
                        )
                        geo = min(dist_km / MAX_DISTANCE_KM, 1.0)
                    else:
                        geo = 0.5  # Unknown station location — neutral penalty

                g_distance = geo * GEOGRAPHIC_WEIGHT

                cost[i, j] = w_penalty + s_mismatch + r_suitability + g_distance

        log.info(
            "Cost matrix built: %d cases × %d officers. "
            "Mean cost: %.4f, Min: %.4f, Max: %.4f",
            n, m, float(np.mean(cost)), float(np.min(cost)), float(np.max(cost))
        )
        return cost


# ============================================================================
# HungarianSolver
# Thin wrapper over scipy.optimize.linear_sum_assignment.
# Exists solely to isolate the solver dependency so it can be swapped
# (e.g. to OR-Tools or a custom implementation) without touching anything else.
# ============================================================================
class HungarianSolver:
    """
    Wraps scipy's linear_sum_assignment to produce typed output.

    solve() returns a list of dicts:
        {'case_index': int, 'officer_index': int, 'cost': float}

    Only includes valid assignments (not padding rows in rectangular matrices).
    """

    @staticmethod
    def solve(cost_matrix: np.ndarray,
              cases: list[CaseRow],
              officers: list[OfficerRow]) -> list[dict]:
        """
        Applies the Hungarian Algorithm to the cost matrix.

        scipy handles rectangular matrices (N ≠ M):
            If N > M (more cases than officers): some cases unassigned.
            If N < M (more officers than cases): some officers unused.

        row_ind[k], col_ind[k] = case index k, best officer index for it.
        Only pairs where both indices are within the valid data range are kept.

        Returns list of assignment dicts sorted by cost ascending.
        """
        if cost_matrix.size == 0:
            return []

        row_ind, col_ind = linear_sum_assignment(cost_matrix)

        n_cases    = len(cases)
        n_officers = len(officers)

        assignments = []
        for ri, ci in zip(row_ind, col_ind):
            if ri < n_cases and ci < n_officers:
                assignments.append({
                    "case_index":    int(ri),
                    "officer_index": int(ci),
                    "cost":          float(cost_matrix[ri, ci]),
                })

        # Sort by cost ascending so the dashboard shows best matches first
        assignments.sort(key=lambda x: x["cost"])

        log.info(
            "Hungarian Algorithm solved: %d assignments from %d×%d matrix",
            len(assignments), cost_matrix.shape[0], cost_matrix.shape[1]
        )
        return assignments


# ============================================================================
# run_analysis  —  7-stage Pipeline orchestrator
# No exception bubbles out. Every stage wrapped in try/except.
# A failure logs to stderr → FIFO error msg → shm error code → exit(1).
# ============================================================================
def run_analysis() -> None:
    """
    Main pipeline. Called once per agent invocation.

    Stage 1: expire_old_suggestions()        — clear stale data
    Stage 2: fetch cases and officers        — if empty, exit cleanly
    Stage 3: build cost matrix               — CostMatrixBuilder
    Stage 4: solve with Hungarian            — HungarianSolver
    Stage 5: write suggestions               — DBRepository.write_suggestions
    Stage 6: log performance                 — DBRepository.log_performance
    Stage 7: write FIFO + shared memory      — IPC reporting
    """
    t_start = time.monotonic()

    # ---- Startup FIFO message ----
    _write_fifo(_build_fifo_msg(
        agent_name="workload", error_code=0,
        predictions=0, run_time_ms=0, is_running=1,
        error_detail="",
    ))

    repo = DBRepository()

    try:
        repo.connect()
    except Exception as exc:
        log.error("Stage 0: DB connection failed: %s", exc)
        _write_fifo(_build_fifo_msg(
            "workload", errno.ECONNREFUSED, 0, 0, 0,
            f"DB connect failed: {exc}"[:256],
        ))
        _update_shared_memory(0, 0.0, False, errno.ECONNREFUSED)
        sys.exit(1)

    cases:       list[CaseRow]    = []
    officers:    list[OfficerRow] = []
    assignments: list[dict]       = []
    assignments_written: int      = 0

    try:
        # ---- Stage 1: Expire stale suggestions ----
        log.info("Stage 1: Expiring stale suggestions")
        try:
            repo.expire_old_suggestions()
        except Exception as exc:
            log.error("Stage 1 failed (expire_old_suggestions): %s", exc)
            raise

        # ---- Stage 2: Fetch data ----
        log.info("Stage 2: Fetching unassigned cases and available officers")
        try:
            cases    = repo.fetch_unassigned_cases()
            officers = repo.fetch_available_officers()
        except Exception as exc:
            log.error("Stage 2 failed (data fetch): %s", exc)
            raise

        if not cases:
            log.info("Stage 2: No unassigned cases found — nothing to assign. Exiting cleanly.")
            repo.log_performance(
                run_time_ms=int((time.monotonic() - t_start) * 1000),
                case_count=0, officer_count=len(officers), assignments_made=0,
            )
            _write_fifo(_build_fifo_msg("workload", 0, 0, 0, 0, "NO_UNASSIGNED_CASES"))
            _update_shared_memory(0, 1.0, False, 0)
            return

        if not officers:
            log.warning("Stage 2: No available officers — all at capacity or inactive. Exiting cleanly.")
            repo.log_performance(
                run_time_ms=int((time.monotonic() - t_start) * 1000),
                case_count=len(cases), officer_count=0, assignments_made=0,
            )
            _write_fifo(_build_fifo_msg("workload", 0, 0, 0, 0, "NO_AVAILABLE_OFFICERS"))
            _update_shared_memory(0, 1.0, False, 0)
            return

        log.info("Stage 2: %d cases, %d officers loaded", len(cases), len(officers))

        # ---- Stage 3: Build cost matrix ----
        log.info("Stage 3: Building %d×%d cost matrix", len(cases), len(officers))
        try:
            builder     = CostMatrixBuilder()
            cost_matrix = builder.build(cases, officers)
        except Exception as exc:
            log.error("Stage 3 failed (cost matrix): %s", exc)
            raise

        # ---- Stage 4: Solve with Hungarian Algorithm ----
        log.info("Stage 4: Running Hungarian Algorithm solver")
        try:
            solver      = HungarianSolver()
            assignments = solver.solve(cost_matrix, cases, officers)
        except Exception as exc:
            log.error("Stage 4 failed (Hungarian solver): %s", exc)
            raise

        if not assignments:
            log.warning("Stage 4: Solver returned no assignments")

        # ---- Stage 5: Write suggestions to analytics schema ----
        log.info("Stage 5: Writing %d suggestions to analytics schema", len(assignments))
        try:
            assignments_written = repo.write_suggestions(assignments, cases, officers)
        except Exception as exc:
            log.error("Stage 5 failed (write_suggestions): %s", exc)
            raise

        # ---- Stage 6: Log performance ----
        log.info("Stage 6: Logging performance metrics")
        run_time_ms = int((time.monotonic() - t_start) * 1000)
        try:
            repo.log_performance(
                run_time_ms=run_time_ms,
                case_count=len(cases),
                officer_count=len(officers),
                assignments_made=assignments_written,
            )
        except Exception as exc:
            log.warning("Stage 6 failed (log_performance — non-fatal): %s", exc)
            # Performance logging failure is non-fatal

        # ---- Stage 7: FIFO + shared memory ----
        log.info("Stage 7: Reporting completion via FIFO and shared memory")
        _write_fifo(_build_fifo_msg(
            agent_name="workload",
            error_code=0,
            predictions=assignments_written,
            run_time_ms=run_time_ms,
            is_running=0,
            error_detail="",
        ))
        _update_shared_memory(
            predictions=assignments_written,
            model_accuracy=1.0,  # Hungarian is exact (optimal given cost matrix)
            is_running=False,
            last_error_code=0,
        )

        log.info(
            "run_analysis complete. Cases: %d, Officers: %d, "
            "Assignments: %d, Time: %d ms",
            len(cases), len(officers), assignments_written, run_time_ms,
        )

    except Exception as exc:
        run_time_ms = int((time.monotonic() - t_start) * 1000)
        err_code    = errno.EIO
        err_detail  = str(exc)[:256]

        log.error("run_analysis failed: %s", exc, exc_info=True)

        # Report error via FIFO
        _write_fifo(_build_fifo_msg(
            agent_name="workload",
            error_code=err_code,
            predictions=0,
            run_time_ms=run_time_ms,
            is_running=0,
            error_detail=err_detail,
        ))

        # Update shared memory error slot
        _update_shared_memory(
            predictions=0,
            model_accuracy=0.0,
            is_running=False,
            last_error_code=err_code,
        )

        sys.exit(1)

    finally:
        repo.close()


# ============================================================================
# Entry point
# ============================================================================
if __name__ == "__main__":
    log.info(
        "workload_agent starting — PID %d, PGUSER=%s",
        os.getpid(), os.environ.get("PGUSER", "<not set>"),
    )
    run_analysis()