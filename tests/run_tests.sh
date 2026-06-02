#!/bin/bash
# Comprehensive BBS login and display tests

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BBS_DIR="$(dirname "$SCRIPT_DIR")"
PORT=2929
PASS=0
FAIL=0
TESTS_RUN=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((PASS++))
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    echo -e "${RED}       Expected: $2${NC}"
    if [ -n "$3" ]; then
        echo -e "${RED}       Got (first 200 chars): ${3:0:200}${NC}"
    fi
    ((FAIL++))
}

log_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
    ((TESTS_RUN++))
}

# Helper to run a test session - sends lines with delays
run_session() {
    local wait_time="${1:-3}"
    shift
    {
        for line in "$@"; do
            sleep 0.3
            printf '%s\n' "$line"
        done
        sleep "$wait_time"
    } | timeout $((wait_time + 3)) nc localhost "$PORT" 2>/dev/null | cat -v
}

# Ensure BBS is running
ensure_bbs_running() {
    if ! fuser "$PORT/tcp" >/dev/null 2>&1; then
        echo "Starting BBS..."
        cd "$BBS_DIR" && ./scripts/start-screen
        sleep 2
    fi
}

# Clean up test users
cleanup_test_users() {
    sqlite3 "$BBS_DIR/dist/data/mutineer.db" "DELETE FROM users WHERE handle LIKE 'test_%';" 2>/dev/null || true
}

echo "========================================"
echo "  Mutineer BBS Test Suite"
echo "========================================"
echo ""

ensure_bbs_running
cleanup_test_users

# ============================================
# TEST 1: Initial connection shows welcome
# ============================================
log_test "Initial connection shows welcome message"
OUTPUT=$(run_session 2)

if echo "$OUTPUT" | grep -q "Welcome to Mutineer BBS!"; then
    log_pass "Welcome message displayed"
else
    log_fail "Welcome message not found" "Welcome to Mutineer BBS!" "$OUTPUT"
fi

# ============================================
# TEST 2: ANSI prompt appears
# ============================================
log_test "ANSI graphics prompt appears"
OUTPUT=$(run_session 2)

if echo "$OUTPUT" | grep -q "ANSI graphics"; then
    log_pass "ANSI prompt displayed"
else
    log_fail "ANSI prompt not found" "ANSI graphics? (Y/n):" "$OUTPUT"
fi

# ============================================
# TEST 3: ANSI mode - Y response enables ANSI
# ============================================
log_test "ANSI mode enabled with Y response"
OUTPUT=$(run_session 3 "y")

if echo "$OUTPUT" | grep -q "ANSI enabled"; then
    log_pass "ANSI enabled confirmation"
else
    log_fail "ANSI enabled not shown" "ANSI enabled" "$OUTPUT"
fi

# ============================================
# TEST 4: ANSI mode - screen clear sent
# ============================================
log_test "ANSI mode sends screen clear escape code"
OUTPUT=$(run_session 3 "y")

# Looking for ESC[2J which shows as ^[[2J in cat -v
if echo "$OUTPUT" | grep -q '\[2J'; then
    log_pass "Screen clear escape code sent"
else
    log_fail "Screen clear not sent" "[2J" "$OUTPUT"
fi

# ============================================
# TEST 5: ANSI mode - color codes present
# ============================================
log_test "ANSI mode sends color escape codes"
OUTPUT=$(run_session 3 "y")

# Looking for ESC[...m color codes
if echo "$OUTPUT" | grep -qE '\[[0-9;]*m'; then
    log_pass "Color escape codes present"
else
    log_fail "Color codes not found" "[...m" "$OUTPUT"
fi

# ============================================
# TEST 6: ASCII mode - N response disables ANSI
# ============================================
log_test "ASCII mode enabled with N response"
OUTPUT=$(run_session 3 "n")

if echo "$OUTPUT" | grep -q "ASCII mode"; then
    log_pass "ASCII mode confirmation"
else
    log_fail "ASCII mode not shown" "ASCII mode" "$OUTPUT"
fi

# ============================================
# TEST 7: ASCII mode - plain text welcome
# ============================================
log_test "ASCII mode shows plain text welcome"
OUTPUT=$(run_session 3 "n")

if echo "$OUTPUT" | grep -q "Welcome to the Mutineer Telnet BBS!"; then
    log_pass "Plain text welcome in ASCII mode"
else
    log_fail "Plain text welcome not found" "Welcome to the Mutineer Telnet BBS!" "$OUTPUT"
fi

# ============================================
# TEST 8: Handle prompt appears
# ============================================
log_test "Handle prompt appears after ANSI selection"
OUTPUT=$(run_session 3 "y")

if echo "$OUTPUT" | grep -q "Handle:"; then
    log_pass "Handle prompt displayed"
else
    log_fail "Handle prompt not found" "Handle:" "$OUTPUT"
fi

# ============================================
# TEST 9: Handle input is echoed
# ============================================
log_test "Handle input is echoed back"
OUTPUT=$(run_session 4 "y" "sysop")

if echo "$OUTPUT" | grep "Handle:" | grep -q "sysop"; then
    log_pass "Handle input echoed"
else
    log_fail "Handle not echoed" "sysop after Handle:" "$OUTPUT"
fi

# ============================================
# TEST 10: Password prompt appears for existing user
# ============================================
log_test "Password prompt appears for existing user"
OUTPUT=$(run_session 4 "y" "sysop")

if echo "$OUTPUT" | grep -q "Password:"; then
    log_pass "Password prompt displayed"
else
    log_fail "Password prompt not found" "Password:" "$OUTPUT"
fi

# ============================================
# TEST 11: Password shows dots not characters
# ============================================
log_test "Password input shows dots instead of characters"
OUTPUT=$(run_session 5 "y" "sysop" "mutineer")

# "mutineer" is 8 chars, should see 8 dots after Password:
if echo "$OUTPUT" | grep "Password:" | grep -q '\.\.\.\.\.\.\.\.'; then
    log_pass "Password shows dots (8 dots for 'mutineer')"
else
    log_fail "Password dots not shown correctly" "Password: ........" "$OUTPUT"
fi

# ============================================
# TEST 12: Password does NOT echo actual characters
# ============================================
log_test "Password does NOT echo actual characters"
OUTPUT=$(run_session 5 "y" "sysop" "mutineer")

# The word "mutineer" should NOT appear after Password:
PW_LINE=$(echo "$OUTPUT" | grep "Password:")
if echo "$PW_LINE" | grep -q "mutineer"; then
    log_fail "Password was echoed in plain text!" "dots only" "$PW_LINE"
else
    log_pass "Password not echoed in plain text"
fi

# ============================================
# TEST 13: Successful login shows menu
# ============================================
log_test "Successful login shows menu"
OUTPUT=$(run_session 6 "y" "sysop" "mutineer")

if echo "$OUTPUT" | grep -q "Menu" && echo "$OUTPUT" | grep -q "Selection:"; then
    log_pass "Menu displayed after login"
else
    log_fail "Menu not displayed" "Menu ... Selection:" "$OUTPUT"
fi

# ============================================
# TEST 14: Invalid password rejected
# ============================================
log_test "Invalid password is rejected"
OUTPUT=$(run_session 5 "y" "sysop" "wrongpass")

if echo "$OUTPUT" | grep -q "Invalid password"; then
    log_pass "Invalid password rejected"
else
    log_fail "Invalid password not rejected" "Invalid password" "$OUTPUT"
fi

# ============================================
# TEST 15: New user prompt appears for unknown handle
# ============================================
log_test "New user prompt for unknown handle"
OUTPUT=$(run_session 4 "y" "test_unknown_xyz")

if echo "$OUTPUT" | grep -q "New user"; then
    log_pass "New user prompt displayed"
else
    log_fail "New user prompt not found" "New user?" "$OUTPUT"
fi

# ============================================
# TEST 16: New user - password confirmation prompt
# ============================================
log_test "New user registration asks for password confirmation"
OUTPUT=$(run_session 5 "y" "test_newuser1" "y" "testpass")

if echo "$OUTPUT" | grep -q "Confirm password:"; then
    log_pass "Password confirmation prompt displayed"
else
    log_fail "Confirm password prompt not found" "Confirm password:" "$OUTPUT"
fi

# ============================================
# TEST 17: New user - matching passwords succeed
# ============================================
log_test "New user with matching passwords succeeds"
cleanup_test_users
OUTPUT=$(run_session 10 "y" "test_newuser2" "y" "testpass" "testpass" "Boston, MA" "test2@example.com" "" "")

if echo "$OUTPUT" | grep -q "User created"; then
    log_pass "User created with matching passwords"
else
    log_fail "User not created" "User created" "$OUTPUT"
fi

# ============================================
# TEST 18: New user - mismatched passwords fail
# ============================================
log_test "New user with mismatched passwords fails"
OUTPUT=$(run_session 5 "y" "test_newuser3" "y" "password1" "password2")

if echo "$OUTPUT" | grep -q "Passwords do not match"; then
    log_pass "Mismatched passwords rejected"
else
    log_fail "Mismatched passwords not rejected" "Passwords do not match" "$OUTPUT"
fi

# ============================================
# TEST 19: New user - short password rejected
# ============================================
log_test "New user with short password rejected"
OUTPUT=$(run_session 5 "y" "test_newuser4" "y" "abc" "abc")

if echo "$OUTPUT" | grep -q "at least 4 characters"; then
    log_pass "Short password rejected"
else
    log_fail "Short password not rejected" "at least 4 characters" "$OUTPUT"
fi

# ============================================
# TEST 20: Window size displayed after login
# ============================================
log_test "Window size displayed after login"
OUTPUT=$(run_session 6 "y" "sysop" "mutineer")

if echo "$OUTPUT" | grep -qE "Window:.*[0-9]+x[0-9]+"; then
    log_pass "Window size displayed"
else
    log_fail "Window size not displayed" "Window: NNxNN" "$OUTPUT"
fi

# ============================================
# TEST 21: Menu options visible
# ============================================
log_test "Menu shows expected options"
OUTPUT=$(run_session 6 "y" "sysop" "mutineer")

MENU_OK=1
for opt in "Who" "Messages" "Files" "Chat" "Logoff"; do
    if ! echo "$OUTPUT" | grep -qi "$opt"; then
        MENU_OK=0
        break
    fi
done

if [ $MENU_OK -eq 1 ]; then
    log_pass "Menu options visible"
else
    log_fail "Some menu options missing" "Who, Messages, Files, Chat, Logoff" "$OUTPUT"
fi

# ============================================
# TEST 22: CRLF line endings (^M present)
# ============================================
log_test "Lines end with CRLF (carriage return)"
OUTPUT=$(run_session 3 "y")

CR_COUNT=$(echo "$OUTPUT" | grep -c '\^M' || echo "0")
if [ "$CR_COUNT" -gt 5 ]; then
    log_pass "CRLF line endings present ($CR_COUNT carriage returns)"
else
    log_fail "CRLF line endings missing" ">5 ^M characters" "$CR_COUNT found"
fi

# ============================================
# TEST 23: Goodbye message on failed login
# ============================================
log_test "Goodbye message on failed login"
OUTPUT=$(run_session 5 "y" "sysop" "wrongpass")

if echo "$OUTPUT" | grep -q "Goodbye"; then
    log_pass "Goodbye message displayed"
else
    log_fail "Goodbye message not found" "Goodbye" "$OUTPUT"
fi

# ============================================
# TEST 24: ASCII mode - Handle prompt without escape codes
# ============================================
log_test "ASCII mode Handle prompt has no escape codes"
OUTPUT=$(run_session 3 "n")

HANDLE_LINE=$(echo "$OUTPUT" | grep "Handle:")
if echo "$HANDLE_LINE" | grep -q '\[.*m'; then
    log_fail "Handle prompt has escape codes in ASCII mode" "Handle: (plain)" "$HANDLE_LINE"
else
    log_pass "Handle prompt is plain text in ASCII mode"
fi

# ============================================
# TEST 25: Default ANSI (just press enter)
# ============================================
log_test "Default to ANSI when just pressing enter"
OUTPUT=$(run_session 3 "")

if echo "$OUTPUT" | grep -q "ANSI enabled"; then
    log_pass "Default to ANSI on empty response"
else
    log_fail "Did not default to ANSI" "ANSI enabled" "$OUTPUT"
fi

# ============================================
# TEST 26: Handle and Password on separate lines
# ============================================
log_test "Handle and Password prompts on separate lines"
OUTPUT=$(run_session 5 "y" "sysop" "mutineer")

# Check that there's a ^M (CR) after the handle input before Password:
if echo "$OUTPUT" | grep -q "sysop\^M" && echo "$OUTPUT" | grep -q "Password:"; then
    log_pass "Handle followed by newline before Password"
else
    log_fail "Handle/Password formatting issue" "sysop^M ... Password:" "$OUTPUT"
fi

# ============================================
# TEST 27: Y echo on ANSI prompt
# ============================================
log_test "Y is echoed on ANSI prompt"
OUTPUT=$(run_session 3 "y")

if echo "$OUTPUT" | grep "ANSI graphics" | grep -q "y"; then
    log_pass "Y echoed on ANSI prompt"
else
    log_fail "Y not echoed" "ANSI graphics? (Y/n): y" "$OUTPUT"
fi

# ============================================
# TEST 28: Confirm password shows dots
# ============================================
log_test "Confirm password also shows dots"
OUTPUT=$(run_session 6 "y" "test_newuser5" "y" "testpass" "testpass")

if echo "$OUTPUT" | grep "Confirm password:" | grep -q '\.\.\.\.'; then
    log_pass "Confirm password shows dots"
else
    log_fail "Confirm password dots not shown" "Confirm password: ...." "$OUTPUT"
fi

# ============================================
# TEST 29: Backspace works on handle input
# ============================================
log_test "Backspace erases character on handle input"
# Send "sysopx" then backspace (0x08 or 0x7f) to correct it
OUTPUT=$( { sleep 0.3; printf 'y\n'; sleep 0.3; printf 'sysopx\x7f\n'; sleep 0.3; printf 'mutineer\n'; sleep 3; } | timeout 8 nc localhost "$PORT" 2>/dev/null | cat -v )

if echo "$OUTPUT" | grep -q "Menu"; then
    log_pass "Backspace correction allowed successful login"
else
    log_fail "Backspace correction failed" "Menu after login" "$OUTPUT"
fi

# ============================================
# TEST 30: ASCII mode login flow works
# ============================================
log_test "ASCII mode full login flow works"
OUTPUT=$(run_session 6 "n" "sysop" "mutineer")

if echo "$OUTPUT" | grep -q "Menu" && echo "$OUTPUT" | grep -q "Selection:"; then
    log_pass "ASCII mode login shows menu"
else
    log_fail "ASCII mode login failed" "Menu ... Selection:" "$OUTPUT"
fi

# ============================================
# TEST 31: New user in ASCII mode
# ============================================
log_test "New user registration works in ASCII mode"
cleanup_test_users
OUTPUT=$(run_session 12 "n" "test_ascii_user" "y" "testpass" "testpass" "Denver, CO" "ascii@test.com" "" "")

if echo "$OUTPUT" | grep -q "User created"; then
    log_pass "New user created in ASCII mode"
else
    log_fail "ASCII mode user creation failed" "User created" "$OUTPUT"
fi

# ============================================
# TEST 32: Telnet negotiation bytes present
# ============================================
log_test "Telnet negotiation bytes sent on connect"
OUTPUT=$(run_session 2)

# Telnet IAC bytes show as M-^? in cat -v
if echo "$OUTPUT" | grep -q "M-\^?"; then
    log_pass "Telnet negotiation bytes present"
else
    log_fail "No telnet negotiation" "M-^? (IAC bytes)" "$OUTPUT"
fi

# ============================================
# TEST 33: Empty handle rejected
# ============================================
log_test "Empty handle is rejected"
OUTPUT=$(run_session 4 "y" "")

# Should still show Handle: prompt again or error
if echo "$OUTPUT" | grep -c "Handle:" | grep -qE "[2-9]|[1-9][0-9]"; then
    log_pass "Empty handle prompts again"
else
    # Alternative: check if it doesn't proceed to Password
    if ! echo "$OUTPUT" | grep -q "Password:"; then
        log_pass "Empty handle does not proceed to password"
    else
        log_fail "Empty handle accepted" "Re-prompt or rejection" "$OUTPUT"
    fi
fi

# ============================================
# TEST 34: MOTD displayed in ANSI mode
# ============================================
log_test "MOTD displayed in ANSI mode"
OUTPUT=$(run_session 3 "y")

if echo "$OUTPUT" | grep -qi "seas are green\|hoist the flag\|Mutineer Telnet BBS"; then
    log_pass "MOTD content displayed"
else
    log_fail "MOTD not displayed" "MOTD text" "$OUTPUT"
fi

# ============================================
# TEST 35: Session info displayed after login
# ============================================
log_test "Session info (Window size) displayed after login"
OUTPUT=$(run_session 6 "y" "sysop" "mutineer")

# Window is always displayed; Terminal only if client negotiates it
if echo "$OUTPUT" | grep -qE "Window:.*[0-9]+x[0-9]+"; then
    log_pass "Session info displayed"
else
    log_fail "Session info not displayed" "Window: NNxNN" "$OUTPUT"
fi

# ============================================
# TEST 36: New user registration prompts for city/state
# ============================================
log_test "New user registration prompts for City, State/Region"
OUTPUT=$(run_session 6 "y" "test_reg_city" "y" "testpass" "testpass")

if echo "$OUTPUT" | grep -q "City, State/Region:"; then
    log_pass "City/State prompt displayed"
else
    log_fail "City/State prompt not found" "City, State/Region:" "$OUTPUT"
fi

# ============================================
# TEST 37: New user registration prompts for email
# ============================================
log_test "New user registration prompts for email"
OUTPUT=$(run_session 7 "y" "test_reg_email" "y" "testpass" "testpass" "Austin, TX")

if echo "$OUTPUT" | grep -q "Email address:"; then
    log_pass "Email prompt displayed"
else
    log_fail "Email prompt not found" "Email address:" "$OUTPUT"
fi

# ============================================
# TEST 38: New user registration prompts for social link
# ============================================
log_test "New user registration prompts for social link (optional)"
OUTPUT=$(run_session 8 "y" "test_reg_social" "y" "testpass" "testpass" "Austin, TX" "test@example.com")

if echo "$OUTPUT" | grep -q "Social media link"; then
    log_pass "Social link prompt displayed"
else
    log_fail "Social link prompt not found" "Social media link" "$OUTPUT"
fi

# ============================================
# TEST 39: New user registration prompts for sysop message
# ============================================
log_test "New user registration prompts for sysop message (optional)"
OUTPUT=$(run_session 9 "y" "test_reg_sysmsg" "y" "testpass" "testpass" "Austin, TX" "test@example.com" "")

if echo "$OUTPUT" | grep -q "Message to SysOp"; then
    log_pass "Sysop message prompt displayed"
else
    log_fail "Sysop message prompt not found" "Message to SysOp" "$OUTPUT"
fi

# ============================================
# TEST 40: Full registration creates user successfully
# ============================================
log_test "Full registration with all fields creates user"
cleanup_test_users
OUTPUT=$(run_session 10 "y" "test_fullreg2" "y" "testpass" "testpass" "Seattle, WA" "user@test.com" "https://github.com/test" "Hello!")

if echo "$OUTPUT" | grep -q "User created"; then
    log_pass "Full registration succeeded"
else
    log_fail "Full registration failed" "User created" "$OUTPUT"
fi

# ============================================
# TEST 41: Registration requires city/state
# ============================================
log_test "Registration requires city/state (not empty)"
OUTPUT=$(run_session 7 "y" "test_nocity" "y" "testpass" "testpass" "")

if echo "$OUTPUT" | grep -q "City/State is required"; then
    log_pass "Empty city/state rejected"
else
    log_fail "Empty city/state not rejected" "City/State is required" "$OUTPUT"
fi

# ============================================
# TEST 42: Registration requires valid email
# ============================================
log_test "Registration requires valid email"
OUTPUT=$(run_session 8 "y" "test_bademail" "y" "testpass" "testpass" "Austin, TX" "notanemail")

if echo "$OUTPUT" | grep -q "Valid email"; then
    log_pass "Invalid email rejected"
else
    log_fail "Invalid email not rejected" "Valid email" "$OUTPUT"
fi

# ============================================
# Cleanup
# ============================================
cleanup_test_users

echo ""
echo "========================================"
echo "  Test Results"
echo "========================================"
echo -e "  Tests run: ${TESTS_RUN}"
echo -e "  ${GREEN}Passed: ${PASS}${NC}"
echo -e "  ${RED}Failed: ${FAIL}${NC}"
echo "========================================"

if [ $FAIL -gt 0 ]; then
    exit 1
fi
exit 0
