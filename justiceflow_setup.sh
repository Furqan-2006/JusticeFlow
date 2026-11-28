#!/bin/bash
# =============================================================================
#  JusticeFlow — System Package Setup
#  Police Case & Evidence Management System
#  Multi-Course: DBMS · SDA · AI · OS
# =============================================================================
#  Usage:   chmod +x justiceflow_setup.sh && sudo ./justiceflow_setup.sh
#  Target:  Ubuntu 22.04 and later
#  Installs system packages only. Python packages go into .venv
#  (`make setup-python`) and the database is created by run_justiceflow.sh.
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
apt_install "libreadline-dev"   "libreadline-dev (CLI line editing headers)"
apt_install "libgtest-dev"      "GoogleTest (C++ unit tests)"
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
apt_install "libpq-dev"             "libpq-dev (C client headers)"

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
apt_install "util-linux"       "util-linux (ipcs — IPC status)"

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
apt_install "plantuml"    "PlantUML (UML diagrams from text)"
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

# =============================================================================
echo ""
echo -e "${GREEN}${BOLD}╔══════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}${BOLD}║   ✔  JusticeFlow setup complete!                ║${NC}"
echo -e "${GREEN}${BOLD}╚══════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  ${BOLD}Next step${NC} (as your normal user, from the project folder):"
echo    "    ./run_justiceflow.sh"
echo ""
