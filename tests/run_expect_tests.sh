#!/bin/bash
# Expect Test Suite Runner for Mutineer BBS
# Starts the daemon, runs all expect tests, and reports results

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DIST_DIR="$PROJECT_DIR/dist"
BUILD_DIR="$PROJECT_DIR/build"

HOST="127.0.0.1"
PORT=2929
DAEMON_PID=""
TESTS_PASSED=0
TESTS_FAILED=0
FAILED_TESTS=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if expect is installed
check_dependencies() {
    if ! command -v expect &> /dev/null; then
        log_error "expect is not installed. Please install it first."
        exit 1
    fi
    
    if ! command -v nc &> /dev/null && ! command -v netcat &> /dev/null; then
        log_warn "nc/netcat not found - using alternative port check"
    fi
}

# Wait for port to be available
wait_for_port() {
    local host=$1
    local port=$2
    local max_attempts=${3:-30}
    
    log_info "Waiting for $host:$port to be available..."
    
    for ((i=1; i<=max_attempts; i++)); do
        if nc -z "$host" "$port" 2>/dev/null || \
           (echo > /dev/tcp/$host/$port) 2>/dev/null; then
            log_info "Port $port is now available"
            return 0
        fi
        sleep 1
    done
    
    log_error "Timeout waiting for port $port"
    return 1
}

# Wait for port to be closed
wait_for_port_closed() {
    local host=$1
    local port=$2
    local max_attempts=${3:-10}
    
    for ((i=1; i<=max_attempts; i++)); do
        if ! nc -z "$host" "$port" 2>/dev/null; then
            return 0
        fi
        sleep 1
    done
    
    return 1
}

# Start the BBS daemon
start_daemon() {
    log_info "Starting Mutineer BBS daemon..."
    
    # Check if already running and responding
    if nc -z "$HOST" "$PORT" 2>/dev/null; then
        # Verify it's actually responding
        if echo "" | nc -w 2 "$HOST" "$PORT" 2>/dev/null | grep -q "Mutineer\|Welcome\|Handle"; then
            log_info "Daemon already running and responding on port $PORT"
            return 0
        fi
        log_warn "Port $PORT in use but daemon not responding, restarting..."
        pkill -9 -f "mutineer.*--config" 2>/dev/null || true
        sleep 2
    fi
    
    # Determine which binary to use
    local binary=""
    if [[ -x "$DIST_DIR/mutineer" ]]; then
        binary="$DIST_DIR/mutineer"
        cd "$DIST_DIR"
    elif [[ -x "$BUILD_DIR/mutineer" ]]; then
        binary="$BUILD_DIR/mutineer"
        cd "$PROJECT_DIR"
    else
        log_error "Cannot find mutineer binary in dist/ or build/"
        exit 1
    fi
    
    # Start daemon in background with test config (WFC disabled)
    local config_file="conf/mutineer-test.conf"
    if [[ ! -f "$config_file" ]]; then
        config_file="conf/mutineer.conf"
    fi
    "$binary" --config "$config_file" > /tmp/mutineer_test.log 2>&1 &
    DAEMON_PID=$!
    
    log_info "Daemon started with PID $DAEMON_PID"
    
    # Wait for it to be ready
    if ! wait_for_port "$HOST" "$PORT" 30; then
        log_error "Daemon failed to start"
        kill $DAEMON_PID 2>/dev/null || true
        cat /tmp/mutineer_test.log | tail -20
        exit 1
    fi
    
    # Give it a moment to fully initialize
    sleep 1
}

# Stop the BBS daemon
stop_daemon() {
    if [[ -n "$DAEMON_PID" ]]; then
        log_info "Stopping daemon (PID $DAEMON_PID)..."
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
        wait_for_port_closed "$HOST" "$PORT" 10
        DAEMON_PID=""
    fi
}

# Run a single expect test
run_test() {
    local test_file=$1
    local test_name=$(basename "$test_file" .exp)
    
    echo ""
    echo "========================================"
    echo "Running: $test_name"
    echo "========================================"
    
    if expect "$test_file"; then
        log_info "$test_name: PASSED"
        ((TESTS_PASSED++))
        return 0
    else
        log_error "$test_name: FAILED"
        ((TESTS_FAILED++))
        FAILED_TESTS="$FAILED_TESTS $test_name"
        return 1
    fi
}

# Print final summary
print_summary() {
    local total=$((TESTS_PASSED + TESTS_FAILED))
    
    echo ""
    echo "========================================"
    echo "       EXPECT TEST SUITE RESULTS        "
    echo "========================================"
    echo ""
    echo -e "Total tests:  $total"
    echo -e "Passed:       ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed:       ${RED}$TESTS_FAILED${NC}"
    echo ""
    
    if [[ $TESTS_FAILED -gt 0 ]]; then
        echo -e "${RED}Failed tests:${NC}$FAILED_TESTS"
        echo ""
    fi
    
    if [[ $TESTS_FAILED -eq 0 ]]; then
        echo -e "${GREEN}All tests passed!${NC}"
        return 0
    else
        echo -e "${RED}Some tests failed.${NC}"
        return 1
    fi
}

# Cleanup on exit
cleanup() {
    stop_daemon
}

trap cleanup EXIT

# Main execution
main() {
    log_info "Mutineer BBS Expect Test Suite"
    log_info "=============================="
    
    check_dependencies
    
    cd "$PROJECT_DIR"
    
    # Start daemon
    start_daemon
    
    # Find and run all test_*.exp files
    local test_files=()
    for f in "$SCRIPT_DIR"/test_*.exp; do
        if [[ -f "$f" ]]; then
            test_files+=("$f")
        fi
    done
    
    if [[ ${#test_files[@]} -eq 0 ]]; then
        log_warn "No test_*.exp files found in $SCRIPT_DIR"
        exit 0
    fi
    
    log_info "Found ${#test_files[@]} test file(s)"
    
    # Run each test
    for test_file in "${test_files[@]}"; do
        run_test "$test_file" || true
    done
    
    # Print summary and exit with appropriate code
    print_summary
    exit $?
}

# Allow running specific tests
if [[ $# -gt 0 ]]; then
    check_dependencies
    cd "$PROJECT_DIR"
    start_daemon
    
    for test_file in "$@"; do
        if [[ -f "$test_file" ]]; then
            run_test "$test_file" || true
        elif [[ -f "$SCRIPT_DIR/$test_file" ]]; then
            run_test "$SCRIPT_DIR/$test_file" || true
        elif [[ -f "$SCRIPT_DIR/test_$test_file.exp" ]]; then
            run_test "$SCRIPT_DIR/test_$test_file.exp" || true
        else
            log_error "Test file not found: $test_file"
            ((TESTS_FAILED++))
        fi
    done
    
    print_summary
    exit $?
fi

main
