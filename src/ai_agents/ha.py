#!/usr/bin/env python3
import os
import json
import psycopg2
import numpy as np
from sklearn.cluster import DBSCAN
from collections import Counter
from datetime import date, timedelta

# ---------------------------------------------------------
# DB Config (match your runner)
# ---------------------------------------------------------
DB_NAME = os.getenv("JF_DB_NAME", "justiceflow")
DB_USER = os.getenv("JF_DB_USER", "justice_ai")
DB_PASS = os.getenv("JF_DB_PASS", "")
DB_HOST = os.getenv("JF_DB_HOST", "/var/run/postgresql")
DB_PORT = os.getenv("JF_DB_PORT", "5432")

TOP_K_ZONES = 5

# DBSCAN params (keep simple, tunable)
KMS_PER_RADIAN = 6371.0088
EPS_KM = float(os.getenv("JF_HOTSPOT_EPS_KM", "1.5"))  # default 1.5km
EPSILON = EPS_KM / KMS_PER_RADIAN  # radians
MIN_SAMPLES = int(os.getenv("JF_HOTSPOT_MIN_SAMPLES", "5"))

MODEL_VERSION = "DBSCAN-v1.1"
ALGORITHM = "DBSCAN"

def get_db_connection():
    try:
        return psycopg2.connect(
            dbname=DB_NAME,
            user=DB_USER,
            password=DB_PASS,
            host=DB_HOST,
            port=DB_PORT,
        )
    except Exception as e:
        print(f"[AI: Hotspot] Critical DB Connection Failure: {e}")
        return None

def haversine_meters(lat1, lon1, lat2, lon2):
    """Distance between two points (degrees) in meters."""
    r = 6371008.8  # meters
    p1 = np.radians(lat1)
    p2 = np.radians(lat2)
    dlat = p2 - p1
    dlon = np.radians(lon2 - lon1)
    a = np.sin(dlat / 2.0) ** 2 + np.cos(p1) * np.cos(p2) * np.sin(dlon / 2.0) ** 2
    c = 2 * np.arctan2(np.sqrt(a), np.sqrt(1 - a))
    return float(r * c)

def risk_level(score: float) -> str:
    if score >= 0.75:
        return "HIGH"
    if score >= 0.50:
        return "MEDIUM"
    return "LOW"

def run_hotspots():
    print("[AI: Hotspot] Running DBSCAN hotspot detection (Top 5 zones)...")
    conn = get_db_connection()
    if not conn:
        return

    cur = None
    try:
        cur = conn.cursor()

        analysis_to = date.today()

        # Try recent window first, then expand if needed (prevents "no output" on sparse datasets)
        candidate_windows = [30, 180, 365]  # days
        rows = []
        analysis_from = None

        for days in candidate_windows:
            analysis_from = analysis_to - timedelta(days=days)
            cur.execute(
                """
                SELECT case_id, case_type::text, incident_lat, incident_lon
                FROM Cases
                WHERE case_status <> 'CLOSED'
                  AND incident_date::date BETWEEN %s AND %s
                  AND incident_lat IS NOT NULL
                  AND incident_lon IS NOT NULL;
                """,
                (analysis_from, analysis_to),
            )
            rows = cur.fetchall()
            if len(rows) >= 10:
                break

        # Final fallback: all open geo-tagged cases (no date filter)
        if len(rows) < 10:
            cur.execute(
                """
                SELECT case_id, case_type::text, incident_lat, incident_lon
                FROM Cases
                WHERE case_status <> 'CLOSED'
                  AND incident_lat IS NOT NULL
                  AND incident_lon IS NOT NULL;
                """
            )
            rows = cur.fetchall()
            # For table constraints (analysis_to > analysis_from), set a reasonable window
            analysis_from = analysis_to - timedelta(days=365)

        if len(rows) < max(5, MIN_SAMPLES * 2):
            print(f"[AI: Hotspot] Not enough geo-tagged open cases to form hotspots (found {len(rows)}).")
            return

        # Build arrays
        case_types = []
        coords = []
        for _, ctype, lat, lon in rows:
            try:
                lat = float(lat)
                lon = float(lon)
            except Exception:
                continue
            if -90 <= lat <= 90 and -180 <= lon <= 180:
                coords.append([lat, lon])
                case_types.append(str(ctype))

        if len(coords) < max(5, MIN_SAMPLES * 2):
            print(f"[AI: Hotspot] Insufficient valid coordinates after filtering (valid={len(coords)}).")
            return

        coords = np.array(coords, dtype=float)

        db = DBSCAN(
            eps=EPSILON,
            min_samples=MIN_SAMPLES,
            algorithm="ball_tree",
            metric="haversine",
        ).fit(np.radians(coords))

        labels = db.labels_

        clusters = []
        for k in sorted(set(labels)):
            if k == -1:
                continue

            mask = (labels == k)
            pts = coords[mask]
            types_in_cluster = [case_types[i] for i, m in enumerate(mask) if m]

            centroid_lat = float(np.mean(pts[:, 0]))
            centroid_lon = float(np.mean(pts[:, 1]))

            # Approx radius: max distance from centroid
            if len(pts) == 1:
                radius_m = 50
            else:
                dists = [haversine_meters(centroid_lat, centroid_lon, p[0], p[1]) for p in pts]
                radius_m = int(max(dists))

            breakdown = Counter(types_in_cluster)
            dominant_case_type = breakdown.most_common(1)[0][0]

            clusters.append({
                "case_count": int(len(pts)),
                "centroid_lat": centroid_lat,
                "centroid_lon": centroid_lon,
                "radius_m": radius_m,
                "dominant_case_type": dominant_case_type,
                "breakdown": dict(breakdown),
            })

        clusters.sort(key=lambda c: c["case_count"], reverse=True)
        top = clusters[:TOP_K_ZONES]

        if not top:
            print("[AI: Hotspot] No clusters formed (all incidents treated as noise).")
            return

        # INSERT-only permissions: DO NOT DELETE/UPDATE.
        # We'll just insert a fresh set for this run.
        max_count = max(c["case_count"] for c in top)
        min_count = min(c["case_count"] for c in top)
        denom = (max_count - min_count) if (max_count != min_count) else 1

        print(f"[AI: Hotspot] Found {len(clusters)} cluster(s). Inserting top {len(top)} into analytics.Crime_Hotspots...")

        for idx, z in enumerate(top, start=1):
            score = (z["case_count"] - min_count) / denom  # 0..1 among top-k
            rlevel = risk_level(float(score))

            patrol_pct = 50 if rlevel == "HIGH" else (25 if rlevel == "MEDIUM" else 10)
            rec_text = f"Increase patrol frequency by {patrol_pct}% in this zone."

            zone_label = f"Zone-{idx}"

            cur.execute(
                """
                INSERT INTO analytics.Crime_Hotspots (
                    zone_label, center_lat, center_lon, radius_meters, area_description,
                    case_count, dominant_case_type, case_type_breakdown,
                    risk_level, risk_score,
                    patrol_increase_pct, recommendation_text,
                    analysis_from, analysis_to,
                    model_version, algorithm, epsilon, min_samples
                )
                VALUES (
                    %s, %s, %s, %s, %s,
                    %s, %s, %s::jsonb,
                    %s, %s,
                    %s, %s,
                    %s, %s,
                    %s, %s, %s, %s
                );
                """,
                (
                    zone_label,
                    z["centroid_lat"],
                    z["centroid_lon"],
                    z["radius_m"],
                    None,  # area_description optional
                    z["case_count"],
                    z["dominant_case_type"],           # relies on enum text matching; should be fine if types are same
                    json.dumps(z["breakdown"]),
                    rlevel,
                    round(float(score), 4),
                    patrol_pct,
                    rec_text,
                    analysis_from,
                    analysis_to,
                    MODEL_VERSION,
                    ALGORITHM,
                    round(float(EPSILON), 6),
                    MIN_SAMPLES,
                ),
            )

            print(
                f" -> {zone_label}: center=({z['centroid_lat']:.5f},{z['centroid_lon']:.5f}) "
                f"radius={z['radius_m']}m cases={z['case_count']} dominant={z['dominant_case_type']} risk={rlevel}"
            )

        conn.commit()
        print("[AI: Hotspot] Done.")

    except Exception as e:
        print(f"[AI: Hotspot] Execution Error: {e}")
        conn.rollback()
    finally:
        try:
            if cur:
                cur.close()
        finally:
            conn.close()

if __name__ == "__main__":
    run_hotspots()