#!/usr/bin/env bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

BUILD_MODE="debug"
DEBUG_MODE=0
SKIP_SETUP=0
SKIP_TESTS=0
SEED_DATA=0
REPO_URL=""

DEFAULT_REPO_URL="https://github.com/Furqan-2006/JusticeFlow.git"
PROJECT_DIR=""
EXECUTABLE_PATH="build/justiceflow"

print_header() { echo -e "\n${BLUE}${BOLD}== $1 ==${NC}"; }
print_ok() { echo -e "${GREEN}✔ $1${NC}"; }
print_info() { echo -e "${CYAN}➜ $1${NC}"; }
print_warn() { echo -e "${YELLOW}⚠ $1${NC}"; }
print_error() { echo -e "${RED}✘ $1${NC}"; }

usage() {
  cat <<EOF
Usage: ./install.sh [options]

Options:
  --debug            Keep verbose diagnostics
  --release          Build with BUILD_MODE=release
  --skip-setup       Skip justiceflow_setup.sh
  --skip-tests       Skip make test
  --seed-data        Fill a new application database with synthetic data
  --repo-url <url>   Clone from custom repository URL
  --help             Show this help message
EOF
}

validate_repo_url() {
  local url="$1"
  case "$url" in
    https://*|git@*|ssh://*) return 0 ;;
    *)
      print_error "Invalid repository URL: $url"
      print_error "Use a URL starting with https://, ssh://, or git@"
      return 1
      ;;
  esac
}

on_error() {
  local line_no="$1"
  print_error "Installation failed at line ${line_no}."
  echo -e "${YELLOW}Suggestions:${NC}"
  echo "  1) Re-run with --debug to see detailed output"
  echo "  2) Verify PostgreSQL is running: pg_isready"
  echo "  3) Try: make verify-deps"
  if [[ "$DEBUG_MODE" -eq 0 ]]; then
    print_warn "No rollback was performed automatically. You can run: make clean"
  fi
  exit 1
}

trap 'on_error $LINENO' ERR

ARGS=("$@")

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug) DEBUG_MODE=1 ;;
    --release) BUILD_MODE="release" ;;
    --skip-setup) SKIP_SETUP=1 ;;
    --skip-tests) SKIP_TESTS=1 ;;
    --seed-data) SEED_DATA=1 ;;
    --repo-url)
      shift
      if [[ $# -eq 0 ]]; then
        print_error "--repo-url requires a value"
        exit 1
      fi
      REPO_URL="$1"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      print_error "Unknown option: $1"
      usage
      exit 1
      ;;
  esac
  shift
done

print_header "JusticeFlow Installer"

if [[ -f "./Makefile" && -f "./justiceflow_setup.sh" ]]; then
  PROJECT_DIR="$(pwd)"
  print_ok "Using existing repository at $PROJECT_DIR"
else
  print_warn "JusticeFlow repository not detected in current directory."
  clone_url="${REPO_URL:-$DEFAULT_REPO_URL}"
  read -r -p "Repository URL [${clone_url}]: " input_url
  clone_url="${input_url:-$clone_url}"
  validate_repo_url "$clone_url"
  read -r -p "Clone destination [JusticeFlow]: " clone_dir
  clone_dir="${clone_dir:-JusticeFlow}"

  print_info "Cloning repository..."
  git clone "$clone_url" "$clone_dir"
  PROJECT_DIR="$(cd "$clone_dir" && pwd)"
  print_ok "Repository cloned to $PROJECT_DIR"
fi

cd "$PROJECT_DIR"
# shellcheck source=scripts/common.sh
source "$PROJECT_DIR/scripts/common.sh"
[[ "$EUID" -ne 0 ]] || die "run this as your normal user; it calls sudo itself"
# Everything from here on is also saved to logs/install-<time>.log
log_run install "$PROJECT_DIR/install.sh" ${ARGS[@]+"${ARGS[@]}"}

if [[ "$SKIP_SETUP" -eq 0 ]]; then
  print_header "System Prerequisites"
  if [[ ! -f "./justiceflow_setup.sh" ]]; then
    print_error "Required script not found: ./justiceflow_setup.sh"
    exit 1
  fi
  if [[ ! -x "./justiceflow_setup.sh" ]]; then
    chmod +x ./justiceflow_setup.sh
  fi
  print_info "Running justiceflow_setup.sh with sudo for system package installation"
  sudo ./justiceflow_setup.sh
else
  print_warn "Skipping justiceflow_setup.sh (--skip-setup)"
fi

print_header "Dependency Verification"
make verify-deps

print_header "Python Packages"
make setup-python
make setup-ai

print_header "Build"
make build BUILD_MODE="$BUILD_MODE"

print_header "Database"
start_postgres
export PGDATABASE=justiceflow
db_login
tables=0
if [[ "$(psql -d postgres -Atc "select 1 from pg_database where datname='justiceflow'")" == "1" ]]; then
  tables="$(psql -Atc "select count(*) from pg_tables where schemaname not in ('pg_catalog', 'information_schema')")"
fi
if [[ "$tables" != "0" ]]; then
  print_warn "Database 'justiceflow' already has tables; leaving it as it is"
elif [[ "$SEED_DATA" -eq 1 ]]; then
  make setup-db SEED_DATA=1
else
  make init-db
fi

if [[ "$SKIP_TESTS" -eq 0 ]]; then
  print_header "Tests"
  print_info "Rebuilding the justiceflow_test database"
  make test-db
  make test BUILD_MODE="$BUILD_MODE"
else
  print_warn "Skipping tests (--skip-tests)"
fi

print_header "Validation"
if [[ -x "$EXECUTABLE_PATH" ]]; then
  print_ok "Binary created: $EXECUTABLE_PATH"
else
  print_error "Expected binary not found at $EXECUTABLE_PATH"
  exit 1
fi

print_header "Installation Complete"
echo -e "${GREEN}${BOLD}JusticeFlow setup finished successfully.${NC}"
echo "Next step: ./run_justiceflow.sh   (starts the daemon and the dashboard)"
