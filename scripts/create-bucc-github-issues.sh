#!/usr/bin/env bash
# Create GitHub issues from BUCC_TODO.md (rickcollette/mutineer)
set -euo pipefail
REPO="rickcollette/mutineer"
SRC="BUCC_TODO.md"

gh label create buccaneer --repo "$REPO" --color "1d76db" --description "Buccaneer VM / BUCC runtime" 2>/dev/null || true

create() {
  local labels="$1"
  local title="$2"
  local body="$3"
  gh issue create --repo "$REPO" --title "$title" --label "$labels" --body "$body"
}

FOOTER="$(cat <<EOF

---
_Sourced from \`$SRC\` in the mutineer-bbs workspace (Buccaneer VM audit)._
EOF
)"

# BUGS
create "bug,buccaneer" "[BUCC] BUG-1: DOOR.CHAIN sets VM_HALT instead of VM_CHAIN" "$(cat <<EOF
**Severity:** Critical | **File:** \`host.c:745\`

\`bucc_host_door_chain()\` terminates the VM with \`ctx->vm->status = VM_HALT\`. The runner in \`bbs.c:228-235\` checks for \`VM_CHAIN\` to populate \`chain_target\` and \`chain_args\`. Setting \`VM_HALT\` causes the runner to treat a chain as a normal successful exit and silently discard the chain target.

\`\`\`c
// host.c:745 — wrong:
ctx->vm->status = VM_HALT;
// should be:
ctx->vm->status = VM_CHAIN;
\`\`\`
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] BUG-2: DOOR.EXIT discards exit code" "$(cat <<EOF
**Severity:** Medium | **File:** \`host.c:721\`

\`bucc_host_door_exit()\` reads the argument (\`code = (int)bucc_value_to_int(...)\`) then suppresses it with \`(void)code;\`. The \`exit_code\` field of \`bucc_door_result_t\` is never populated; callers cannot distinguish a normal exit from one with a specific code.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] BUG-3: OP_ARRAY_NEW with count=0 calls malloc(0)" "$(cat <<EOF
**Severity:** High | **File:** \`vm.c:807\`

\`\`\`c
bucc_value_t* items = malloc(count * sizeof(bucc_value_t));
for (int i = count - 1; i >= 0; i--) {
    items[i] = bucc_vm_pop(vm);   // UB if items == NULL
}
\`\`\`

\`malloc(0)\` is implementation-defined; on platforms that return NULL, the subsequent indexed write is undefined behavior. Guard with \`if (count > 0)\`.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] BUG-4: OP_DISPATCH_CALL discards const on vm->module" "$(cat <<EOF
**Severity:** Low | **File:** \`vm.c:1076\`

\`\`\`c
bucc_module_proc_t* proc = &((bucc_module_t*)vm->module)->procedures[proc_id];
\`\`\`

\`vm->module\` is \`const bucc_module_t*\`. The cast discards \`const\` (UB if truly const). Use a local \`const bucc_module_proc_t*\` instead.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] BUG-5: OP_DISPATCH_CALL out-of-range selector breaks stack" "$(cat <<EOF
**Severity:** Medium | **File:** \`vm.c:1060-1090\`

When \`index < 1\` or \`index > target_count\` the selector is consumed but nothing is pushed. Callers expecting a return value will hit a silent stack underflow on the next operation.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] BUG-6: OP_CALL_HOST silently truncates args beyond 16" "$(cat <<EOF
**Severity:** Medium | **File:** \`vm.c:709\`

\`\`\`c
int actual_argc = argc < 16 ? argc : 16;
\`\`\`

No error is raised. Scripts passing more than 16 arguments have trailing args silently dropped.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] BUG-7: OP_DISPATCH_CALL always emits base_proc_id=0" "$(cat <<EOF
**Severity:** Critical | **File:** \`emit.c:793\`

\`\`\`c
bucc_emit_op_u16(emit, OP_DISPATCH_CALL, 0);   // wrong for any real ON CALL
\`\`\`

The \`ON CALL\` emitter never looks up the actual starting procedure ID. Every \`ON CALL\` dispatch incorrectly starts from procedure 0.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] BUG-8: SHARED.CAS uses shallow equality for map/array" "$(cat <<EOF
**Severity:** High | **File:** \`host.c:614\`

\`bucc_value_equals\` compares pointer identity for arrays/maps, not deep equality. \`bucc_value_equal\` is the correct deep-equality function. CAS on a map or array expected value will always report mismatch.

Related: DESIGN-1 (dual equality APIs).
$FOOTER
EOF
)"

# MISSING
create "enhancement,buccaneer" "[BUCC] MISSING-1: TERM.GOTOXY registered but never dispatched" "$(cat <<EOF
**Severity:** High | **File:** \`host.c\`

\`host_functions[]\` registers \`TERM.GOTOXY\` and \`bucc_term_api_t\` has \`.gotoxy\`, but \`bucc_host_dispatch()\` has no \`GOTOXY\` case and no \`bucc_host_term_gotoxy()\` exists. Runtime always returns "Unimplemented host function".

**Fix:** Add \`bucc_host_term_gotoxy()\`, declare in \`bucc_host.h\`, add dispatch case.
$FOOTER
EOF
)"

create "enhancement,buccaneer" "[BUCC] MISSING-2: TERM.PAUSE, SUPPORTS_ANSI, INPUT_PASSWORD absent" "$(cat <<EOF
**Severity:** Medium | **File:** \`host.c\` / \`host.h\`

\`bucc_term_api_t\` defines \`.pause\`, \`.supports_ansi\`, \`.input_password\` (simulator implements them). Not in \`host_functions[]\`, no handlers in \`host.c\`, \`TERM.INPUT\` never uses \`.input_password\`.
$FOOTER
EOF
)"

create "enhancement,buccaneer" "[BUCC] MISSING-3: SESSION.NODE() and SESSION.ELAPSED_MS() absent" "$(cat <<EOF
**Severity:** Medium | **Spec:** Buccaneer_SPEC.md:399-400

Documented but not in \`host_functions[]`, not dispatched, no handlers. \`BBS.NODE\` partially covers node under a different namespace.
$FOOTER
EOF
)"

create "enhancement,buccaneer" "[BUCC] MISSING-4: USER.TIME_LEFT name mismatch vs spec" "$(cat <<EOF
**Severity:** Low | **File:** \`host.c:41\`

Spec documents \`USER.TIME_LEFT()\`; implementation registers \`USER.TIMELEFT\`. Scripts using the spec name get "Unknown host function".
$FOOTER
EOF
)"

# WIRE
create "enhancement,buccaneer" "[BUCC] WIRE-1: Missing users/msg/file API setters in bucc_bbs.h" "$(cat <<EOF
**Severity:** Medium | **File:** \`bbs.c\` / \`bucc_bbs.h\`

\`bucc_door_set_users_api\`, \`bucc_door_set_msg_api\`, \`bucc_door_set_file_api\` are missing from the embedding API (only lower-level \`bucc_host_set_*\` exists).
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] WIRE-2: All API context pointers passed as NULL" "$(cat <<EOF
**Severity:** Critical | **File:** \`bbs.c:108-136\`

Every \`bucc_door_set_*\` calls \`bucc_host_set_*(..., NULL)\`. \`session_ctx\`, \`user_ctx\`, etc. are always NULL — API implementations that dereference \`ctx\` will segfault or malfunction.

Setters need a \`void* context\` parameter or \`bucc_door_runner_t\` must carry context pointers.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] WIRE-3: No runner=bucc dispatch path in doors.c" "$(cat <<EOF
**Severity:** Critical | **File:** \`src/doors.c:873-892\`

\`door_launch()\` supports \`runner=dosbox\` and \`runner=native\` but not \`runner=bucc\`. BUCC doors cannot be launched from the BBS door menu; \`bucc_door_run_simple()\` is never called from BBS code.
$FOOTER
EOF
)"

# SECURITY
create "bug,buccaneer" "[BUCC] SEC-1: Capability enforcement absent in bucc_host_dispatch" "$(cat <<EOF
**Severity:** Critical | **File:** \`host.c\`

\`host_functions[]\` entries have \`.capability\` strings and \`bucc_door_runner_t\` tracks \`allowed_capabilities\`, but \`bucc_host_dispatch()\` never checks them. Any script can call any host function regardless of \`door.json\`.

Capability mask must be threaded into host context before dispatch.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] SEC-2: bucc_door_run_simple() grants BUCC_CAP_ALL" "$(cat <<EOF
**Severity:** High | **File:** \`bbs.c:303\`

\`\`\`c
runner->allowed_capabilities = BUCC_CAP_ALL;
\`\`\`

Convenience runner grants all capabilities including \`data.write\`, \`text.write\`, \`bbs.message\` to untrusted paths. Should default to \`BUCC_CAP_SAFE\`.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] SEC-3: Throttle only covers USERS.FIND" "$(cat <<EOF
**Severity:** Medium | **File:** \`host.c:954-963\`

Rate limiting only for \`USERS.FIND\`. Spec §14.2 expects throttling on sensitive calls (\`BBS.SENDMSG\`, \`MSG.POST\`, \`DATA.*\`, etc.). \`throttled: true\` in \`host_functions[]\` is never read.
$FOOTER
EOF
)"

create "bug,buccaneer" "[BUCC] SEC-4: SHARED.* operations not thread-safe" "$(cat <<EOF
**Severity:** High | **File:** \`host.c:557-622\`

\`ctx->shared_state\` is read/written by concurrent BUCC sessions with no mutex. \`SHARED.CAS\` as an atomic primitive is broken without locking.
$FOOTER
EOF
)"

# DEAD
create "enhancement,buccaneer" "[BUCC] DEAD-1: OP_RANGE_TEST implemented but never emitted" "$(cat <<EOF
**Severity:** Low | **File:** \`vm.c:1092-1117\`

Opcode fully implemented but no parser/emitter path. Unreachable unless hand-crafted bytecode.

Either add \`x BETWEEN low AND high\` grammar + emitter, or remove the opcode.
$FOOTER
EOF
)"

# BUILD
create "enhancement,buccaneer" "[BUCC] BUILD-1: Buccaneer not in main CMakeLists.txt" "$(cat <<EOF
**Severity:** High | **File:** \`CMakeLists.txt\`

\`src/buccaneer/\` builds via standalone Makefile only. BUCC is not linked into \`mutineer\`, tests not in \`ctest\`, not built by normal \`cmake --build\`.
$FOOTER
EOF
)"

create "enhancement,buccaneer" "[BUCC] BUILD-2: make test does not run buccaneer test suites" "$(cat <<EOF
**Severity:** Medium | **File:** \`src/buccaneer/Makefile\`

\`make test\` only runs \`bucc version\`. \`test_vm.c\`, \`test_e2e.c\`, \`test_lexer.c\`, etc. exist under \`tests/\` but are not compiled or executed by the \`test\` target.
$FOOTER
EOF
)"

# DESIGN
create "enhancement,buccaneer" "[BUCC] DESIGN-1: Dual equality APIs; wrong one in SHARED.CAS" "$(cat <<EOF
**Severity:** Medium | **File:** \`value.c\` / \`host.c:614\`

- \`bucc_value_equal\` — deep equality
- \`bucc_value_equals\` — shallow (pointer identity for arrays/maps)

\`SHARED.CAS\` uses \`bucc_value_equals\`. Document clearly or consolidate APIs. Overlaps BUG-8.
$FOOTER
EOF
)"

create "enhancement,buccaneer" "[BUCC] DESIGN-2: USER.FLAGS missing from host dispatch" "$(cat <<EOF
**Severity:** Low | **File:** \`host.c\` / \`host.h:56\`

\`bucc_user_api_t\` has \`get_flags\` but no \`USER.FLAGS\` in \`host_functions[]\` or dispatch. Scripts cannot read user AR/AC flags.
$FOOTER
EOF
)"

create "enhancement,buccaneer" "[BUCC] DESIGN-3: bucc_door_result_t.exit_code never set" "$(cat <<EOF
**Severity:** Low | **File:** \`bucc_bbs.h:38\`

\`exit_code\` is never populated by \`bucc_door_run()\` or \`bucc_host_door_exit()\`. Always 0. Related to BUG-2.
$FOOTER
EOF
)"

create "enhancement,buccaneer" "[BUCC] DESIGN-4: Simulator TERM functions missing in host.c" "$(cat <<EOF
**Severity:** Medium | **Files:** \`tools/simulator.c\` vs \`host.c\`

Simulator implements \`gotoxy\`, \`pause\`, \`supports_ansi\`, \`input_password\`. Main host layer does not — doors work in simulator but fail in production.
$FOOTER
EOF
)"

echo "All BUCC issues created."
