# Simple convenience Makefile (wraps CMake)

BUILD_DIR ?= build-make
DIST_DIR  ?= dist
MUTINEER_DIST := $(DIST_DIR)/mutineer
BUCCANEER_DIST := $(DIST_DIR)/buccaneer
CMAKE     ?= cmake
CMAKE_BUILD_TYPE ?= Release
CMAKE_ARGS ?=
JOBS      ?= 2
BINARY    := $(BUILD_DIR)/mutineer
BUCC      := $(BUILD_DIR)/bucc

.PHONY: all clean dist-legacy dist-mutineer dist-buccaneer configure test bbs-up bbs-down bbs-status tools plank plugins bucc release release-debian release-fedora release-alpine version-bump

version-bump:
	@./scripts/bump-build-version.sh

all: version-bump $(BINARY)

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	@mkdir -p $(BUILD_DIR)
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ] && ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$(CURDIR)$$" "$(BUILD_DIR)/CMakeCache.txt"; then \
		echo "Stale CMake cache in $(BUILD_DIR) was created for a different source tree."; \
		echo "Run 'make clean' or choose a fresh BUILD_DIR=..."; \
		exit 1; \
	fi
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) $(CMAKE_ARGS)

configure: version-bump $(BUILD_DIR)/CMakeCache.txt

$(BINARY): $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS) --target mutineer

tools: version-bump $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS) --target tools

plank: version-bump $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS) --target plank

plugins: version-bump $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS) --target plugins

bucc: version-bump $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS) --target bucc

test: version-bump $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)
	@ctest --test-dir $(BUILD_DIR) --output-on-failure

release-debian: all tools plank plugins
	@VERSION=$(or $(VERSION),dev) PLATFORM=debian ./scripts/build-release.sh

release-fedora: all tools plank plugins
	@VERSION=$(or $(VERSION),dev) PLATFORM=fedora ./scripts/build-release.sh

release-alpine: all tools plank plugins
	@VERSION=$(or $(VERSION),dev) PLATFORM=alpine ./scripts/build-release.sh

release: release-debian

clean:
	@rm -rf $(BUILD_DIR) $(DIST_DIR) build_debug

# The former host-built distribution remains available for compatibility.
# The canonical `dist` target is provided by packaging/Makefile.fragment and
# always compiles and packages inside the pinned Docker build environment.
dist-legacy: dist-mutineer dist-buccaneer

dist-mutineer: all tools plank plugins
	@VERSION=$(or $(VERSION),dev) PLATFORM=debian OUTPUT_DIR=$(DIST_DIR) BUILD_DIR=$(BUILD_DIR) ./scripts/build-release.sh
	@# Legacy layout: symlink dist/mutineer → latest debian package dir for scripts/start
	@pkg=$$(ls -d $(DIST_DIR)/mutineer-$(or $(VERSION),dev)-x86_64-debian 2>/dev/null | tail -1); \
	if [ -n "$$pkg" ]; then rm -rf $(MUTINEER_DIST); cp -a "$$pkg" $(MUTINEER_DIST); fi
	@echo "Mutineer BBS built in $(MUTINEER_DIST)/"

dist-buccaneer: bucc
	@rm -rf $(BUCCANEER_DIST)
	@mkdir -p $(BUCCANEER_DIST)/bin $(BUCCANEER_DIST)/include/buccaneer $(BUCCANEER_DIST)/examples
	@cp $(BUCC) $(BUCCANEER_DIST)/bin/
	@cp src/buccaneer/include/*.h $(BUCCANEER_DIST)/include/buccaneer/
	@if [ -d src/buccaneer/examples ]; then cp -a src/buccaneer/examples/. $(BUCCANEER_DIST)/examples/; fi
	@echo "Buccaneer toolchain built in $(BUCCANEER_DIST)/"

PID_FILE := mutineer.pid

bbs-up:
	@if [ -f $(MUTINEER_DIST)/$(PID_FILE) ] && kill -0 $$(cat $(MUTINEER_DIST)/$(PID_FILE)) 2>/dev/null; then \
		echo "BBS already running (PID $$(cat $(MUTINEER_DIST)/$(PID_FILE)))"; \
	else \
		$(MAKE) --no-print-directory dist-mutineer; \
		(cd $(MUTINEER_DIST) && ./mutineer -c conf/mutineer.conf > /dev/null 2>&1 & echo $$! > $(PID_FILE)); \
		sleep 2; \
		if [ -f $(MUTINEER_DIST)/$(PID_FILE) ] && kill -0 $$(cat $(MUTINEER_DIST)/$(PID_FILE)) 2>/dev/null; then \
			echo "BBS started (PID $$(cat $(MUTINEER_DIST)/$(PID_FILE)))"; \
		else \
			echo "BBS failed to start - check $(MUTINEER_DIST)/logs/mutineer.log"; \
			rm -f $(MUTINEER_DIST)/$(PID_FILE); \
			exit 1; \
		fi \
	fi

bbs-down:
	@if [ -f $(MUTINEER_DIST)/$(PID_FILE) ]; then \
		PID=$$(cat $(MUTINEER_DIST)/$(PID_FILE)); \
		if kill -0 $$PID 2>/dev/null; then \
			kill $$PID; \
			sleep 1; \
			echo "BBS stopped (PID $$PID)"; \
		else \
			echo "BBS not running (stale PID file)"; \
		fi; \
		rm -f $(MUTINEER_DIST)/$(PID_FILE); \
	else \
		echo "BBS not running (no PID file)"; \
	fi

bbs-status:
	@if [ -f $(MUTINEER_DIST)/$(PID_FILE) ] && kill -0 $$(cat $(MUTINEER_DIST)/$(PID_FILE)) 2>/dev/null; then \
		echo "BBS running (PID $$(cat $(MUTINEER_DIST)/$(PID_FILE)))"; \
	else \
		echo "BBS not running"; \
	fi

# Canonical Docker-built, self-extracting distribution targets.
include packaging/Makefile.fragment
