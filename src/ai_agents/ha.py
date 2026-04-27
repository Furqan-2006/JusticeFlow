#!/usr/bin/env python3
import psycopg2
import numpy as np
from sklearn.cluster import DBSCAN
from datetime import datetime

# ---------------------------------------------------------
# Database Configuration
# ---------------------------------------------------------
DB_NAME = "justiceflow"
DB_USER = "justiceflow"
DB_PASS = "justiceflow123"
DB_HOST = "localhost"

TOP_K_ZONES = 5

def get_db_connection():
    try:
        return psycopg2.connect(
            dbname=DB_NAME,
            user=DB_USER,
            password=DB_PASS,
            host=DB_HOST
        )
    except Exception as e:
        print(f"[AI: Hotspot] Critical DB Connection Failure: {e}")
        return None

def _analytics_table_exists(cur, table_fqtn: str) -> bool:
    # table_fqtn like "analytics.hotspots"
    try:
        cur.execute("SELECT to_regclass(%s);", (table_fqtn,))
        return cur.fetchone()[0] is not None
    except Exception:
        return False

def _write_hotspots_if_possible(cur, hotspots):
    """
    Writes to analytics.hotspots only if the table exists.
    Expected schema (simple):
      analytics.hotspots(
        run_at timestamptz,
        zone_rank int,
        incident_count int,
        centroid_lat double precision,
        centroid_lon double precision
      )
    """
    if not _analytics_table_exists(cur, "analytics.hotspots"):
        return False

    run_at = datetime.utcnow()
    cur.execute("DELETE FROM analytics.hotspots;")
    for i, hz in enumerate(hotspots, start=1):
        cur.execute(
            """
            INSERT INTO analytics.hotspots(run_at, zone_rank, incident_count, centroid_lat, centroid_lon)
            VALUES (%s, %s, %s, %s, %s);
            """,
            (run_at, i, hz["incident_count"], hz["centroid_lat"], hz["centroid_lon"])
        )
    return True

def train_hotspot_model():
    print("[AI: Hotspot] Running DBSCAN hotspot detection (Top 5 zones)...")
    conn = get_db_connection()
    if not conn:
        return

    cur = None
    try:
        cur = conn.cursor()

        # Fetch geo data for active (not closed) cases.
        # Minimal assumption: Cases has latitude/longitude columns.
        cur.execute("""
            SELECT case_id, latitude, longitude
            FROM Cases
            WHERE status != 'CLOSED'
              AND latitude IS NOT NULL
              AND longitude IS NOT NULL;
        """)
        rows = cur.fetchall()

        if len(rows) < 10:
            print("[AI: Hotspot] Insufficient data for clustering. Need at least 10 geo-tagged active cases.")
            return

        # Filter out obviously invalid coordinates (keeps it robust without extra complexity)
        coords = []
        for _, lat, lon in rows:
            try:
                lat = float(lat)
                lon = float(lon)
            except Exception:
                continue
            if -90 <= lat <= 90 and -180 <= lon <= 180:
                coords.append([lat, lon])

        if len(coords) < 10:
            print("[AI: Hotspot] Insufficient valid coordinates after filtering.")
            return

        coords = np.array(coords, dtype=float)

        # DBSCAN with haversine distance: eps in radians
        kms_per_radian = 6371.0088
        epsilon = 1.5 / kms_per_radian  # ~1.5km radius
        db = DBSCAN(
            eps=epsilon,
            min_samples=5,
            algorithm="ball_tree",
            metric="haversine"
        ).fit(np.radians(coords))

        labels = db.labels_
        clusters = []
        for k in set(labels):
            if k == -1:
                continue  # noise
            mask = (labels == k)
            pts = coords[mask]
            clusters.append({
                "cluster_id": int(k),
                "incident_count": int(len(pts)),
                "centroid_lat": float(np.mean(pts[:, 0])),
                "centroid_lon": float(np.mean(pts[:, 1]))
            })

        # Sort by incident_count desc and take top 5 (proposal requirement)
        clusters.sort(key=lambda c: c["incident_count"], reverse=True)
        top = clusters[:TOP_K_ZONES]

        print(f"[AI: Hotspot] Detected {len(clusters)} zone(s). Showing top {len(top)}:")
        for rank, zone in enumerate(top, start=1):
            print(
                f" -> Rank {rank}: Lat {zone['centroid_lat']:.4f}, Lon {zone['centroid_lon']:.4f} "
                f"| Incidents: {zone['incident_count']}"
            )

        # Optional persistence into analytics schema if table exists
        wrote = _write_hotspots_if_possible(cur, top)
        if wrote:
            conn.commit()
            print("[AI: Hotspot] Saved top zones to analytics.hotspots")
        else:
            print("[AI: Hotspot] analytics.hotspots not found; skipping DB write (printing only).")

    except Exception as e:
        print(f"[AI: Hotspot] Execution Error: {e}")
    finally:
        try:
            if cur:
                cur.close()
        finally:
            conn.close()

if __name__ == "__main__":
    train_hotspot_model()