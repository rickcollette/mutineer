#!/bin/bash
# Expect Test Suite Runner for Mutineer BBS
# Starts a private daemon, runs expect tests, and removes its runtime files.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-$PROJECT_DIR/build}"
HOST="127.0.0.1"
PORT="${MUTINEER_TEST_PORT:-}"
RUNTIME_DIR="${MUTINEER_TEST_ROOT:-}"
CONFIG_FILE=""
DAEMON_PID=""
TESTS_PASSED=0
TESTS_FAILED=0
FAILED_TESTS=""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

check_dependencies() {
    if ! command -v expect &> /dev/null; then
        log_error "expect is not installed. Please install it first."
        exit 1
    fi
    if ! command -v nc &> /dev/null && ! command -v netcat &> /dev/null; then
        log_warn "nc/netcat not found - using bash /dev/tcp port checks"
    fi
}

choose_port() {
    python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

port_open() {
    local host=$1
    local port=$2
    nc -z "$host" "$port" 2>/dev/null || (echo > "/dev/tcp/$host/$port") 2>/dev/null
}

wait_for_port() {
    local host=$1
    local port=$2
    local max_attempts=${3:-30}
    log_info "Waiting for $host:$port to be available..."
    for ((i=1; i<=max_attempts; i++)); do
        if port_open "$host" "$port"; then
            log_info "Port $port is now available"
            return 0
        fi
        sleep 1
    done
    log_error "Timeout waiting for port $port"
    return 1
}

wait_for_port_closed() {
    local host=$1
    local port=$2
    local max_attempts=${3:-10}
    for ((i=1; i<=max_attempts; i++)); do
        if ! port_open "$host" "$port"; then
            return 0
        fi
        sleep 1
    done
    return 1
}

prepare_runtime() {
    if [[ -z "$PORT" ]]; then
        PORT="$(choose_port)"
    fi
    if port_open "$HOST" "$PORT"; then
        log_error "Refusing to attach to existing daemon: $HOST:$PORT is already in use"
        exit 1
    fi

    if [[ -z "$RUNTIME_DIR" ]]; then
        RUNTIME_DIR="$(mktemp -d "${TMPDIR:-/tmp}/mutineer-expect.XXXXXX")"
    else
        mkdir -p "$RUNTIME_DIR"
    fi

    mkdir -p \
        "$RUNTIME_DIR/conf" \
        "$RUNTIME_DIR/data/files/general" \
        "$RUNTIME_DIR/data/dropfiles" \
        "$RUNTIME_DIR/logs" \
        "$RUNTIME_DIR/art" \
        "$RUNTIME_DIR/doors" \
        "$RUNTIME_DIR/door_runtime"

    CONFIG_FILE="$RUNTIME_DIR/conf/mutineer-test.conf"
    cat > "$CONFIG_FILE" <<EOF
bind=$HOST
port=$PORT
db_path=$RUNTIME_DIR/data/mutineer.db
menu_main=$PROJECT_DIR/menus/main.mnu
data_path=$RUNTIME_DIR/data
logs_path=$RUNTIME_DIR/logs/mutineer.log
art_path=$PROJECT_DIR/art
doors_path=$RUNTIME_DIR/doors
dropfile_path=$RUNTIME_DIR/data/dropfiles
protocol_path=$PROJECT_DIR/conf/protocols.conf
bbs_name=Mutineer Test BBS
sysop_name=Sysop
motd=$PROJECT_DIR/art/motd.ans
wfc_enabled=0
console_enabled=0
scheduler_enabled=0
allow_multi_login=0
login_max_attempts=20
door_runtime_path=$RUNTIME_DIR/door_runtime
EOF

    log_info "Initializing private test DB at $RUNTIME_DIR/data/mutineer.db"
    "$BUILD_DIR/mutineer-initbbs" -c "$CONFIG_FILE" -y > "$RUNTIME_DIR/initbbs.log" 2>&1
}

start_daemon() {
    local binary="$BUILD_DIR/mutineer"
    if [[ ! -x "$binary" ]]; then
        log_error "Cannot find mutineer binary at $binary"
        exit 1
    fi
    if [[ ! -x "$BUILD_DIR/mutineer-initbbs" ]]; then
        log_error "Cannot find mutineer-initbbs binary at $BUILD_DIR/mutineer-initbbs"
        exit 1
    fi

    prepare_runtime

    log_info "Starting Mutineer BBS daemon on $HOST:$PORT..."
    "$binary" -c "$CONFIG_FILE" > "$RUNTIME_DIR/mutineer_test.log" 2>&1 &
    DAEMON_PID=$!
    log_info "Daemon started with PID $DAEMON_PID"

    if ! wait_for_port "$HOST" "$PORT" 30; then
        log_error "Daemon failed to start"
        kill "$DAEMON_PID" 2>/dev/null || true
        tail -40 "$RUNTIME_DIR/mutineer_test.log" || true
        exit 1
    fi
    sleep 1
}

stop_daemon() {
    if [[ -n "$DAEMON_PID" ]]; then
        log_info "Stopping daemon (PID $DAEMON_PID)..."
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
        wait_for_port_closed "$HOST" "$PORT" 10 || true
        DAEMON_PID=""
    fi
}

run_test() {
    local test_file=$1
    local test_name
    test_name=$(basename "$test_file" .exp)

    echo ""
    echo "========================================"
    echo "Running: $test_name"
    echo "========================================"

    if MUTINEER_EXPECT_HOST="$HOST" MUTINEER_EXPECT_PORT="$PORT" expect "$test_file"; then
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
    [[ $TESTS_FAILED -eq 0 ]]
}

cleanup() {
    stop_daemon
    if [[ -n "$RUNTIME_DIR" && -d "$RUNTIME_DIR" && -z "${MUTINEER_KEEP_EXPECT_ROOT:-}" ]]; then
        rm -rf "$RUNTIME_DIR"
    fi
}

trap cleanup EXIT

main() {
    local requested_tests=("${@:2}")

    log_info "Mutineer BBS Expect Test Suite"
    log_info "=============================="

    check_dependencies
    cd "$PROJECT_DIR"
    start_daemon

    local test_files=()
    if [[ ${#requested_tests[@]} -gt 0 ]]; then
        for test_file in "${requested_tests[@]}"; do
            if [[ -f "$test_file" ]]; then
                test_files+=("$test_file")
            elif [[ -f "$SCRIPT_DIR/$test_file" ]]; then
                test_files+=("$SCRIPT_DIR/$test_file")
            elif [[ -f "$SCRIPT_DIR/test_$test_file.exp" ]]; then
                test_files+=("$SCRIPT_DIR/test_$test_file.exp")
            else
                log_error "Test file not found: $test_file"
                ((TESTS_FAILED++))
            fi
        done
    else
        for f in "$SCRIPT_DIR"/test_*.exp; do
            [[ -f "$f" ]] && test_files+=("$f")
        done
    fi

    if [[ ${#test_files[@]} -eq 0 ]]; then
        log_warn "No expect test files selected"
        exit 0
    fi

    log_info "Found ${#test_files[@]} test file(s)"
    for test_file in "${test_files[@]}"; do
        run_test "$test_file" || true
    done

    print_summary
    exit $?
}

main "$@"
