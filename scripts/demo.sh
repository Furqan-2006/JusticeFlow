#!/usr/bin/env bash
# Start the whole JusticeFlow stack: database, daemon, dashboard.
#
#   scripts/demo.sh start   reset the database, build, start daemon + dashboard
#   scripts/demo.sh stop    stop both
#
# Set JF_KEEP_DB=1 to start without resetting the database.
#
# Database admin access (to create and seed the database) comes from the usual
# PGHOST / PGUSER / PGPASSWORD. The daemon and the AI agents connect as the
# application roles the schema creates; their development passwords are the
# defaults below. Override them (JF_DB_PASS, JF_AI_DB_PASS) for anything that
# is not a throwaway machine.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_DIR="${ROOT}/.demo"
DAEMON_PID="${RUN_DIR}/daemon.pid"
DASH_PID="${RUN_DIR}/dashboard.pid"
mkdir -p "${RUN_DIR}"
# PID files live in .demo/, logs in logs/ (one set per start).
LOG_DIR="${ROOT}/logs"
STAMP="${JF_LOG_STAMP:-$(date +%Y%m%d-%H%M%S)}"
BUILD_LOG="${LOG_DIR}/build-${STAMP}.log"
DB_LOG="${LOG_DIR}/db-reset-${STAMP}.log"
DAEMON_LOG="${LOG_DIR}/daemon-${STAMP}.log"
DASH_LOG="${LOG_DIR}/dashboard-${STAMP}.log"

# Use the project virtualenv if there is one (`make setup-python`).
if [[ -f "${ROOT}/.venv/bin/activate" ]]; then
  source "${ROOT}/.venv/bin/activate"
fi

PORT="${JF_DASHBOARD_PORT:-5000}"

stop_one() {
  local pidfile="$1" name="$2" pid
  pid="$(cat "${pidfile}" 2>/dev/null || true)"
  if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
    kill -TERM "${pid}" 2>/dev/null || true
    for _ in $(seq 1 50); do kill -0 "${pid}" 2>/dev/null || break; sleep 0.1; done
    kill -KILL "${pid}" 2>/dev/null || true
    echo "stopped ${name}"
  fi
  rm -f "${pidfile}"
}

stop() {
  stop_one "${DASH_PID}" "dashboard"
  stop_one "${DAEMON_PID}" "daemon"
}

start() {
  stop
  mkdir -p "${LOG_DIR}"
  for cmd in psql python3 make; do
    command -v "${cmd}" >/dev/null || { echo "missing: ${cmd}" >&2; exit 1; }
  done
  psql -d postgres -Atc "select 1" >/dev/null 2>&1 || {
    echo "Cannot reach PostgreSQL as an admin. Start it and set PGHOST / PGUSER / PGPASSWORD." >&2
    exit 1
  }

  if [[ "${JF_KEEP_DB:-0}" == "1" ]]; then
    echo "[demo] keeping the existing database"
  else
    echo "[demo] resetting database"
    make -C "${ROOT}" --no-print-directory db-reset >"${DB_LOG}" 2>&1 \
      || { tail -20 "${DB_LOG}"; exit 1; }
  fi

  echo "[demo] building"
  make -C "${ROOT}" --no-print-directory build >"${BUILD_LOG}" 2>&1 \
    || { tail -20 "${BUILD_LOG}"; exit 1; }

  export JF_DB_HOST="${JF_DB_HOST:-127.0.0.1}"
  export JF_DB_USER="${JF_DB_USER:-justice_app}"
  export JF_DB_PASS="${JF_DB_PASS:-justice_app}"
  export JF_AI_DB_PASS="${JF_AI_DB_PASS:-justice_ai}"
  export JF_AI_DB_HOST="${JF_AI_DB_HOST:-${JF_DB_HOST}}"
  export JF_AGENT_SCRIPT_DIR="${ROOT}/src/ai_agents"
  export JF_EXPIRE_INTERVAL_S="${JF_EXPIRE_INTERVAL_S:-5}"
  export JF_DASHBOARD_SECRET="${JF_DASHBOARD_SECRET:-$(python3 -c 'import secrets;print(secrets.token_hex(32))')}"

  rm -f /tmp/justiceflow.sock
  echo "[demo] starting daemon"
  (cd "${ROOT}" && exec setsid -f ./build/justiceflow --headless --pid-file "${DAEMON_PID}" \
      >"${DAEMON_LOG}" 2>&1 </dev/null)
  for _ in $(seq 1 50); do [[ -S /tmp/justiceflow.sock ]] && break; sleep 0.2; done
  [[ -S /tmp/justiceflow.sock ]] || { echo "daemon did not start; see ${DAEMON_LOG}" >&2; tail -20 "${DAEMON_LOG}"; exit 1; }

  if curl -fsS "http://127.0.0.1:${PORT}/healthz" >/dev/null 2>&1; then
    echo "port ${PORT} is already serving something; stop it or set JF_DASHBOARD_PORT" >&2
    exit 1
  fi

  echo "[demo] starting dashboard"
  (cd "${ROOT}/dashboard" && export JF_DASHBOARD_PORT="${PORT}" && exec setsid -f \
      sh -c 'echo $$ >"$1"; exec python3 run.py' sh "${DASH_PID}" \
      >"${DASH_LOG}" 2>&1 </dev/null)
  for _ in $(seq 1 50); do
    curl -fsS "http://127.0.0.1:${PORT}/healthz" >/dev/null 2>&1 && break; sleep 0.2
  done
  curl -fsS "http://127.0.0.1:${PORT}/healthz" >/dev/null 2>&1 \
    || { echo "dashboard did not start; see ${DASH_LOG}" >&2; tail -20 "${DASH_LOG}"; exit 1; }

  local officer
  officer="$(psql -d "${PGDATABASE:-justiceflow}" -Atc "select cnic || '  (' || current_rank || ')' from officers where status='ACTIVE' and current_rank='INSPECTOR' order by officer_id limit 1")"
  cat <<MSG

JusticeFlow is running.
  Dashboard   http://127.0.0.1:${PORT}
  Sign in     ${officer}   password: justice123
  Logs        ${LOG_DIR}/
  Check it    make smoke
  Stop        make demo-stop
MSG
}

case "${1:-start}" in
  start) start ;;
  stop) stop ;;
  *) echo "usage: $0 start|stop" >&2; exit 2 ;;
esac
