#!/usr/bin/env python3
import psycopg2
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score, classification_report
import shap
import joblib
import os
from datetime import datetime

# ---------------------------------------------------------
# Database Configuration
# ---------------------------------------------------------
DB_NAME = "justiceflow"
DB_USER = "justiceflow"
DB_PASS = "justiceflow123"
DB_HOST = "localhost"

MODEL_PATH = "ai/models/priority_rf.pkl"

# Minimal feature set (matches your current DB query)
FEATURE_NAMES = ["days_open", "evidence_count", "severity_score", "prior_convictions"]

def get_db_connection():
    try:
        return psycopg2.connect(dbname=DB_NAME, user=DB_USER, password=DB_PASS, host=DB_HOST)
    except Exception as e:
        print(f"[AI: Priority] DB Connection Failed: {e}")
        return None

def _analytics_table_exists(cur, table_fqtn: str) -> bool:
    try:
        cur.execute("SELECT to_regclass(%s);", (table_fqtn,))
        return cur.fetchone()[0] is not None
    except Exception:
        return False

def _save_priority_outputs_if_possible(cur, outputs):
    """
    Writes to analytics.case_priority only if table exists.
    Expected simple schema:
      analytics.case_priority(
        run_at timestamptz,
        case_id int,
        priority_label int,
        priority_proba double precision,
        shap_top_features text
      )
    """
    if not _analytics_table_exists(cur, "analytics.case_priority"):
        return False

    run_at = datetime.utcnow()
    # Keep it simple: replace the table each run
    cur.execute("DELETE FROM analytics.case_priority;")
    for o in outputs:
        cur.execute(
            """
            INSERT INTO analytics.case_priority(run_at, case_id, priority_label, priority_proba, shap_top_features)
            VALUES (%s, %s, %s, %s, %s);
            """,
            (run_at, o["case_id"], o["priority_label"], o["priority_proba"], o["shap_top_features"])
        )
    return True

def _load_or_train_model(X, y):
    os.makedirs(os.path.dirname(MODEL_PATH), exist_ok=True)

    if os.path.exists(MODEL_PATH):
        try:
            model = joblib.load(MODEL_PATH)
            print(f"[AI: Priority] Loaded cached model from {MODEL_PATH}")
            return model
        except Exception as e:
            print(f"[AI: Priority] Failed to load cached model; retraining. Reason: {e}")

    # Train a small, stable RF (not over-engineered)
    model = RandomForestClassifier(n_estimators=150, max_depth=6, random_state=42)
    model.fit(X, y)
    joblib.dump(model, MODEL_PATH)
    print(f"[AI: Priority] Trained and saved model to {MODEL_PATH}")
    return model

def train_and_score_priority():
    """
    Meets proposal expectations:
      - trains with a basic train/test split and prints accuracy
      - SHAP explainability available
      - can score OPEN cases and (optionally) persist into analytics schema
    """
    print("[AI: Priority] Training/Loading Random Forest + SHAP explainability...")
    conn = get_db_connection()
    if not conn:
        return

    cur = None
    try:
        cur = conn.cursor()

        # 1) Training data from CLOSED cases (historical)
        cur.execute("""
            SELECT days_open, evidence_count, severity_score, prior_convictions, is_high_priority
            FROM Cases
            WHERE status = 'CLOSED'
              AND days_open IS NOT NULL
              AND evidence_count IS NOT NULL
              AND severity_score IS NOT NULL
              AND prior_convictions IS NOT NULL
              AND is_high_priority IS NOT NULL;
        """)
        rows = cur.fetchall()

        if len(rows) < 20:
            print("[AI: Priority] Insufficient historical data. Need at least 20 closed cases with labels.")
            return

        data = np.array(rows, dtype=float)
        X = data[:, :-1]
        y = data[:, -1].astype(int)

        # 2) Simple hold-out evaluation (proposal: accuracy threshold etc.)
        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=0.2, random_state=42, stratify=y if len(set(y)) > 1 else None
        )

        model = _load_or_train_model(X_train, y_train)

        y_pred = model.predict(X_test)
        acc = accuracy_score(y_test, y_pred)
        print(f"[AI: Priority] Hold-out accuracy: {acc:.3f}")
        # Print a compact report (helps instructor verify)
        try:
            print("[AI: Priority] Classification report:")
            print(classification_report(y_test, y_pred, digits=3))
        except Exception:
            pass

        # 3) SHAP explainer (TreeExplainer is appropriate for RF)
        explainer = shap.TreeExplainer(model)

        # 4) Score OPEN cases and produce explanations
        cur.execute("""
            SELECT case_id, days_open, evidence_count, severity_score, prior_convictions
            FROM Cases
            WHERE status != 'CLOSED'
              AND days_open IS NOT NULL
              AND evidence_count IS NOT NULL
              AND severity_score IS NOT NULL
              AND prior_convictions IS NOT NULL;
        """)
        open_rows = cur.fetchall()
        if not open_rows:
            print("[AI: Priority] No open cases found to score.")
            return

        case_ids = [int(r[0]) for r in open_rows]
        X_open = np.array([r[1:] for r in open_rows], dtype=float)

        # Probabilities for "high priority" class (assumes binary labels 0/1)
        if hasattr(model, "predict_proba"):
            proba = model.predict_proba(X_open)[:, 1]
        else:
            # fallback (shouldn't happen for RF)
            proba = model.predict(X_open).astype(float)

        pred_label = (proba >= 0.5).astype(int)

        # SHAP values: for binary RF, shap may return list [class0, class1] or array
        shap_values = explainer.shap_values(X_open)
        if isinstance(shap_values, list) and len(shap_values) == 2:
            shap_for_positive = shap_values[1]
        else:
            shap_for_positive = shap_values

        outputs = []
        for i, cid in enumerate(case_ids):
            # pick top 3 features by absolute SHAP contribution
            contrib = shap_for_positive[i]
            order = np.argsort(np.abs(contrib))[::-1][:3]
            top_feats = ", ".join([f"{FEATURE_NAMES[j]}({contrib[j]:+.3f})" for j in order])

            outputs.append({
                "case_id": cid,
                "priority_label": int(pred_label[i]),
                "priority_proba": float(proba[i]),
                "shap_top_features": top_feats
            })

        # Sort by probability descending (ranked queue)
        outputs.sort(key=lambda o: o["priority_proba"], reverse=True)

        print("[AI: Priority] Top 10 open cases by priority probability:")
        for o in outputs[:10]:
            print(f" -> Case {o['case_id']}: P(high)={o['priority_proba']:.3f} | label={o['priority_label']} | why: {o['shap_top_features']}")

        # Optional persistence into analytics schema if table exists
        wrote = _save_priority_outputs_if_possible(cur, outputs)
        if wrote:
            conn.commit()
            print("[AI: Priority] Saved outputs to analytics.case_priority")
        else:
            print("[AI: Priority] analytics.case_priority not found; skipping DB write (printing only).")

    except Exception as e:
        print(f"[AI: Priority] Execution Error: {e}")
    finally:
        try:
            if cur:
                cur.close()
        finally:
            conn.close()

if __name__ == "__main__":
    train_and_score_priority()