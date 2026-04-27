#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${REPO_ROOT}/.venv-ai-agents"

# --- Writer (AI agent) DB connection ---
: "${PGHOST:=/var/run/postgresql}"
: "${PGPORT:=5432}"
: "${PGDATABASE:=justiceflow}"
: "${PGUSER:=justice_ai}"
: "${PGPASSWORD:=justiceflow123}"

# --- Reader (dashboard/app) DB connection for printing output ---
: "${PGREADUSER:=justice_app}"
: "${PGREADPASSWORD:=justice_app}"

export PGHOST PGPORT PGDATABASE PGUSER PGPASSWORD

WORKLOAD_AGENT="${REPO_ROOT}/src/ai_agents/workload_agent.py"
HOTSPOT_AGENT="${REPO_ROOT}/src/ai_agents/ha.py"
PRIORITY_AGENT="${REPO_ROOT}/src/ai_agents/pa.py"

die() { echo "[!] $*" >&2; exit 1; }

echo "[*] Repo root: ${REPO_ROOT}"
echo "[*] WRITE DB:  ${PGUSER}@${PGHOST}:${PGPORT}/${PGDATABASE}"
echo "[*] READ DB:   ${PGREADUSER}@${PGHOST}:${PGPORT}/${PGDATABASE}"
echo ""

echo "Select an agent to run:"
echo "  1) workload_agent   (${WORKLOAD_AGENT})"
echo "  2) hotspot_agent    (${HOTSPOT_AGENT})"
echo "  3) priority_agent   (${PRIORITY_AGENT})"
echo -n "Enter choice [1-3]: "
read -r CHOICE

AGENT=""
AGENT_NAME=""

case "${CHOICE}" in
  1) AGENT="${WORKLOAD_AGENT}"; AGENT_NAME="workload_agent" ;;
  2) AGENT="${HOTSPOT_AGENT}";  AGENT_NAME="hotspot_agent" ;;
  3) AGENT="${PRIORITY_AGENT}"; AGENT_NAME="priority_agent" ;;
  *) die "Invalid choice: ${CHOICE}" ;;
esac

[[ -f "${AGENT}" ]] || die "Agent file not found: ${AGENT}"

echo ""
echo "[*] Selected: ${AGENT_NAME}"
echo "[*] Agent:    ${AGENT}"

if [[ ! -d "${VENV_DIR}" ]]; then
  echo "[*] Creating venv: ${VENV_DIR}"
  python3 -m venv "${VENV_DIR}"
fi

# shellcheck source=/dev/null
source "${VENV_DIR}/bin/activate"

echo "[*] Upgrading pip tooling"
python -m pip install --upgrade pip wheel setuptools >/dev/null

echo "[*] Installing python dependencies (shared venv)"
# Superset deps used across agents in this repo (safe baseline)
python -m pip install -q numpy psycopg2-binary scipy pandas scikit-learn

echo "[*] Running ${AGENT_NAME} (logs are on stderr)"
set +e
python "${AGENT}"
RC=$?
set -e

echo "[*] ${AGENT_NAME} exit code: ${RC}"
echo ""

if command -v psql >/dev/null 2>&1; then
  case "${AGENT_NAME}" in
    workload_agent)
      echo "[*] Latest suggestions (analytics.Officer_Workload_Assignments) — read as ${PGREADUSER}"
      PGPASSWORD="${PGREADPASSWORD}" psql \
        -h "${PGHOST}" -p "${PGPORT}" -U "${PGREADUSER}" -d "${PGDATABASE}" \
        -c "SELECT assignment_id, case_id, officer_id, assignment_status, cost_score,
                   expires_at, analyzed_at, model_version, algorithm
            FROM analytics.Officer_Workload_Assignments
            ORDER BY analyzed_at DESC
            LIMIT 20;"
      ;;
    hotspot_agent)
      echo "[*] Latest hotspots (analytics.Crime_Hotspots) — read as ${PGREADUSER}"
      PGPASSWORD="${PGREADPASSWORD}" psql \
        -h "${PGHOST}" -p "${PGPORT}" -U "${PGREADUSER}" -d "${PGDATABASE}" \
        -c "SELECT hotspot_id, zone_label, risk_level, risk_score,
                   case_count, dominant_case_type,
                   analyzed_at, model_version, algorithm
            FROM analytics.Crime_Hotspots
            ORDER BY analyzed_at DESC
            LIMIT 20;"
      ;;
    priority_agent)
      echo "[*] Latest priority scores (analytics.Case_Priority_Scores) — read as ${PGREADUSER}"
      PGPASSWORD="${PGREADPASSWORD}" psql \
        -h "${PGHOST}" -p "${PGPORT}" -U "${PGREADUSER}" -d "${PGDATABASE}" \
        -c "SELECT score_id, case_id, priority_level, priority_score,
                   analyzed_at, model_version, algorithm
            FROM analytics.Case_Priority_Scores
            ORDER BY analyzed_at DESC
            LIMIT 20;"
      ;;
  esac
else
  echo "[!] psql not found; install postgresql-client to print DB results automatically."
fi

echo ""
echo "[*] Done."
exit "${RC}"