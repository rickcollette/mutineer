# Simple convenience Makefile (wraps CMake)

BUILD_DIR ?= build
DIST_DIR  ?= dist
MUTINEER_DIST := $(DIST_DIR)/mutineer
BUCCANEER_DIST := $(DIST_DIR)/buccaneer
CMAKE     ?= cmake
CMAKE_BUILD_TYPE ?= Release
BINARY    := $(BUILD_DIR)/mutineer

TOOLS := mutineer-qwkgen mutineer-msgpack mutineer-userpack mutineer-filepack \
         mutineer-stats mutineer-maint mutineer-initbbs mutineer-validate \
         mutineer-netmail-export

PLANK_TOOLS := plankd coved plankctl plankpack plank-offline

.PHONY: all clean dist dist-mutineer dist-buccaneer configure bbs-up bbs-down bbs-status tools plank release release-debian release-fedora release-alpine

all: $(BINARY)

$(BUILD_DIR)/CMakeCache.txt: CMakeLists.txt
	@$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)

$(BINARY): $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --target mutineer

tools: $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --target tools

plank: $(BUILD_DIR)/CMakeCache.txt
	@$(CMAKE) --build $(BUILD_DIR) --target plank plugins

release-debian: all tools plank
	@VERSION=$(or $(VERSION),dev) PLATFORM=debian ./scripts/build-release.sh

release-fedora: all tools plank
	@VERSION=$(or $(VERSION),dev) PLATFORM=fedora ./scripts/build-release.sh

release-alpine: all tools plank
	@VERSION=$(or $(VERSION),dev) PLATFORM=alpine ./scripts/build-release.sh

release: release-debian

clean:
	@rm -rf $(BUILD_DIR) $(DIST_DIR) build_debug

dist: dist-mutineer dist-buccaneer

dist-mutineer: all tools plank
	@VERSION=$(or $(VERSION),dev) PLATFORM=debian OUTPUT_DIR=$(DIST_DIR) BUILD_DIR=$(BUILD_DIR) ./scripts/build-release.sh
	@# Legacy layout: symlink dist/mutineer → latest debian package dir for scripts/start
	@pkg=$$(ls -d $(DIST_DIR)/mutineer-$(or $(VERSION),dev)-x86_64-debian 2>/dev/null | tail -1); \
	if [ -n "$$pkg" ]; then rm -rf $(MUTINEER_DIST); cp -a "$$pkg" $(MUTINEER_DIST); fi
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

