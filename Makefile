# ============================================================================
# JusticeFlow Makefile
# ============================================================================
# Comprehensive build system for the JusticeFlow platform
# Handles OS Layer, System Subsystems, and API Gateway components.

# ============================================================================
# Configuration
# ============================================================================

PROJECT_NAME    := justiceflow
VERSION         := 1.0.0
BUILD_DIR       := build
SRC_DIR         := src
TEST_DIR        := tests
INSTALL_PREFIX  := /usr/local
DB_SCHEMA_DIR   := db/schema
DB_SCHEMA_FILES := 00_schema.sql 01_types.sql 02_tables.sql 03_functions.sql 04_triggers.sql 05_indexes.sql 06_permissions.sql
DB_HOST         ?= $(if $(PGHOST),$(PGHOST),localhost)
DB_NAME         ?= $(if $(PGDATABASE),$(PGDATABASE),justiceflow)
DB_USER         ?= $(if $(PGUSER),$(PGUSER),justiceflow)
SEED_DATA       ?= auto

# Compiler settings
CXX             := g++
CXXFLAGS        := -std=c++17 -Wall -Wextra -Werror -fPIC -pthread
CXXFLAGS_DEBUG  := $(CXXFLAGS) -g -O0 -DDEBUG
CXXFLAGS_RELEASE:= $(CXXFLAGS) -O3 -DNDEBUG

# Linker settings
# -lpq: PostgreSQL | -lrt: Shared Mem/Timers | -lreadline: CLI | -ldl: Dynamic Loading
LDFLAGS         := -L/usr/lib/postgresql
LDLIBS          := -lpq -lpthread -lrt -lreadline -ldl -lm

# Include paths - Adding all module include directories
INCLUDE_DIRS    := \
    -I$(SRC_DIR) \
    -I$(SRC_DIR)/common \
    -I$(SRC_DIR)/api_gateway/include \
    -I$(SRC_DIR)/interface \
    -I$(SRC_DIR)/os_layer \
    -I$(SRC_DIR)/os_layer/scheduler/include \
    -I$(SRC_DIR)/os_layer/ipc/include \
    -I$(SRC_DIR)/os_layer/threading/include \
    -I$(SRC_DIR)/os_layer/memory/include \
    -I$(SRC_DIR)/os_layer/process/include \
    -I$(SRC_DIR)/system/shr_infra/auth/include \
    -I$(SRC_DIR)/system/subsystem1/include \
    -I$(SRC_DIR)/system/subsystem2/include \
    -I$(SRC_DIR)/system/subsystem2/include/controllers \
    -I$(SRC_DIR)/system/subsystem2/include/models \
    -I$(SRC_DIR)/system/subsystem2/include/patterns \
    -I$(SRC_DIR)/system/subsystem3/audit/include \
    -I$(SRC_DIR)/system/subsystem3/enforcement/include \
    -I$(SRC_DIR)/system/subsystem3/forensic/include \
    -I/usr/include/postgresql

# Targets
EXECUTABLE      := $(BUILD_DIR)/justiceflow
TEST_EXECUTABLE := $(BUILD_DIR)/justiceflow_test

# Build mode selection
BUILD_MODE      ?= debug
ifeq ($(BUILD_MODE),release)
    CXXFLAGS    := $(CXXFLAGS_RELEASE)
else
    CXXFLAGS    := $(CXXFLAGS_DEBUG)
endif

# ============================================================================
# Source Files Discovery
# ============================================================================

# We use recursive wildcards to find all .cpp files in the nested structure
# This ensures we don't miss new files added to the sub-directories
define rwildcard
$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
endef

# Gather all source files
ALL_CPP_SOURCES := $(call rwildcard,$(SRC_DIR),*.cpp)

# Filter out main.cpp for the test build if necessary (common practice)
MAIN_SRC := $(SRC_DIR)/main.cpp
APP_SOURCES := $(filter-out $(MAIN_SRC), $(ALL_CPP_SOURCES))

# Object files mapping (e.g., src/os/ipc.cpp -> build/src/os/ipc.o)
OBJECTS := $(ALL_CPP_SOURCES:%.cpp=$(BUILD_DIR)/%.o)

# ============================================================================
# Build Rules
# ============================================================================

.PHONY: all build clean test install help debug release verify-deps init-db seed-db setup-db setup-python setup-ai setup-status setup-all full-setup

# Default target
all: setup-all build

build: $(EXECUTABLE)

# Link the final executable
$(EXECUTABLE): $(OBJECTS)
	@echo "[LINK] $@"
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)
	@echo "Build Successful: $@"

# Compile source files to object files
$(BUILD_DIR)/%.o: %.cpp
	@echo "[CXX]  $<"
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) -c $< -o $@

# ============================================================================
# Testing
# ============================================================================

# Test objects (excluding main.o from the app sources)
TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_OBJECTS := $(TEST_SOURCES:%.cpp=$(BUILD_DIR)/%.o)

test: $(TEST_EXECUTABLE)
	@echo "[RUN]  Running tests..."
	@export PGHOST=/var/run/postgresql && \
	 export PGDATABASE=justiceflow && \
	 export PGUSER=justice_app && \
	 export JF_TEST_AUTH_FALLBACK='42401-637951-0=JusticeDemo@2026;12345-6789012-3=password123' && \
	 ./$(TEST_EXECUTABLE)

$(TEST_EXECUTABLE): $(filter-out $(BUILD_DIR)/$(SRC_DIR)/main.o, $(OBJECTS)) $(TEST_OBJECTS)
	@echo "[LINK] Test Suite"
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS) -lgtest -lgtest_main

# ============================================================================
# Maintenance
# ============================================================================

clean:
	@echo "[CLEAN] Removing $(BUILD_DIR)..."
	@rm -rf $(BUILD_DIR)

install: $(EXECUTABLE)
	@echo "[INSTALL] Installing to $(INSTALL_PREFIX)/bin..."
	@install -d $(INSTALL_PREFIX)/bin
	@install -m 755 $(EXECUTABLE) $(INSTALL_PREFIX)/bin/

debug:
	@$(MAKE) BUILD_MODE=debug

release:
	@$(MAKE) BUILD_MODE=release

# ============================================================================
# Diagnostics / Help
# ============================================================================

help:
	@echo "JusticeFlow Build System"
	@echo "Usage: make [target] [BUILD_MODE=debug|release]"
	@echo ""
	@echo "Targets:"
	@echo "  all         (default) Run setup-all then build"
	@echo "  build       Build the main justiceflow binary"
	@echo "  clean     Remove build directory and artifacts"
	@echo "  test        Build and run the test suite (requires GTest)"
	@echo "  install     Install binary to $(INSTALL_PREFIX)/bin"
	@echo "  debug       Build with debug symbols (-g -O0)"
	@echo "  release     Build with optimizations (-O3)"
	@echo "  verify-deps Check g++, make, python3, pip3, psql, and pg_config"
	@echo "  init-db     Load DB schema SQL files (00..06) via psql"
	@echo "  seed-db     Seed database using generator script if available"
	@echo "  setup-db    Run init-db then seed-db"
	@echo "  setup-python Install Python dependencies from requirements.txt"
	@echo "  setup-ai    Verify AI agent imports"
	@echo "  setup-status Show current setup configuration"
	@echo "  setup-all   verify-deps -> setup-db -> setup-python -> setup-ai"
	@echo "  full-setup  verify-deps -> setup-all -> build"
	@echo ""
	@echo "Current Settings:"
	@echo "  Compiler: $(CXX)"
	@echo "  Mode:     $(BUILD_MODE)"
	@echo "  DB Host:  $(DB_HOST)"
	@echo "  DB Name:  $(DB_NAME)"
	@echo "  DB User:  $(DB_USER)"
	@echo "  Seed:     $(SEED_DATA)"

verify-deps:
	@echo "[CHECK] Verifying required system dependencies..."
	@for cmd in g++ make python3 pip3 psql pg_config; do \
		if ! command -v $$cmd >/dev/null 2>&1; then \
			echo "[ERROR] Missing dependency: $$cmd"; \
			exit 1; \
		fi; \
	done
	@echo "  - g++:       $$(g++ --version | head -n1)"
	@echo "  - make:      $$(make --version | head -n1)"
	@echo "  - python3:   $$(python3 --version 2>&1)"
	@echo "  - pip3:      $$(pip3 --version | head -n1)"
	@echo "  - psql:      $$(psql --version | head -n1)"
	@echo "  - pg_config: $$(pg_config --version | head -n1)"

init-db:
	@echo "[DB] Initializing schema using host=$(DB_HOST) db=$(DB_NAME) user=$(DB_USER)"
	@for sql_file in $(DB_SCHEMA_FILES); do \
		file="$(DB_SCHEMA_DIR)/$$sql_file"; \
		if [ ! -f "$$file" ]; then \
			echo "[ERROR] Missing schema file: $$file"; \
			exit 1; \
		fi; \
		echo "[DB] Executing $$file"; \
		PGHOST="$(DB_HOST)" PGDATABASE="$(DB_NAME)" PGUSER="$(DB_USER)" psql -v ON_ERROR_STOP=1 -f "$$file" || exit $$?; \
	done
	@echo "[DB] Schema initialization completed"

seed-db:
	@seed_lower="$$(echo "$(SEED_DATA)" | tr '[:upper:]' '[:lower:]')"; \
	if [ "$$seed_lower" = "0" ] || [ "$$seed_lower" = "false" ]; then \
		echo "[DB] Seeding disabled (set SEED_DATA=1 or SEED_DATA=auto to enable)"; \
	else \
		seed_script=""; \
		if [ "$$seed_lower" != "1" ] && [ "$$seed_lower" != "true" ] && [ "$$seed_lower" != "auto" ]; then \
			echo "[DB] Unsupported SEED_DATA='$(SEED_DATA)'. Use 0, 1, or auto."; \
			exit 1; \
		fi; \
		if [ -f db/generate_data.py ]; then \
			seed_script="db/generate_data.py"; \
		elif [ -f db/data_generator/generate_data.py ]; then \
			seed_script="db/data_generator/generate_data.py"; \
		fi; \
		if [ -n "$$seed_script" ]; then \
			if python3 -c "import numpy, psycopg2, faker" >/dev/null 2>&1; then \
				echo "[DB] Running seed script: $$seed_script"; \
				python3 "$$seed_script"; \
			else \
				echo "[DB] Python seed dependencies missing. Run 'make setup-python' first."; \
			fi; \
		else \
			echo "[DB] No data generator found. Skipping seed step."; \
		fi; \
	fi

setup-db: init-db seed-db

setup-python:
	@echo "[PY] Installing dependencies from requirements.txt"
	@pip3 install -r requirements.txt

setup-ai:
	@echo "[PY] Verifying AI agent imports"
	@python3 -c "import src.ai_agents.hotspot_agent; import src.ai_agents.priority_agent" || \
		( echo "[PY] Failed to import required AI agents: hotspot_agent, priority_agent"; exit 1 )
	@workload_found=0; \
	for module_name in src.ai_agents.workload src.ai_agents.workload_agent; do \
		if python3 -c "import importlib; importlib.import_module('$$module_name')" >/dev/null 2>&1; then \
			echo "[PY] Imported $$module_name"; \
			workload_found=1; \
			break; \
		fi; \
	done; \
	if [ "$$workload_found" -ne 1 ]; then \
		echo "[PY] Could not import src.ai_agents.workload or src.ai_agents.workload_agent"; \
		exit 1; \
	fi
	@echo "[PY] AI agent imports verified"

setup-status:
	@echo "JusticeFlow Setup Status"
	@echo "  PGHOST:     $(DB_HOST)"
	@echo "  PGDATABASE: $(DB_NAME)"
	@echo "  PGUSER:     $(DB_USER)"
	@echo "  SEED_DATA:  $(SEED_DATA)"
	@echo "  Schema dir: $(DB_SCHEMA_DIR)"
	@if [ -f db/generate_data.py ]; then \
		echo "  Seed script: db/generate_data.py"; \
	elif [ -f db/data_generator/generate_data.py ]; then \
		echo "  Seed script: db/data_generator/generate_data.py"; \
	else \
		echo "  Seed script: not found"; \
	fi

setup-all: verify-deps setup-db setup-python setup-ai

full-setup: verify-deps setup-all build

# Print variables for debugging Makefile logic
print-%:
	@echo $* = $($*)
