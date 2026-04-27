#!/usr/bin/env python3
import psycopg2
import numpy as np
from sklearn.ensemble import RandomForestClassifier
import shap
import joblib
import os

# Database Configuration

DB_NAME = "justiceflow"
DB_USER = "justiceflow"
DB_PASS = "justiceflow123"
DB_HOST = "localhost"

def get_db_connection():
    try:
        return psycopg2.connect(dbname=DB_NAME, user=DB_USER, password=DB_PASS, host=DB_HOST)
    except Exception as e:
        print(f"[AI: Priority] DB Connection Failed: {e}")
        return None

def train_priority_model():
    print("[AI: Priority] Initializing Random Forest & SHAP Explainer...")
    conn = get_db_connection()
    if not conn:
        return

    try:
        cur = conn.cursor()
        
        # 1. Fetch training data
        # Features: [days_open, evidence_count, severity_score, prior_convictions]
        # Target: [is_high_priority] (0 or 1)
        cur.execute("SELECT days_open, evidence_count, severity_score, prior_convictions, is_high_priority FROM Cases WHERE status = 'CLOSED';")
        rows = cur.fetchall()
        
        if len(rows) < 20:
            print("[AI: Priority] Insufficient historical data to train the Random Forest. (Need at least 20 closed cases).")
            return

        # Prepare arrays
        data = np.array(rows)
        X = data[:, :-1]  # The first 4 columns are our features
        y = data[:, -1]   # The last column is our target label

        # 2. Train the Random Forest
        rf_model = RandomForestClassifier(n_estimators=100, max_depth=5, random_state=42)
        rf_model.fit(X, y)
        print("[AI: Priority] Random Forest trained successfully.")

        # 3. Serialize (save) the model so it doesn't retrain on every boot
        os.makedirs("ai/models", exist_ok=True)
        joblib.dump(rf_model, "ai/models/priority_rf.pkl")
        print("[AI: Priority] Model saved to ai/models/priority_rf.pkl")

        # 4. Initialize the SHAP Explainer
        # This allows us to say: "Priority is HIGH because evidence_count=4 (+0.3) and days_open=120 (+0.5)"
        explainer = shap.TreeExplainer(rf_model)
        
        # Test it on the first case just to verify it works
        shap_values = explainer.shap_values(X[0:1])
        print(f"[AI: Priority] SHAP initialization complete. Model is ready for inference via shared memory.")

    except Exception as e:
        print(f"[AI: Priority] Execution Error: {e}")
    finally:
        cur.close()
        conn.close()

if __name__ == "__main__":
    train_priority_model()
