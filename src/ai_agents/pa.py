#!/usr/bin/env python3
import os
import json
import psycopg2
import numpy as np

DB_NAME = os.getenv("JF_DB_NAME", "justiceflow")
DB_USER = os.getenv("JF_DB_USER", "justice_ai")
DB_PASS = os.getenv("JF_DB_PASS", "")
DB_HOST = os.getenv("JF_DB_HOST", "/var/run/postgresql")
DB_PORT = os.getenv("JF_DB_PORT", "5432")

MODEL_VERSION = "RULES-v1.0"
ALGORITHM = "HeuristicScoring"

CASE_TYPE_SEVERITY = {
    "MURDER": 1.00,
    "RAPE": 0.95,
    "KIDNAPPING": 0.90,
    "ROBBERY": 0.75,
    "ASSAULT": 0.70,
    "FRAUD": 0.60,
    "BURGLARY": 0.55,
    "THEFT": 0.45,
}

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
        print(f"[AI: Priority] DB Connection Failed: {e}")
        return None

def clamp01(x: float) -> float:
    return max(0.0, min(1.0, x))

def priority_level(score: float) -> str:
    if score >= 0.75:
        return "HIGH"
    if score >= 0.50:
        return "MEDIUM"
    return "LOW"

def run_priority():
    print("[AI: Priority] Scoring open cases (INSERT-only; matches justice_ai permissions)...")

    conn = get_db_connection()
    if not conn:
        return

    cur = None
    try:
        cur = conn.cursor()

        cur.execute("""
            WITH ev AS (
                SELECT case_id, COUNT(*)::int AS evidence_count
                FROM Evidence
                WHERE is_deleted = FALSE
                GROUP BY case_id
            )
            SELECT
                c.case_id,
                c.case_type::text,
                GREATEST(0, (CURRENT_DATE - c.filed_at::date))::int AS days_open,
                COALESCE(ev.evidence_count, 0) AS evidence_count
            FROM Cases c
            LEFT JOIN ev ON ev.case_id = c.case_id
            WHERE c.case_status <> 'CLOSED';
        """)
        rows = cur.fetchall()

        if not rows:
            print("[AI: Priority] No open cases found.")
            return

        outputs = []
        for case_id, case_type, days_open, evidence_count in rows:
            case_type = str(case_type)

            severity_score = float(CASE_TYPE_SEVERITY.get(case_type, 0.50))
            prior_convictions = 0

            days_norm = clamp01(float(days_open) / 120.0)
            evidence_norm = clamp01(float(evidence_count) / 10.0)
            low_evidence = 1.0 - evidence_norm

            w_sev, w_days, w_low_ev = 0.55, 0.30, 0.15
            score = clamp01(w_sev * severity_score + w_days * days_norm + w_low_ev * low_evidence)
            level = priority_level(score)

            feature_contributions = {
                "severity_score": round(w_sev * severity_score, 6),
                "days_open": round(w_days * days_norm, 6),
                "low_evidence": round(w_low_ev * low_evidence, 6),
                "prior_convictions": 0.0,
            }

            input_features = {
                "case_type": case_type,
                "severity_score": round(severity_score, 4),
                "days_open": int(days_open),
                "evidence_count": int(evidence_count),
                "prior_convictions": int(prior_convictions),
            }

            reason_bits = []
            if severity_score >= 0.75:
                reason_bits.append(f"High severity ({case_type})")
            if days_open >= 60:
                reason_bits.append(f"Open for {days_open} days")
            if evidence_count <= 1:
                reason_bits.append(f"Low evidence ({evidence_count})")
            top_reason = "; ".join(reason_bits) if reason_bits else f"type={case_type}, days_open={days_open}, evidence={evidence_count}"

            suggested_action = (
                "Assign additional investigator immediately"
                if level == "HIGH" else
                "Review within 24 hours"
                if level == "MEDIUM" else
                "Normal workflow"
            )

            # INSERT ONLY (justice_ai has INSERT, not DELETE/UPDATE)
            cur.execute(
                """
                INSERT INTO analytics.Case_Priority_Scores (
                    case_id, priority_level, priority_score,
                    feature_contributions, top_reason, suggested_action,
                    input_features,
                    model_version, algorithm, model_accuracy
                )
                VALUES (
                    %s, %s, %s,
                    %s::jsonb, %s, %s,
                    %s::jsonb,
                    %s, %s, %s
                );
                """,
                (
                    int(case_id),
                    level,
                    round(float(score), 4),
                    json.dumps(feature_contributions),
                    top_reason,
                    suggested_action,
                    json.dumps(input_features),
                    MODEL_VERSION,
                    ALGORITHM,
                    None,
                )
            )

            outputs.append((int(case_id), float(score), case_type, int(days_open), int(evidence_count)))

        conn.commit()

        outputs.sort(key=lambda t: t[1], reverse=True)
        print("[AI: Priority] Top 10 open cases:")
        for cid, s, ctype, d, evc in outputs[:10]:
            print(f" -> Case {cid}: score={s:.3f} level={priority_level(s)} type={ctype} days_open={d} evidence={evc}")

    except Exception as e:
        print(f"[AI: Priority] Execution Error: {e}")
        conn.rollback()
    finally:
        try:
            if cur:
                cur.close()
        finally:
            conn.close()

if __name__ == "__main__":
    run_priority()