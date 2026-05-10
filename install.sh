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
  --seed-data        Enable database synthetic data generation
  --repo-url <url>   Clone from custom repository URL
  --help             Show this help message
EOF
}

on_error() {
  local line_no="$1"
  print_error "Installation failed at line ${line_no}."
  echo -e "${YELLOW}Suggestions:${NC}"
  echo "  1) Re-run with --debug to see detailed output"
  echo "  2) Verify PostgreSQL is running and credentials are correct"
  echo "  3) Try: make verify-deps"
  if [[ "$DEBUG_MODE" -eq 0 ]]; then
    print_warn "No rollback was performed automatically. You can run: make clean"
  fi
  exit 1
}

trap 'on_error $LINENO' ERR

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
  read -r -p "Clone destination [JusticeFlow]: " clone_dir
  clone_dir="${clone_dir:-JusticeFlow}"

  print_info "Cloning repository..."
  git clone "$clone_url" "$clone_dir"
  PROJECT_DIR="$(cd "$clone_dir" && pwd)"
  print_ok "Repository cloned to $PROJECT_DIR"
fi

cd "$PROJECT_DIR"

if [[ "$SKIP_SETUP" -eq 0 ]]; then
  print_header "System Prerequisites"
  if [[ "$EUID" -eq 0 ]]; then
    ./justiceflow_setup.sh
  else
    sudo ./justiceflow_setup.sh
  fi
else
  print_warn "Skipping justiceflow_setup.sh (--skip-setup)"
fi

print_header "Dependency Verification"
make verify-deps

print_header "Database + Python Setup"
make setup-all SEED_DATA=0
if [[ "$SEED_DATA" -eq 1 ]]; then
  print_header "Synthetic Data Seeding"
  make seed-db SEED_DATA=1
fi

print_header "Build"
make build BUILD_MODE="$BUILD_MODE"

if [[ "$SKIP_TESTS" -eq 0 ]]; then
  print_header "Tests"
  make test BUILD_MODE="$BUILD_MODE"
else
  print_warn "Skipping tests (--skip-tests)"
fi

print_header "Validation"
if [[ -x "build/justiceflow" ]]; then
  print_ok "Binary created: build/justiceflow"
else
  print_error "Expected binary not found at build/justiceflow"
  exit 1
fi

print_header "Installation Complete"
echo -e "${GREEN}${BOLD}JusticeFlow setup finished successfully.${NC}"
echo "Next steps:"
echo "  1) Configure database credentials (PGHOST/PGDATABASE/PGUSER as needed)"
echo "  2) Run backend: ./build/justiceflow"
echo "  3) Start dashboard (if needed): cd dashboard && python3 run.py"
