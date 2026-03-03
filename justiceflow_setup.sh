#!/bin/bash
# =============================================================================
#  JusticeFlow — Full Environment Setup Script
#  Police Case & Evidence Management System
#  Multi-Course: DBMS · SDA · AI · OS
# =============================================================================
#  Usage:   chmod +x justiceflow_setup.sh && sudo ./justiceflow_setup.sh
#  Target:  Ubuntu 22.04 / 24.04 LTS
# =============================================================================

set -e  # Exit immediately on any error

# ── Colour helpers ────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'  # No Color

print_header() { echo -e "\n${BLUE}${BOLD}══════════════════════════════════════════${NC}"; \
                  echo -e "${BLUE}${BOLD}  $1${NC}"; \
                  echo -e "${BLUE}${BOLD}══════════════════════════════════════════${NC}"; }
print_ok()      { echo -e "  ${GREEN}✔  $1${NC}"; }
print_skip()    { echo -e "  ${YELLOW}⊙  $1 — already installed, skipping${NC}"; }
print_info()    { echo -e "  ${CYAN}➜  $1${NC}"; }
print_error()   { echo -e "  ${RED}✘  $1${NC}"; }

# ── Root check ────────────────────────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    print_error "This script must be run as root (use: sudo ./justiceflow_setup.sh)"
    exit 1
fi

# ── Helper: check if an apt package is installed ──────────────────────────────
apt_installed() { dpkg -s "$1" &>/dev/null; }

# ── Helper: check if a Python package is installed ───────────────────────────
py_installed()  { python3 -c "import $1" &>/dev/null 2>&1; }

# ── Helper: install apt package only if missing ───────────────────────────────
apt_install() {
    local pkg="$1"
    local label="${2:-$1}"
    if apt_installed "$pkg"; then
        print_skip "$label"
    else
        print_info  "Installing $label …"
        apt-get install -y "$pkg" -qq
        print_ok "$label installed"
    fi
}

# ── Helper: install pip package only if missing ───────────────────────────────
pip_install() {
    local import_name="$1"
    local pip_name="${2:-$1}"
    local label="${3:-$pip_name}"
    if py_installed "$import_name"; then
        print_skip "$label (Python)"
    else
        print_info  "Installing Python: $label …"
        pip3 install --quiet --break-system-packages "$pip_name"
        print_ok "$label installed"
    fi
}

# =============================================================================
echo -e "\n${BOLD}${CYAN}"
echo "  ___           _   _           _____ _"
echo " |_  |         | | (_)         |  ___| |"
echo "   | |_   _ ___| |_ _  ___ ___ | |_  | | _____      __"
echo "   | | | | / __| __| |/ __/ _ \|  _| | |/ _ \ \ /\ / /"
echo "  _| | |_| \__ \ |_| | (_|  __/| |   | | (_) \ V  V /"
echo " \___/\__,_|___/\__|_|\___\___|\\_|   |_|\___/ \_/\_/"
echo -e "${NC}"
echo -e "  ${BOLD}JusticeFlow — Environment Setup${NC}"
echo -e "  Multi-Course: DBMS · SDA · AI · OS\n"

# =============================================================================
print_header "STEP 1 — System Update & Base Tools"
# =============================================================================
print_info "Updating apt package lists …"
apt-get update -qq
print_ok "Package lists updated"

apt_install "curl"         "cURL"
apt_install "wget"         "wget"
apt_install "gnupg"        "GnuPG"
apt_install "lsb-release"  "lsb-release"
apt_install "ca-certificates" "CA Certificates"
apt_install "software-properties-common" "software-properties-common"

# =============================================================================
print_header "STEP 2 — Build Essentials (SDA / OS Modules)"
# =============================================================================
apt_install "build-essential"   "Build Essential (gcc, g++, make)"
apt_install "g++"               "G++ (C++17 compiler)"
apt_install "make"              "GNU Make"
apt_install "cmake"             "CMake"
apt_install "gdb"               "GDB Debugger"
apt_install "valgrind"          "Valgrind (memory leak checker)"
apt_install "strace"            "strace (syscall tracer)"
apt_install "ltrace"            "ltrace (library call tracer)"
apt_install "clang-format"      "clang-format (code formatter)"
apt_install "doxygen"           "Doxygen (code documentation)"
apt_install "graphviz"          "Graphviz (Doxygen diagrams)"

# Verify g++ version
GXX_VER=$(g++ --version | head -n1)
print_info "Active compiler: $GXX_VER"

# =============================================================================
print_header "STEP 3 — PostgreSQL (DBMS Module)"
# =============================================================================
if apt_installed "postgresql"; then
    print_skip "PostgreSQL server"
else
    print_info "Adding PostgreSQL APT repository …"
    install -d /usr/share/postgresql-common/pgdg
    curl -fsSL https://www.postgresql.org/media/keys/ACCC4CF8.asc \
        -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc
    sh -c 'echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] \
        https://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" \
        > /etc/apt/sources.list.d/pgdg.list'
    apt-get update -qq
    apt-get install -y postgresql postgresql-contrib -qq
    print_ok "PostgreSQL installed"
fi

apt_install "postgresql-client"     "PostgreSQL client (psql)"
apt_install "libpq-dev"             "libpq-dev (C client headers for psycopg2)"
apt_install "pgadmin4"              "pgAdmin 4 (GUI)" || true  # optional, may not be in repo

# Start & enable PostgreSQL service
if systemctl is-active --quiet postgresql; then
    print_skip "PostgreSQL service (already running)"
else
    systemctl start postgresql
    systemctl enable postgresql --quiet
    print_ok "PostgreSQL service started & enabled"
fi

PG_VER=$(psql --version 2>/dev/null || echo "unknown")
print_info "PostgreSQL version: $PG_VER"

# =============================================================================
print_header "STEP 4 — Python 3 & pip (AI Module)"
# =============================================================================
apt_install "python3"        "Python 3"
apt_install "python3-pip"    "pip3"
apt_install "python3-dev"    "python3-dev (headers)"
apt_install "python3-venv"   "python3-venv"

PY_VER=$(python3 --version 2>&1)
print_info "Python version: $PY_VER"

# Core ML / Data Science libraries
pip_install "sklearn"        "scikit-learn"     "scikit-learn (ML models)"
pip_install "numpy"          "numpy"            "NumPy (numerical computing)"
pip_install "pandas"         "pandas"           "Pandas (data frames)"
pip_install "scipy"          "scipy"            "SciPy (Hungarian algorithm)"
pip_install "matplotlib"     "matplotlib"       "Matplotlib (heatmap / plots)"
pip_install "seaborn"        "seaborn"          "Seaborn (statistical plots)"

# Database connectivity
pip_install "psycopg2"       "psycopg2-binary"  "psycopg2 (PostgreSQL → Python)"
pip_install "sqlalchemy"     "SQLAlchemy"       "SQLAlchemy (ORM)"

# AI explainability
pip_install "shap"           "shap"             "SHAP (model explainability)"

# Synthetic data generation
pip_install "faker"          "Faker"            "Faker (synthetic data)"

# Job & utility libraries
pip_install "joblib"         "joblib"           "joblib (model persistence)"
pip_install "tqdm"           "tqdm"             "tqdm (progress bars)"
pip_install "tabulate"       "tabulate"         "tabulate (pretty tables in CLI)"
pip_install "python_dotenv"  "python-dotenv"    "python-dotenv (env vars)"

# =============================================================================
print_header "STEP 5 — OS / Systems Programming Tools (OS Module)"
# =============================================================================
apt_install "inotify-tools"    "inotify-tools (filesystem event monitoring)"
apt_install "e2fsprogs"        "e2fsprogs (chattr / lsattr for immutable files)"
apt_install "attr"             "attr (extended file attributes — xattr)"
apt_install "libcap-dev"       "libcap-dev (POSIX capabilities)"
apt_install "libcap2-bin"      "libcap2-bin (getcap / setcap)"
apt_install "lsof"             "lsof (open file/socket inspector)"
apt_install "procps"           "procps (ps, top, free — /proc utilities)"
apt_install "htop"             "htop (process monitor)"
apt_install "ipcs"             "util-linux (ipcs — IPC status)" || true

# POSIX IPC & threading headers (usually included with glibc-dev)
apt_install "libc6-dev"        "libc6-dev (glibc headers — POSIX IPC, semaphores)"

# Shared memory / POSIX real-time libraries
if ! dpkg -s "librt-dev" &>/dev/null; then
    # On modern Ubuntu librt is part of libc
    print_skip "librt (bundled with libc6 on this Ubuntu version)"
else
    print_skip "librt-dev"
fi

# =============================================================================
print_header "STEP 6 — Git & Version Control"
# =============================================================================
apt_install "git"         "Git"
apt_install "git-lfs"     "Git LFS (large file storage)"
apt_install "tig"         "tig (terminal Git browser)"

GIT_VER=$(git --version)
print_info "$GIT_VER"

# =============================================================================
print_header "STEP 7 — Network & IPC Diagnostics"
# =============================================================================
apt_install "netcat-openbsd"   "netcat (Unix socket testing)"
apt_install "socat"            "socat (socket relay / testing)"
apt_install "net-tools"        "net-tools (ifconfig, netstat)"
apt_install "jq"               "jq (JSON processor for API responses)"

# =============================================================================
print_header "STEP 8 — Documentation & UML"
# =============================================================================
apt_install "plantuml"    "PlantUML (UML diagrams from text)" || true
apt_install "pandoc"      "Pandoc (doc format conversion)"

# Draw.io / Lucidchart are web-based — nothing to install
print_info "Draw.io and Lucidchart are browser-based — no installation needed"
print_info "Draw.io desktop: https://github.com/jgraph/drawio-desktop/releases"

# =============================================================================
print_header "STEP 9 — Optional but Recommended"
# =============================================================================
apt_install "tree"         "tree (directory structure viewer)"
apt_install "tmux"         "tmux (terminal multiplexer — useful for demos)"
apt_install "zip"          "zip"
apt_install "unzip"        "unzip"
apt_install "sqlite3"      "SQLite3 (lightweight DB for AI testing)"

# =============================================================================
print_header "STEP 10 — Verification Summary"
# =============================================================================

echo ""
echo -e "${BOLD}  Tool                     Version / Status${NC}"
echo    "  ─────────────────────────────────────────────────────"

check_tool() {
    local label="$1"
    local cmd="$2"
    local result
    result=$(eval "$cmd" 2>/dev/null | head -n1 || echo "NOT FOUND")
    printf "  %-26s %s\n" "$label" "$result"
}

check_tool "g++"           "g++ --version"
check_tool "make"          "make --version"
check_tool "PostgreSQL"    "psql --version"
check_tool "Python 3"      "python3 --version"
check_tool "pip3"          "pip3 --version"
check_tool "Git"           "git --version"
check_tool "Valgrind"      "valgrind --version"
check_tool "inotifywait"   "inotifywait --version"
check_tool "Doxygen"       "doxygen --version"
check_tool "lsof"          "lsof -v 2>&1 | head -n1"

echo ""
echo -e "  ${BOLD}Python Libraries:${NC}"
for lib in sklearn numpy pandas scipy matplotlib seaborn psycopg2 shap faker joblib; do
    ver=$(python3 -c "import $lib; print(getattr($lib, '__version__', 'installed'))" 2>/dev/null || echo "MISSING")
    printf "    %-22s %s\n" "$lib" "$ver"
done

# =============================================================================
print_header "STEP 11 — PostgreSQL Quick Setup"
# =============================================================================
print_info "Creating 'justiceflow' database and user …"

sudo -u postgres psql -tc "SELECT 1 FROM pg_roles WHERE rolname='justiceflow'" | grep -q 1 && {
    print_skip "PostgreSQL role 'justiceflow'"
} || {
    sudo -u postgres psql -c "CREATE USER justiceflow WITH PASSWORD 'justiceflow123';" -q
    print_ok "PostgreSQL user 'justiceflow' created (password: justiceflow123)"
}

sudo -u postgres psql -tc "SELECT 1 FROM pg_database WHERE datname='justiceflow'" | grep -q 1 && {
    print_skip "PostgreSQL database 'justiceflow'"
} || {
    sudo -u postgres psql -c "CREATE DATABASE justiceflow OWNER justiceflow;" -q
    print_ok "PostgreSQL database 'justiceflow' created"
}

# Grant required privileges
sudo -u postgres psql -d justiceflow -c "GRANT ALL PRIVILEGES ON DATABASE justiceflow TO justiceflow;" -q
print_ok "Privileges granted to user 'justiceflow'"

# =============================================================================
print_header "STEP 12 — Project Directory Structure"
# =============================================================================
PROJECT_DIR="${HOME}/justiceflow"

if [[ -d "$PROJECT_DIR" ]]; then
    print_skip "Project directory already exists at $PROJECT_DIR"
else
    mkdir -p "$PROJECT_DIR"/{src/{core,patterns,os_layer},db/{schema,triggers,procedures,views,seeds,tests},ai/{agents,models,data,analytics},docs/{uml,api},scripts,logs,evidence_store,backups}
    print_ok "Project directory tree created at $PROJECT_DIR"
fi

# =============================================================================
echo ""
echo -e "${GREEN}${BOLD}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}${BOLD}║   ✔  JusticeFlow setup complete!                ║${NC}"
echo -e "${GREEN}${BOLD}╚══════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  ${BOLD}Next Steps:${NC}"
echo    "  1. cd ~/justiceflow"
echo    "  2. git init && git remote add origin <your-repo-url>"
echo    "  3. Load DB schema:  psql -U justiceflow -d justiceflow -f db/schema/init.sql"
echo    "  4. Compile C++:     cd src && make"
echo    "  5. Run AI setup:    python3 ai/agents/hotspot.py --generate-data"
echo ""
echo -e "  ${CYAN}PostgreSQL connection string:${NC}"
echo    "  postgresql://justiceflow:justiceflow123@localhost:5432/justiceflow"
echo ""
