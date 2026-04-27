from __future__ import annotations

import ctypes
import ctypes.util
import errno
import json
import logging
import math
import mmap
import os
import struct
import sys
import time
from dataclasses import dataclass
from typing import Optional

import numpy as np
import psycopg2
import psycopg2.extras
from scipy.optimize import linear_sum_assignment

# ============================================================================
# Logging — stderr only.
# ============================================================================
logging.basicConfig(
    stream=sys.stderr,
    level=logging.INFO,
    format="%(asctime)s [workload_agent] %(levelname)s %(message)s",
    datefmt="%Y-%m-%dT%H:%M:%S",
)
log = logging.getLogger("workload_agent")

# ============================================================================
# Constants — weights defined here, never inline
# ============================================================================
WORKLOAD_WEIGHT: float = 0.35
SKILL_WEIGHT: float = 0.30
RANK_WEIGHT: float = 0.20
GEOGRAPHIC_WEIGHT: float = 0.15

RANK_ORDER: dict[str, int] = {
    "CONSTABLE": 1,
    "HEAD_CONSTABLE": 2,
    "ASI": 3,
    "SI": 4,
    "INSPECTOR": 5,
    "DSP": 6,
    "SP": 7,
    "SSP": 8,
    "DIG": 9,
    "ADDL_IG": 10,
    "IGP": 11,
}

# Agent-only severity (your live DB currently doesn't have Cases.severity)
CASE_TYPE_SEVERITY: dict[str, str] = {
    "TERRORISM": "CRITICAL",
    "MURDER": "CRITICAL",
    "RAPE": "CRITICAL",
    "KIDNAPPING": "HIGH",
    "ARMED_ROBBERY": "HIGH",
    "HUMAN_TRAFFICKING": "HIGH",
    "DRUG_TRAFFICKING": "HIGH",
    "AGGRAVATED_ASSAULT": "HIGH",
    "ROBBERY": "MEDIUM",
    "CYBERCRIME": "MEDIUM",
    "FRAUD": "MEDIUM",
    "BURGLARY": "MEDIUM",
    "ASSAULT": "LOW",
    "THEFT": "LOW",
    "BRIBERY": "LOW",
    "FORGERY": "LOW",
}

SEVERITY_MIN_RANK: dict[str, str] = {
    "CRITICAL": "DSP",
    "HIGH": "INSPECTOR",
    "MEDIUM": "SI",
    "LOW": "CONSTABLE",
}

SKILL_MAP: dict[str, list[str]] = {
    "MURDER": ["HOMICIDE", "CRIMINAL_INVESTIGATION"],
    "KIDNAPPING": ["CRIMINAL_INVESTIGATION", "SPECIAL_BRANCH"],
    "DRUG_TRAFFICKING": ["ANTI_NARCOTICS", "CRIMINAL_INVESTIGATION"],
    "TERRORISM": ["COUNTER_TERRORISM", "SPECIAL_BRANCH"],
    "CYBERCRIME": ["CYBER", "DIGITAL_FORENSICS"],
    "FRAUD": ["FINANCIAL_CRIMES", "ANTI_CORRUPTION"],
    "ROBBERY": ["CRIMINAL_INVESTIGATION"],
    "ARMED_ROBBERY": ["CRIMINAL_INVESTIGATION", "SPECIAL_BRANCH"],
    "ASSAULT": ["CRIMINAL_INVESTIGATION"],
    "AGGRAVATED_ASSAULT": ["CRIMINAL_INVESTIGATION"],
    "BURGLARY": ["CRIMINAL_INVESTIGATION"],
    "GANG_ACTIVITY": ["CRIMINAL_INVESTIGATION", "COUNTER_TERRORISM"],
    "HUMAN_TRAFFICKING": ["SPECIAL_BRANCH", "CRIMINAL_INVESTIGATION"],
    "BRIBERY": ["ANTI_CORRUPTION"],
    "FORGERY": ["FINANCIAL_CRIMES"],
}

FIFO_PATH: str = "/tmp/justiceflow_workload.fifo"
SHM_NAME: str = "/justiceflow_shm"
AGENT_INDEX: int = 2
MAX_OFFICER_CASES: int = 10
SUGGESTION_TTL_SECONDS: int = 6 * 3600

MODEL_VERSION: str = os.environ.get("JF_MODEL_VERSION", "WF-HUNGARIAN-v1")
ALGORITHM_NAME: str = "HungarianAlgorithm"

# ============================================================================
# Data classes
# ============================================================================
@dataclass
class CaseRow:
    case_id: int
    case_type: str
    case_status: str
    severity: str
    station_id: int
    incident_lat: float
    incident_lon: float
    filed_at_epoch: float


@dataclass
class OfficerRow:
    officer_id: int
    belt_number: str
    current_rank: str
    qualification: str
    station_id: int
    active_case_count: int


# ============================================================================
# FIFO status message (optional)
# ============================================================================
_FIFO_MSG_FORMAT = "=32siii i256s"


def _build_fifo_msg(
    agent_name: str,
    error_code: int,
    predictions: int,
    run_time_ms: int,
    is_running: int,
    error_detail: str,
) -> bytes:
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
        pass


# ============================================================================
# Shared memory status slot (optional)
# ============================================================================
_SLOT_FORMAT = "=qqifi i"
_SLOT_SIZE = struct.calcsize(_SLOT_FORMAT)
_SHM_OFFSET = AGENT_INDEX * _SLOT_SIZE


def _update_shared_memory(
    predictions: int,
    model_accuracy: float,
    is_running: bool,
    last_error_code: int,
) -> None:
    try:
        libc_name = ctypes.util.find_library("c")
        libc = ctypes.CDLL(libc_name, use_errno=True)

        fd = libc.shm_open(SHM_NAME.encode(), os.O_RDWR, 0)
        if fd < 0:
            return

        now = int(time.time())
        slot_data = struct.pack(
            _SLOT_FORMAT,
            now,
            now + SUGGESTION_TTL_SECONDS,
            predictions,
            float(model_accuracy),
            1 if is_running else 0,
            last_error_code,
        )

        total_size = _SLOT_SIZE * 3
        with mmap.mmap(fd, total_size, access=mmap.ACCESS_WRITE) as mm:
            mm.seek(_SHM_OFFSET)
            mm.write(slot_data)

        os.close(fd)
    except Exception:
        pass  # IPC optional


# ============================================================================
# Helpers
# ============================================================================
def derive_severity(case_type: str) -> str:
    ct = (case_type or "UNKNOWN").upper()
    return CASE_TYPE_SEVERITY.get(ct, "MEDIUM")


def json_dumps_compact(obj) -> str:
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=True)


# ============================================================================
# DB Repository
# ============================================================================
class DBRepository:
    def __init__(self) -> None:
        self._conn: Optional[psycopg2.extensions.connection] = None

    def connect(self) -> None:
        self._conn = psycopg2.connect(
            host=os.environ["PGHOST"],
            port=int(os.environ.get("PGPORT", 5432)),
            dbname=os.environ["PGDATABASE"],
            user=os.environ["PGUSER"],
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
        if self._conn is None or self._conn.closed:
            self.connect()
        return self._conn.cursor(cursor_factory=psycopg2.extras.DictCursor)

    def expire_old_suggestions(self) -> int:
        # justice_ai is INSERT-only by design; expiry is OS job.
        log.info(
            "Stage 1: Skipping expiry (justice_ai is INSERT-only; "
            "use expire_workload_assignments() via OS scheduler)."
        )
        return 0

    def fetch_unassigned_cases(self) -> list[CaseRow]:
        sql = """
            SELECT
                c.case_id,
                c.case_type,
                c.case_status,
                c.station_id,
                COALESCE(c.incident_lat, 0.0)   AS incident_lat,
                COALESCE(c.incident_lon, 0.0)   AS incident_lon,
                EXTRACT(EPOCH FROM c.filed_at)::bigint AS filed_at_epoch
            FROM public.Cases c
            WHERE c.case_status IN ('REGISTERED', 'UNDER_INVESTIGATION')
              AND (
                  c.lead_officer_id IS NULL
                  OR NOT EXISTS (
                      SELECT 1
                      FROM public.Case_Officers co
                      WHERE co.case_id = c.case_id
                  )
              )
            ORDER BY c.filed_at ASC;
        """
        with self._cursor() as cur:
            cur.execute(sql)
            rows = cur.fetchall()

        out: list[CaseRow] = []
        for r in rows:
            case_type = (r["case_type"] or "UNKNOWN")
            out.append(
                CaseRow(
                    case_id=int(r["case_id"]),
                    case_type=str(case_type),
                    case_status=str(r["case_status"]),
                    severity=derive_severity(str(case_type)),
                    station_id=int(r["station_id"]),
                    incident_lat=float(r["incident_lat"]),
                    incident_lon=float(r["incident_lon"]),
                    filed_at_epoch=float(r["filed_at_epoch"]),
                )
            )
        return out

    def fetch_available_officers(self) -> list[OfficerRow]:
        sql = """
            SELECT
                o.officer_id,
                o.belt_number,
                o.current_rank,
                COALESCE(o.qualification, '')      AS qualification,
                o.station_id,
                COALESCE(co_count.active_count, 0) AS active_case_count
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

        out: list[OfficerRow] = []
        for r in rows:
            out.append(
                OfficerRow(
                    officer_id=int(r["officer_id"]),
                    belt_number=str(r["belt_number"] or ""),
                    current_rank=str(r["current_rank"] or "CONSTABLE"),
                    qualification=str(r["qualification"] or ""),
                    station_id=int(r["station_id"]),
                    active_case_count=int(r["active_case_count"]),
                )
            )
        return out

    def insert_suggestions(
        self,
        assignments: list[dict],
        cases: list[CaseRow],
        officers: list[OfficerRow],
    ) -> int:
        """
        INSERT-only, matching analytics.Officer_Workload_Assignments schema.
        No UPSERT (schema has no UNIQUE(case_id)).
        """
        if not assignments:
            return 0

        sql = """
            INSERT INTO analytics.Officer_Workload_Assignments
                (case_id, officer_id, assignment_status, cost_score,
                 recommendation_reason, cost_breakdown,
                 officer_active_cases, officer_workload_score,
                 expires_at, analyzed_at, model_version, algorithm)
            VALUES
                (%(case_id)s, %(officer_id)s, 'SUGGESTED', %(cost_score)s,
                 %(reason)s, %(cost_breakdown)s::jsonb,
                 %(officer_active_cases)s, %(officer_workload_score)s,
                 NOW() + INTERVAL '6 hours', NOW(), %(model_version)s, %(algorithm)s);
        """

        records: list[dict] = []
        for a in assignments:
            case = cases[a["case_index"]]
            officer = officers[a["officer_index"]]

            workload_penalty = CostMatrixBuilder.workload_penalty(officer)
            skill_mismatch = CostMatrixBuilder.skill_mismatch(officer, case)
            rank_penalty = CostMatrixBuilder.rank_suitability(officer, case)
            workload_score = float(officer.active_case_count / MAX_OFFICER_CASES)

            breakdown = {
                "components_0_to_1": {
                    "workload_penalty": workload_penalty,
                    "skill_mismatch": skill_mismatch,
                    "rank_penalty": rank_penalty,
                },
                "weights": {
                    "workload": WORKLOAD_WEIGHT,
                    "skill": SKILL_WEIGHT,
                    "rank": RANK_WEIGHT,
                    "geographic": GEOGRAPHIC_WEIGHT,
                },
                "derived_case_severity": case.severity,
                "note": "DB does not contain Cases.severity; agent derives it from case_type.",
            }

            reason = (
                f"Hungarian best-fit. "
                f"case_type={case.case_type}, severity={case.severity}, "
                f"officer_rank={officer.current_rank}, active_cases={officer.active_case_count}, "
                f"station_match={'YES' if officer.station_id == case.station_id else 'NO'}."
            )

            records.append(
                {
                    "case_id": case.case_id,
                    "officer_id": officer.officer_id,
                    "cost_score": float(a["cost"]),
                    "reason": reason,
                    "cost_breakdown": json_dumps_compact(breakdown),
                    "officer_active_cases": int(officer.active_case_count),
                    "officer_workload_score": float(workload_score),
                    "model_version": MODEL_VERSION,
                    "algorithm": ALGORITHM_NAME,
                }
            )

        with self._cursor() as cur:
            psycopg2.extras.execute_batch(cur, sql, records, page_size=100)

        self._conn.commit()
        return len(records)


# ============================================================================
# Cost Matrix + Solver
# ============================================================================
class CostMatrixBuilder:
    @staticmethod
    def workload_penalty(officer: OfficerRow) -> float:
        ratio = officer.active_case_count / MAX_OFFICER_CASES
        return float(ratio**2)

    @staticmethod
    def skill_mismatch(officer: OfficerRow, case: CaseRow) -> float:
        expected_skills = SKILL_MAP.get(case.case_type.upper(), [])
        if not expected_skills:
            return 0.5

        officer_quals = {q.strip().upper() for q in officer.qualification.split(",") if q.strip()}
        for skill in expected_skills:
            if skill.upper() in officer_quals:
                return 0.0

        if "CRIMINAL_INVESTIGATION" in officer_quals:
            return 0.5

        return 1.0

    @staticmethod
    def rank_suitability(officer: OfficerRow, case: CaseRow) -> float:
        min_rank_name = SEVERITY_MIN_RANK.get(case.severity.upper(), "CONSTABLE")
        min_rank_int = RANK_ORDER.get(min_rank_name, 1)
        officer_rank_int = RANK_ORDER.get(officer.current_rank.upper(), 1)
        return 0.0 if officer_rank_int >= min_rank_int else 1.0

    @staticmethod
    def _haversine(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
        R = 6371.0
        phi1, phi2 = math.radians(lat1), math.radians(lat2)
        dphi = math.radians(lat2 - lat1)
        dlam = math.radians(lon2 - lon1)
        a = (math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2) ** 2)
        return 2 * R * math.asin(math.sqrt(a))

    def build(self, cases: list[CaseRow], officers: list[OfficerRow]) -> np.ndarray:
        n = len(cases)
        m = len(officers)
        if n == 0 or m == 0:
            return np.zeros((max(n, 1), max(m, 1)), dtype=np.float64)

        station_centroids: dict[int, tuple[float, float]] = {}
        for c in cases:
            if c.incident_lat != 0.0 or c.incident_lon != 0.0:
                station_centroids.setdefault(c.station_id, (c.incident_lat, c.incident_lon))

        MAX_DISTANCE_KM = 50.0
        cost = np.zeros((n, m), dtype=np.float64)

        for i, case in enumerate(cases):
            for j, officer in enumerate(officers):
                w = self.workload_penalty(officer) * WORKLOAD_WEIGHT
                s = self.skill_mismatch(officer, case) * SKILL_WEIGHT
                r = self.rank_suitability(officer, case) * RANK_WEIGHT

                if (case.incident_lat == 0.0 and case.incident_lon == 0.0):
                    geo = 0.0 if officer.station_id == case.station_id else 0.5
                elif officer.station_id == case.station_id:
                    geo = 0.0
                else:
                    officer_centroid = station_centroids.get(officer.station_id)
                    if officer_centroid:
                        dist_km = self._haversine(
                            officer_centroid[0],
                            officer_centroid[1],
                            case.incident_lat,
                            case.incident_lon,
                        )
                        geo = min(dist_km / MAX_DISTANCE_KM, 1.0)
                    else:
                        geo = 0.5

                g = geo * GEOGRAPHIC_WEIGHT
                cost[i, j] = w + s + r + g

        log.info(
            "Cost matrix built: %d cases × %d officers. Mean=%.4f Min=%.4f Max=%.4f",
            n,
            m,
            float(np.mean(cost)),
            float(np.min(cost)),
            float(np.max(cost)),
        )
        return cost


class HungarianSolver:
    @staticmethod
    def solve(cost_matrix: np.ndarray, cases: list[CaseRow], officers: list[OfficerRow]) -> list[dict]:
        if cost_matrix.size == 0:
            return []

        row_ind, col_ind = linear_sum_assignment(cost_matrix)

        n_cases = len(cases)
        n_officers = len(officers)

        assignments: list[dict] = []
        for ri, ci in zip(row_ind, col_ind):
            if ri < n_cases and ci < n_officers:
                assignments.append(
                    {"case_index": int(ri), "officer_index": int(ci), "cost": float(cost_matrix[ri, ci])}
                )

        assignments.sort(key=lambda x: x["cost"])
        log.info(
            "Hungarian Algorithm solved: %d assignments from %d×%d matrix",
            len(assignments),
            cost_matrix.shape[0],
            cost_matrix.shape[1],
        )
        return assignments


# ============================================================================
# Entry pipeline
# ============================================================================
def run_analysis() -> None:
    t_start = time.monotonic()

    _write_fifo(_build_fifo_msg("workload", 0, 0, 0, 1, ""))

    repo = DBRepository()
    try:
        repo.connect()
    except Exception as exc:
        log.error("DB connection failed: %s", exc)
        _write_fifo(_build_fifo_msg("workload", errno.ECONNREFUSED, 0, 0, 0, f"{exc}"[:256]))
        _update_shared_memory(0, 0.0, False, errno.ECONNREFUSED)
        sys.exit(1)

    try:
        log.info("Stage 1: Expiring stale suggestions")
        repo.expire_old_suggestions()

        log.info("Stage 2: Fetching unassigned cases and available officers")
        cases = repo.fetch_unassigned_cases()
        officers = repo.fetch_available_officers()

        if not cases:
            log.info("No unassigned cases — exiting cleanly.")
            _write_fifo(_build_fifo_msg("workload", 0, 0, 0, 0, "NO_UNASSIGNED_CASES"))
            _update_shared_memory(0, 1.0, False, 0)
            return

        if not officers:
            log.warning("No available officers — exiting cleanly.")
            _write_fifo(_build_fifo_msg("workload", 0, 0, 0, 0, "NO_AVAILABLE_OFFICERS"))
            _update_shared_memory(0, 1.0, False, 0)
            return

        log.info("Loaded: %d cases, %d officers", len(cases), len(officers))

        log.info("Stage 3: Building cost matrix")
        cost_matrix = CostMatrixBuilder().build(cases, officers)

        log.info("Stage 4: Solving assignments")
        assignments = HungarianSolver.solve(cost_matrix, cases, officers)

        log.info("Stage 5: Inserting suggestions")
        written = repo.insert_suggestions(assignments, cases, officers)

        run_time_ms = int((time.monotonic() - t_start) * 1000)
        _write_fifo(_build_fifo_msg("workload", 0, written, run_time_ms, 0, ""))
        _update_shared_memory(written, 1.0, False, 0)

        log.info("Done. Inserted %d suggestion(s) in %d ms.", written, run_time_ms)

    except Exception as exc:
        run_time_ms = int((time.monotonic() - t_start) * 1000)
        log.error("run_analysis failed: %s", exc, exc_info=True)
        _write_fifo(_build_fifo_msg("workload", errno.EIO, 0, run_time_ms, 0, str(exc)[:256]))
        _update_shared_memory(0, 0.0, False, errno.EIO)
        sys.exit(1)

    finally:
        repo.close()


if __name__ == "__main__":
    log.info(
        "workload_agent starting — PID %d, PGUSER=%s",
        os.getpid(),
        os.environ.get("PGUSER", "<not set>"),
    )
    run_analysis()