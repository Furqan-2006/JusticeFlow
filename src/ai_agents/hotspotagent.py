#!/usr/bin/env python3
import psycopg2
import numpy as np
from sklearn.cluster import DBSCAN
import os
import time

# ---------------------------------------------------------
# Database Configuration
# ---------------------------------------------------------
DB_NAME = "justiceflow"
DB_USER = "justiceflow"
DB_PASS = "justiceflow123"
DB_HOST = "localhost"

def get_db_connection():
    try:
        conn = psycopg2.connect(
            dbname=DB_NAME,
            user=DB_USER,
            password=DB_PASS,
            host=DB_HOST
        )
        return conn
    except Exception as e:
        print(f"[AI: Hotspot] Critical DB Connection Failure: {e}")
        return None

def train_hotspot_model():
    print("[AI: Hotspot] Initializing DBSCAN clustering sequence...")
    conn = get_db_connection()
    if not conn:
        return

    try:
        cur = conn.cursor()
        
        # 1. Fetch geographic data for recent incidents
        # Assuming the database has a lat/long column in the evidence or cases table
        cur.execute("SELECT case_id, latitude, longitude FROM Cases WHERE status != 'CLOSED';")
        rows = cur.fetchall()
        
        if len(rows) < 10:
            print("[AI: Hotspot] Insufficient data for clustering. Waiting for more FIRs.")
            return

        # Extract coordinates into a NumPy array
        coords = np.array([[row[1], row[2]] for row in rows])

        # 2. Run DBSCAN
        # eps is the max distance between two samples to be considered in the same neighborhood
        # min_samples is the number of incidents required to declare a hotspot
        kms_per_radian = 6371.0088
        epsilon = 1.5 / kms_per_radian # 1.5km radius
        
        db = DBSCAN(eps=epsilon, min_samples=5, algorithm='ball_tree', metric='haversine').fit(np.radians(coords))
        
        labels = db.labels_
        num_clusters = len(set(labels)) - (1 if -1 in labels else 0)
        
        print(f"[AI: Hotspot] Detected {num_clusters} high-risk zones.")

        # 3. Process and output the centroids (the center of the hotspot)
        for k in set(labels):
            if k == -1:
                continue # Noise (isolated incidents)

            class_member_mask = (labels == k)
            cluster_points = coords[class_member_mask]
            
            # Calculate the centroid of the cluster
            centroid_lat = np.mean(cluster_points[:, 0])
            centroid_lon = np.mean(cluster_points[:, 1])
            incident_count = len(cluster_points)

            print(f" -> Zone {k}: Lat {centroid_lat:.4f}, Lon {centroid_lon:.4f} | Incidents: {incident_count}")
            
            # TODO: In future steps, we will write these centroids back to the database 
            # or into Abdullah's POSIX shared memory block for the C++ UI to read.

    except Exception as e:
        print(f"[AI: Hotspot] Execution Error: {e}")
    finally:
        cur.close()
        conn.close()

if __name__ == "__main__":
    train_hotspot_model()
