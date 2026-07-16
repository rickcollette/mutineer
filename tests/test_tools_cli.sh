#!/bin/bash
#
# test_tools_cli.sh - Test that standalone tools run correctly
#
# This script verifies that each tool:
# 1. Executes without crashing
# 2. Shows help when --help is passed
# 3. Returns appropriate exit codes
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${1:-${SCRIPT_DIR}/../build}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

PASS=0
FAIL=0

pass() {
    echo -e "${GREEN}PASS${NC}: $1"
    PASS=$((PASS + 1))
}

fail() {
    echo -e "${RED}FAIL${NC}: $1"
    FAIL=$((FAIL + 1))
}

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found: $BUILD_DIR"
    echo "Run 'make' first to build the tools."
    exit 1
fi

echo "Testing standalone tools..."
echo ""

# Test mutineer-stats --help
echo -n "mutineer-stats --help... "
if "$BUILD_DIR/mutineer-stats" --help 2>&1 | grep -q "Display system statistics"; then
    pass "mutineer-stats --help"
else
    fail "mutineer-stats --help"
fi

# Test mutineer-maint --help
echo -n "mutineer-maint --help... "
if "$BUILD_DIR/mutineer-maint" --help 2>&1 | grep -q "Database maintenance"; then
    pass "mutineer-maint --help"
else
    fail "mutineer-maint --help"
fi

# Test mutineer-msgpack --help
echo -n "mutineer-msgpack --help... "
# Capture first instead of piping the instrumented process into grep -q.  grep
# exits as soon as it matches, which can deliver SIGPIPE while ASan is still
# flushing stdio and turn a harmless help check into a DEADLYSIGNAL flood.
msgpack_help="$($BUILD_DIR/mutineer-msgpack --help 2>&1)"
if grep -q "Pack/purge old messages" <<<"$msgpack_help"; then
    pass "mutineer-msgpack --help"
else
    fail "mutineer-msgpack --help"
fi

# Test mutineer-userpack --help
echo -n "mutineer-userpack --help... "
if "$BUILD_DIR/mutineer-userpack" --help 2>&1 | grep -q "Pack/purge deleted"; then
    pass "mutineer-userpack --help"
else
    fail "mutineer-userpack --help"
fi

# Test mutineer-filepack --help
echo -n "mutineer-filepack --help... "
if "$BUILD_DIR/mutineer-filepack" --help 2>&1 | grep -q "Remove orphaned file"; then
    pass "mutineer-filepack --help"
else
    fail "mutineer-filepack --help"
fi

# Test mutineer-qwkgen --help
echo -n "mutineer-qwkgen --help... "
if "$BUILD_DIR/mutineer-qwkgen" --help 2>&1 | grep -q "Generate QWK mail packet"; then
    pass "mutineer-qwkgen --help"
else
    fail "mutineer-qwkgen --help"
fi

# Test mutineer-netmail-export --help
echo -n "mutineer-netmail-export --help... "
if "$BUILD_DIR/mutineer-netmail-export" --help 2>&1 | grep -q "Export pending FidoNet netmail"; then
    pass "mutineer-netmail-export --help"
else
    fail "mutineer-netmail-export --help"
fi

# Test that tools fail gracefully with missing config
echo -n "mutineer-stats with missing config... "
if ! "$BUILD_DIR/mutineer-stats" -c /nonexistent/config.conf 2>&1 | grep -q "Failed to load config"; then
    fail "mutineer-stats should fail with missing config"
else
    pass "mutineer-stats fails gracefully with missing config"
fi

# Test that mutineer-msgpack requires --days
echo -n "mutineer-msgpack requires --days... "
if "$BUILD_DIR/mutineer-msgpack" 2>&1 | grep -q "days.*required"; then
    pass "mutineer-msgpack requires --days"
else
    fail "mutineer-msgpack should require --days"
fi

# Test that mutineer-userpack requires --deleted or --inactive
echo -n "mutineer-userpack requires --deleted or --inactive... "
if "$BUILD_DIR/mutineer-userpack" 2>&1 | grep -q "Must specify"; then
    pass "mutineer-userpack requires --deleted or --inactive"
else
    fail "mutineer-userpack should require --deleted or --inactive"
fi

# Test that mutineer-maint requires a command
echo -n "mutineer-maint requires command... "
if "$BUILD_DIR/mutineer-maint" 2>&1 | grep -q "Command required"; then
    pass "mutineer-maint requires command"
else
    fail "mutineer-maint should require command"
fi

# Test that mutineer-qwkgen requires username
echo -n "mutineer-qwkgen requires username... "
if "$BUILD_DIR/mutineer-qwkgen" 2>&1 | grep -q "Username required"; then
    pass "mutineer-qwkgen requires username"
else
    fail "mutineer-qwkgen should require username"
fi

echo ""
echo "================================"
echo "Results: $PASS passed, $FAIL failed"
echo "================================"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
exit 0
