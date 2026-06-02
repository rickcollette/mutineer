# Simple convenience Makefile (wraps CMake)

BUILD_DIR ?= build
DIST_DIR  ?= dist
MUTINEER_DIST := $(DIST_DIR)/mutineer
BUCCANEER_DIST := $(DIST_DIR)/buccaneer
CMAKE     ?= cmake
CMAKE_BUILD_TYPE ?= Release
BINARY    := $(BUILD_DIR)/mutineer

TOOLS := mutineer-qwkgen mutineer-msgpack mutineer-userpack mutineer-filepack mutineer-stats mutineer-maint mutineer-initbbs

.PHONY: all clean dist dist-mutineer dist-buccaneer configure bbs-up bbs-down bbs-status tools

all: $(BINARY)

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

$(BINARY): $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --target mutineer

tools: $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --target tools

clean:
	@rm -rf $(BUILD_DIR) $(DIST_DIR) build_debug

dist: dist-mutineer dist-buccaneer

dist-mutineer: all tools
	@mkdir -p $(MUTINEER_DIST)
	@cp $(BINARY) $(MUTINEER_DIST)/
	@mkdir -p $(MUTINEER_DIST)/bin
	@for tool in $(TOOLS); do \
		if [ -f $(BUILD_DIR)/$$tool ]; then cp $(BUILD_DIR)/$$tool $(MUTINEER_DIST)/bin/; fi; \
	done
	@mkdir -p $(MUTINEER_DIST)/conf $(MUTINEER_DIST)/art $(MUTINEER_DIST)/menus $(MUTINEER_DIST)/sql $(MUTINEER_DIST)/docs $(MUTINEER_DIST)/logs $(MUTINEER_DIST)/data $(MUTINEER_DIST)/scripts
	@cp -a conf/ $(MUTINEER_DIST)/
	@cp -a art/ $(MUTINEER_DIST)/
	@cp -a menus/ $(MUTINEER_DIST)/
	@cp -a sql/ $(MUTINEER_DIST)/
	@cp -a docs/ $(MUTINEER_DIST)/
	@cp -a scripts/ $(MUTINEER_DIST)/
	@if [ -f README.md ]; then cp README.md $(MUTINEER_DIST)/; fi
	@echo "Mutineer BBS built in $(MUTINEER_DIST)/"

dist-buccaneer:
	@docker run --rm -v "$(CURDIR)/src/buccaneer:/buccaneer" -w /buccaneer bucc-test sh -c 'make clean; make; make dist'
	@mkdir -p $(BUCCANEER_DIST)
	@docker run --rm -v "$(CURDIR)/src/buccaneer:/buccaneer" -v "$(CURDIR)/$(BUCCANEER_DIST):/out" -w /buccaneer bucc-test sh -c 'cp -a dist/* /out/'
	@echo "Buccaneer toolchain built in $(BUCCANEER_DIST)/"

PID_FILE := mutineer.pid

bbs-up:
	@if [ -f $(MUTINEER_DIST)/$(PID_FILE) ] && kill -0 $$(cat $(MUTINEER_DIST)/$(PID_FILE)) 2>/dev/null; then \
		echo "BBS already running (PID $$(cat $(MUTINEER_DIST)/$(PID_FILE)))"; \
	else \
		$(MAKE) --no-print-directory dist-mutineer; \
		(cd $(MUTINEER_DIST) && ./mutineer > /dev/null 2>&1 & echo $$! > $(MUTINEER_DIST)/$(PID_FILE)); \
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

