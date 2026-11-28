#!/usr/bin/env bash
# Run the whole JusticeFlow project with one command.
#
#   ./run_justiceflow.sh         install what is missing, set up, build and start
#   ./run_justiceflow.sh stop    stop the daemon and the dashboard
#
# Output is saved in logs/, as are the build, daemon and dashboard logs.
# Asks for input only where it is needed. Passwords are never written to disk.
# Run it as your normal user; it calls sudo itself where root is required.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${ROOT}"

VENV="${ROOT}/.venv"
DB_NAME="justiceflow"

info() { printf '\n\033[1;34m== %s ==\033[0m\n' "$*"; }
source "${ROOT}/scripts/common.sh"
log_run run "${ROOT}/run_justiceflow.sh" "$@"

case "${1:-start}" in
  start) ;;
  stop) exec "${ROOT}/scripts/demo.sh" stop ;;
  *) echo "usage: $0 [start|stop]" >&2; exit 2 ;;
esac

[[ ${EUID} -ne 0 ]] || die "run this as your normal user, not with sudo"

# ── 1. System packages ───────────────────────────────────────────────────────
info "System packages"
packages=(build-essential libpq-dev libreadline-dev python3-venv postgresql-client curl)
command -v pg_lsclusters >/dev/null || packages+=(postgresql)
missing=()
for pkg in "${packages[@]}"; do
  [[ "$(dpkg-query -W -f='${Status}' "${pkg}" 2>/dev/null)" == *" ok installed" ]] || missing+=("${pkg}")
done
if ((${#missing[@]})); then
  echo "Missing: ${missing[*]}"
  confirm "Install them with sudo apt-get?" || die "cannot continue without: ${missing[*]}"
  sudo apt-get update
  sudo apt-get install -y "${missing[@]}"
else
  echo "all present"
fi

# ── 2. PostgreSQL server ─────────────────────────────────────────────────────
info "PostgreSQL"
start_postgres

# ── 3. Database admin login ──────────────────────────────────────────────────
info "Database admin account"
export PGDATABASE="${DB_NAME}"
db_login

# ── 4. Database contents ─────────────────────────────────────────────────────
info "Database"
db_exists="$(psql -d postgres -Atc "select 1 from pg_database where datname = '${DB_NAME}'")"
tables=0
seeded=""
if [[ "${db_exists}" == "1" ]]; then
  tables="$(psql -Atc "select count(*) from pg_tables where schemaname not in ('pg_catalog', 'information_schema')")"
  if [[ -n "$(psql -Atc "select to_regclass('public.officers')")" ]]; then
    seeded="$(psql -Atc 'select 1 from public.officers limit 1')"
  fi
fi

export JF_KEEP_DB=0
if [[ "${tables}" != "0" ]]; then
  echo "Database '${DB_NAME}' already has ${tables} tables."
  if confirm "Erase it and load fresh demo data?"; then
    JF_KEEP_DB=0
  else
    [[ "${seeded}" == "1" ]] || die "the existing database has no officers, so it cannot be used as it is. Run again and answer y to rebuild it."
    JF_KEEP_DB=1
    echo "keeping the existing data"
  fi
else
  echo "'${DB_NAME}' will be created and filled with demo data"
fi

# ── 5. Python environment ────────────────────────────────────────────────────
info "Python packages"
[[ -x "${VENV}/bin/pip" ]] || python3 -m venv "${VENV}"
# shellcheck disable=SC1091
source "${VENV}/bin/activate"
stamp="${VENV}/.requirements.sha256"
wanted="$(cat requirements.txt dashboard/requirements.txt | sha256sum)"
if [[ "$(cat "${stamp}" 2>/dev/null)" != "${wanted}" ]]; then
  pip install -r requirements.txt -r dashboard/requirements.txt
  echo "${wanted}" >"${stamp}"
else
  echo "up to date"
fi

# ── 6. Build and start ───────────────────────────────────────────────────────
info "Dashboard port"
while :; do
  ask port "Port for the web dashboard" "${JF_DASHBOARD_PORT:-5000}"
  [[ "${port}" =~ ^[0-9]{1,5}$ ]] && ((10#${port} >= 1024 && 10#${port} <= 65535)) && break
  echo "enter a number from 1024 to 65535"
done
export JF_DASHBOARD_PORT="${port}"

info "Build and start"
"${ROOT}/scripts/demo.sh" start
echo "To stop everything: ./run_justiceflow.sh stop"
