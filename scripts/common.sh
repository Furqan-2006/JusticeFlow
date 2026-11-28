# shellcheck shell=bash
# Prompts and the PostgreSQL admin login shared by install.sh and
# run_justiceflow.sh. Source this file; do not run it.

SOCKET_DIR="/var/run/postgresql"

die()  { printf '\033[0;31merror: %s\033[0m\n' "$*" >&2; exit 1; }

# ask VARIABLE "Question" "default"
ask() {
  local reply
  read -r -p "$2 [$3]: " reply || die "no input (run this in a terminal)"
  printf -v "$1" '%s' "${reply:-$3}"
}

# confirm "Question": true only on an explicit yes
confirm() {
  local reply
  read -r -p "$1 [y/N]: " reply || return 1
  [[ "${reply}" =~ ^[Yy]([Ee][Ss])?$ ]]
}

start_postgres() {
  if ! pg_isready -q -h "${SOCKET_DIR}"; then
    echo "starting PostgreSQL (needs sudo)"
    sudo systemctl start postgresql
    for _ in $(seq 1 50); do pg_isready -q -h "${SOCKET_DIR}" && break; sleep 0.2; done
  fi
  pg_isready -h "${SOCKET_DIR}" || die "PostgreSQL is not accepting connections on ${SOCKET_DIR}"
}

is_superuser() {
  [[ "$(psql -w -d postgres -Atc 'select rolsuper from pg_roles where rolname = current_user' 2>/dev/null)" == "t" ]]
}

# Ask for a PostgreSQL superuser and export PGHOST / PGUSER / PGPASSWORD for it.
db_login() {
  local admin="" password=""
  echo "A PostgreSQL superuser is needed to create and seed the database."
  ask admin "PostgreSQL admin user" "${PGUSER:-${USER}}"
  [[ "${admin}" =~ ^[A-Za-z_][A-Za-z0-9_-]*$ ]] || die "unsupported user name: ${admin}"

  export PGUSER="${admin}" PGHOST="${SOCKET_DIR}"

  # Your own Linux account can log in through the local socket without a password.
  if ! is_superuser && [[ "${admin}" == "${USER}" ]]; then
    echo "'${admin}' cannot yet log in to PostgreSQL as a superuser."
    if confirm "Create the PostgreSQL superuser '${admin}' with sudo (local development machines only)?"; then
      sudo -u postgres createuser --superuser "${admin}" \
        || die "could not create '${admin}'. If it already exists without superuser rights, choose another admin user."
    fi
  fi

  # Any other account logs in over TCP with its password.
  if ! is_superuser; then
    export PGHOST="127.0.0.1"
    read -r -s -p "Password for PostgreSQL user '${admin}': " password || die "no password given"
    echo
    export PGPASSWORD="${password}"
    is_superuser || die "could not log in as superuser '${admin}' (wrong password, or not a superuser)"
  fi
  echo "connected as ${admin}"
}

# log_run NAME SCRIPT [ARGS...]
# Re-run SCRIPT with its output also saved to logs/NAME-<time>.log, then exit
# with its status. Returns at once inside that re-run. Typed input (including
# passwords) is not recorded.
log_run() {
  [[ -z "${JF_LOG_STAMP:-}" ]] || return 0
  local name="$1" script="$2" dir file rc
  shift 2
  dir="$(dirname "${script}")/logs"
  mkdir -p "${dir}"
  JF_LOG_STAMP="$(date +%Y%m%d-%H%M%S)"
  export JF_LOG_STAMP
  file="${dir}/${name}-${JF_LOG_STAMP}.log"
  echo "Logging to ${file}"
  set +e
  trap - ERR
  # tee -i: keep recording while the script handles Ctrl-C.
  "${script}" "$@" 2>&1 | tee -i "${file}"
  rc=${PIPESTATUS[0]}
  sed -i 's/\x1b\[[0-9;]*m//g' "${file}"   # drop terminal colour codes
  exit "${rc}"
}
