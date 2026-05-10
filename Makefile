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

.PHONY: all clean test install help debug release

# Default target
all: $(EXECUTABLE)

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
	@echo "  all       (default) Build the main justiceflow binary"
	@echo "  clean     Remove build directory and artifacts"
	@echo "  test      Build and run the test suite (requires GTest)"
	@echo "  install   Install binary to $(INSTALL_PREFIX)/bin"
	@echo "  debug     Build with debug symbols (-g -O0)"
	@echo "  release   Build with optimizations (-O3)"
	@echo ""
	@echo "Current Settings:"
	@echo "  Compiler: $(CXX)"
	@echo "  Mode:     $(BUILD_MODE)"

# Print variables for debugging Makefile logic
print-%:
	@echo $* = $($*)
